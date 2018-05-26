#include "precompiled.hpp"
#include "../automaton.hpp"
#include "../worker.hpp"
#include "../pinning.hpp"
#include "canonicalize.hpp"
#include "registry.hpp"
#include "ops.hpp"
#include "gadgetdefs.hpp"
#include "stringutils.hpp"

using namespace automaton;
using std::get;

template<typename Duration>
std::string hms(Duration diff) {
	using std::chrono::duration_cast;
	auto hours = duration_cast<std::chrono::hours>(diff);
	auto minutes = duration_cast<std::chrono::minutes>(diff) - hours;
	auto seconds = duration_cast<std::chrono::seconds>(diff) - hours - minutes;
	return std::to_string(hours.count()) + "h" + std::to_string(minutes.count()) + "m" + std::to_string(seconds.count()) + "s";
}
std::string hms(std::chrono::steady_clock::time_point end, std::chrono::steady_clock::time_point begin) {
	return hms(end - begin);
}

struct Input {
	Registry::index_type index;
	automaton_type normal, mirror; //mirror is empty if the input is not chiral
	automaton_type::symbol_type active_alphabet_size;
};

struct Target {
	std::size_t packed_hash, mirror_packed_hash;
	std::unique_ptr<const PackedAutomaton> normal, mirror;
};

static Registry registry;
static std::vector<Input> inputs; //TODO: registry has to register inputs before anything else in FIFO order
static std::vector<Target> targets;
static bounded_queue<std::function<void()>> issue(4*std::thread::hardware_concurrency());
static bounded_queue<Result> retire(4*std::thread::hardware_concurrency());

[[gnu::cold]]
void print_provenance_backtrace(AutomatonBase& thing, Provenance& provenance) {
	circular_deque<std::uint32_t, 32> queue;
	linear_set<std::uint32_t> printed;

	std::cout << "<found> = " << provenance << " " << thing << '\n';
	for (auto p : provenance.parents())
		if (printed.insert(p).second)
			queue.push_back(p);

	while (!queue.empty()) {
		auto idx = queue.pop_front();
		auto prov = registry.provenance(idx);
		std::cout << idx << " = " << prov << '\n';
		//Ideally we'd print the automaton here, but we're no longer
		//maintaining an id->automaton map.  We'll have to replay the
		//log this code is printing out.
		for (auto p : prov.parents())
			if (printed.insert(p).second)
				queue.push_back(p);
	}

	std::cout << std::flush;
	std::quick_exit(0);
}

constexpr static unsigned int prepare_size = 10000, max_state_cutoff = 100;
void finish(automaton_type thing, Provenance provenance, Result& finishArg) {
	thing.minimize();
	if (registry.registered_size() >= prepare_size && thing.state_size() > max_state_cutoff)
		return;

	canonicalize(thing, thing.active_alphabet_size());
	std::unique_ptr<const PackedAutomaton> packed = pack(thing);
	std::size_t hash = packed->packed_hash();

	for (const Target& t : targets)
		if ((hash == t.packed_hash && *packed == *t.normal) ||
				(hash == t.mirror_packed_hash && *packed == *t.mirror))
			print_provenance_backtrace(thing, provenance);

	for (auto& r : finishArg)
		if (get<2>(r) == hash && *get<0>(r) == *packed)
			return;
	finishArg.emplace_back(std::move(packed), provenance, hash);
}


class Combine {
public:
	Combine(const PackedAutomaton* source, Registry::index_type sourceIndex)
			: source_(source), sourceIndex_(sourceIndex) {}
	void operator()() {
		Result result;
		automaton_type unpacked(*source_);
		automaton_type::symbol_type leftLocations = unpacked.active_alphabet_size();
		automaton_type mirrored = mirror(unpacked);
		bool shouldmirror = unpacked == mirrored;
		auto finishAction = [&](automaton_type&& a, Provenance p) {
			finish(std::move(a), p, result);
		};
		for (const Input& i : inputs) {
			if (leftLocations + i.active_alphabet_size > automaton_type::alphabet_size_v) continue;
			combine(unpacked, sourceIndex_, false, leftLocations, i.normal, i.index, false, i.active_alphabet_size, finishAction);
			if (i.mirror.state_size())
				combine(unpacked, sourceIndex_, false, leftLocations, i.mirror, i.index, true, i.active_alphabet_size, finishAction);
			if (shouldmirror) {
				combine(mirrored, sourceIndex_, true, leftLocations, i.normal, i.index, false, i.active_alphabet_size, finishAction);
				if (i.mirror.state_size()) //TODO: the both-mirrored combine may be redundant
					combine(mirrored, sourceIndex_, true, leftLocations, i.mirror, i.index, true, i.active_alphabet_size, finishAction);
			}
		}
		retire.put(std::move(result));
	}
private:
	const PackedAutomaton* source_;
	Registry::index_type sourceIndex_;
};

