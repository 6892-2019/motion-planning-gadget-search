#include "precompiled.hpp"
#include "../automaton.hpp"
#include "../regex.hpp"
#include "../alphabet.hpp"
#include "../worker.hpp"
#include "../pinning.hpp"

#include "canonicalize.hpp"
#include "registry.hpp"
#include "ops.hpp"

using std::get;

static Registry registry(900001);
static bounded_queue<std::function<void()>> issue(4*std::thread::hardware_concurrency());
static bounded_queue<Result> retire(4*std::thread::hardware_concurrency());

class Combine {
public:
	Combine(Registry::index_type i, bounded_queue<Result>& retire) : i_(i), retire_(retire) {}
	void operator()() {
		Result result;
		for (Registry::index_type j = 0; j <= i_; ++j)
			combine(i_, j, registry, result);
		retire_.put(std::move(result));
	}
private:
	Registry::index_type i_;
	bounded_queue<Result>& retire_;
};

class Connect {
public:
	Connect(Registry::index_type min, Registry::index_type max, bounded_queue<Result>& retire) : min_(min), max_(max), retire_(retire) {}
	void operator()() {
		Result result;
		for (Registry::index_type i = min_; i < max_; ++i)
			connect(i, registry, result);
		retire_.put(std::move(result));
	}
private:
	Registry::index_type min_, max_;
	bounded_queue<Result>& retire_;
};

void finish(Gadget&& gadget, Provenance provenance, Result& finishArg) {
	std::size_t hash = std::hash<automaton_type>()(*gadget.a_);
	//TODO: we could augment Result with a hash table to avoid this scan
	for (auto& r : finishArg)
		if (get<2>(r) == hash && *get<0>(r).a_ == *gadget.a_)
			return;
	finishArg.emplace_back(std::move(gadget), provenance, hash);
}

void registrar_thread(int core_number, const std::vector<std::pair<std::size_t, automaton_type>>& targets) {
	pin_this_thread(core_number);

	unsigned int inflight = 0;
	Registry::index_type connectWatermark = 0;

	auto is_combinable = [&](Registry::index_type i) {
		return registry.at(i).locations_ + 2 < automaton_type::alphabet_size_v;
	};
	auto try_issue_combine = [&]() {
		Registry::index_type i = registry.register_next();
		if (!is_combinable(i)) return false;
		issue.put(Combine(i, retire));
		++inflight;
		return true;
	};
	auto connect_pending = [&]() {
		return connectWatermark != registry.registered_size();
	};
	auto issue_connect = [&]() {
		issue.put(Connect(connectWatermark, registry.registered_size(), retire));
		connectWatermark = registry.registered_size();
		++inflight;
	};
	auto do_retire = [&]() {
		Result res = retire.take();
		--inflight;
		for (auto r : res) { //TODO: decomposition declaration!
			const Gadget& g = get<0>(r);
			const Provenance& p = get<1>(r);
			std::size_t hash = get<2>(r);
			if (registry.offer(g, p, hash)) {
				for (const auto& t : targets)
					if (t.first == hash && t.second == *g.a_) {
						std::cout << "found! TODO details" << std::endl;
						std::exit(0);
					}
			}
		}
	};

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
	std::cout << "Completed initialization: " << registry.registered_size() << " registered, " << registry.waiting_size() << " waiting\n";

	//Steady state: alternate issuance and retirement, periodically issuing a connect task
	while (registry.waiting_size() || connect_pending() || inflight) {
		if ((connectWatermark + 100) < registry.registered_size() || !registry.waiting_size())
			issue_connect();
		else while (registry.waiting_size())
			if (try_issue_combine())
				break;

		if (inflight)
			do_retire();
	}
	std::cout << "exiting after " << registry.registered_size() << " registrations" << std::endl;
	std::exit(0);
}

int main(int argc, char* argv[]) {
	auto split = known_gadget("split");
	std::cout << *split.a_ << std::endl;
	auto splithash = std::hash<automaton_type>()(*split.a_);
	registry.offer(split, Provenance(0), splithash);

	auto parallelToggle = known_gadget("parallel-2-toggle");
	std::cout << *parallelToggle.a_ << std::endl;
	auto parallelToggleHash = std::hash<automaton_type>()(*parallelToggle.a_);
	registry.offer(parallelToggle, Provenance(1), parallelToggleHash);

	automaton_type antiparallelToggle = *known_gadget("antiparallel-2-toggle").a_;
	std::cout << antiparallelToggle << std::endl;

	//TODO: more targets (with/without noop)
	std::vector<std::pair<std::size_t, automaton_type>> targets = {
		{std::hash<automaton_type>()(antiparallelToggle), std::move(antiparallelToggle)},
	};
	for (int i = 1; i < std::thread::hardware_concurrency(); ++i) {
		std::thread worker(&worker_thread, i, std::ref(issue));
		worker.detach();
	}
	std::thread registrar(&registrar_thread, 0, targets);
	registrar.join(); //not intended to return

	return 0;
}