class Connect {
public:
	Connect(std::vector<std::pair<const PackedAutomaton*, Registry::index_type>> connectibles) : connectibles_(std::move(connectibles)) {}
	void operator()() {
		Result result;
		auto finishAction = [&](automaton_type&& a, Provenance p) {
			finish(std::move(a), p, result);
		};
		for (auto& [pack, index] : connectibles_) {
			automaton_type inflated(*pack);
			automaton_type mirrored = mirror(inflated);
			automaton_type::symbol_type locations = inflated.active_alphabet_size();
			connect(inflated, index, false, locations, finishAction);
			if (inflated != mirrored)
				connect(mirrored, index, true, locations, finishAction);
		}
		retire.put(std::move(result));
	}
private:
	std::vector<std::pair<const PackedAutomaton*, Registry::index_type>> connectibles_;
};



void registrar_thread(int core_number) {
	pin_this_thread(core_number);

	unsigned int min_input_locations = automaton_type::alphabet_size_v;
	for (const Input& input : inputs)
		min_input_locations = std::min(input.active_alphabet_size, min_input_locations);

	unsigned int inflight = 0;
	std::vector<std::pair<const PackedAutomaton*, Registry::index_type>> connectibles;

	auto is_combinable = [&](unsigned int locations) {
		return locations + min_input_locations <= automaton_type::alphabet_size_v;
	};
	auto is_connectible = [](unsigned int locations) {
		return locations > 3;
	};
	automaton_type::state_type maxregisteredstates = 0;
	auto try_issue_combine = [&]() {
		auto [pack, index] = registry.register_next();
		if (index >= 10000)
			maxregisteredstates = std::max(maxregisteredstates, pack->state_size());
		unsigned int locations = pack->active_alphabet_size();
		if (is_connectible(locations))
			connectibles.emplace_back(pack, index);
		if (!is_combinable(locations)) return false;
		issue.put(Combine(pack, index));
		++inflight;
		return true;
	};
	auto connect_pending = [&]() {
		return !connectibles.empty();
	};
	auto issue_connect = [&]() {
		issue.put(Connect(std::move(connectibles)));
		connectibles.clear(); //https://stackoverflow.com/q/9168823/3614835
		++inflight;
	};
	automaton_type::state_type maxstates = 0;
	std::size_t totalstates = 0, maxedges = 0, maxtransitions = 0, totaledges = 0, totaltransitions = 0;
	auto do_retire = [&]() {
		Result res = retire.take();
		--inflight;
		for (auto& [pack, provenance, unused] : res) { //can't use [[maybe_unused]] in decomposition declarations
			//TODO: collecting these stats from packed form may be slow
			auto thesestates = pack->state_size();
			auto theseedges = pack->edge_size();
			auto thesetransitions = pack->transition_size();
			if (registry.offer(std::move(pack), provenance)) {
				totalstates += thesestates;
				maxstates = std::max(maxstates, thesestates);
				totaledges += theseedges;
				maxedges = std::max(maxedges, theseedges);
				totaltransitions += thesetransitions;
				maxtransitions = std::max(maxtransitions, thesetransitions);
			}
		}
	};

	const auto threadStarted = std::chrono::steady_clock::now();

	//Initialization phase: issue until workers are busy, retiring only if necessary
	auto in_init = [&](){return inflight < 3*std::thread::hardware_concurrency();};
	while (in_init()) {
		if (registry.waiting_size() == 0 && inflight == 0) {
			std::cout << "exited during initialization after " << registry.registered_size() << " registrations" << std::endl;
			std::exit(0);
		}

		while (in_init() && registry.waiting_size())
			try_issue_combine();
		if (in_init() && connect_pending())
			issue_connect();
		if (in_init())
			do_retire();
	}
	std::cout << "Completed initialization: " << registry.registered_size() << " registered, " << registry.waiting_size() << " waiting\n" << std::flush;

	auto lastReport = std::chrono::steady_clock::now();
	//Steady state: alternate issuance and retirement, periodically issuing a connect task
	while (registry.waiting_size() || connect_pending() || inflight) {
		//while we have fewer inflight tasks than we'd like, and there are some to issue
		while (in_init() && (registry.waiting_size() || connect_pending())) {
			if (connect_pending() && (connectibles.size() >= 100 || !registry.waiting_size()))
				issue_connect();
			else while (registry.waiting_size())
				if (try_issue_combine())
					break;
		}

		if (inflight)
			do_retire();

		using namespace std::chrono_literals;
		auto now = std::chrono::steady_clock::now();
		if ((now - lastReport) > 30s) {
			auto elapsed = now - threadStarted;
			auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
			rusage usagestats = {};
			getrusage(RUSAGE_SELF, &usagestats);
			std::chrono::seconds userSeconds(usagestats.ru_utime.tv_sec);
			double efficiency = ((double)userSeconds.count())/((double)elapsedSeconds.count());
			double gb = ((double)usagestats.ru_maxrss) / (1024*1024);

			std::cout << std::fixed << std::setprecision(2);
			std::cout << registry.registered_size() << " registered, " << registry.waiting_size() << " waiting, "
					<< hms(elapsed) << " elapsed, " << hms(userSeconds) << " user (" << efficiency << "), "
					<< gb << " GiB"
					<< ", " << maxstates << " maxstates, " << maxedges << " maxedges, " << maxtransitions << " maxtransitions, "
					<< totalstates << " totalstates, " << totaledges << " totaledges, " << totaltransitions << " totaltransitions"
					<< ", " << maxregisteredstates << " maxregisteredstates"
					<< std::endl;
			lastReport = now;
		}

//		if (registry.registered_size() >= 4000)
//			std::quick_exit(0);
	}
	std::cout << "exiting after " << registry.registered_size() << " registrations" << std::endl;
	std::exit(0);
}

int main(int argc, char* argv[]) { //genbuild entrypoint
	char hostname[64];
	gethostname(hostname, sizeof(hostname));
	std::cout << "running on " << hostname << "\n";

	std::vector<std::string_view> tokens = split_view(argv[1], ',');
	for (unsigned int i = 0; i < tokens.size(); ++i) {
		std::cout << "input " << i << ": " << tokens[i] << "\n";
		automaton_type a = *known_gadget(tokens[i], automaton_type::alphabet_size_v);
		automaton_type mirrored = mirror(a);
		if (mirrored == a)
			mirrored.clear();
		inputs.push_back(Input{i, a, mirrored, a.active_alphabet_size()});
		registry.offer(pack(a), Provenance(i));
	}

	tokens = split_view(argv[2], ',');
	for (unsigned int i = 0; i < tokens.size(); ++i) {
		std::cout << "target " << i << ": " << tokens[i] << "\n";
		automaton_type a = *known_gadget(tokens[i], automaton_type::alphabet_size_v);
		automaton_type mirrored = mirror(a);
		std::unique_ptr<const PackedAutomaton> packed = pack(a);
		std::size_t packed_hash = packed->packed_hash();
		std::unique_ptr<const PackedAutomaton> mirror_packed = pack(mirrored);
		std::size_t mirror_packed_hash = mirror_packed->packed_hash();
		if (mirrored == a)
			mirror_packed_hash = 0; //we'll just zero the hash and hope we don't get a super-unlucky collision
		targets.push_back(Target{packed_hash, mirror_packed_hash, std::move(packed), std::move(mirror_packed)});
	}
	std::cout << std::flush;

	for (unsigned int i = 1; i < std::thread::hardware_concurrency(); ++i) {
//	for (unsigned int i = 1; i < 2; ++i) {
		std::thread worker(&worker_thread, i, std::ref(issue));
		worker.detach();
	}
	std::thread registrar(&registrar_thread, 0);
	registrar.join(); //not intended to return

	return 0;
}
