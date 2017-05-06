#include "precompiled.hpp"
#include "../automaton.hpp"
#include "../regex.hpp"
#include "../alphabet.hpp"
#include "canonicalize.hpp"

#include "registry.hpp"
#include "ops.hpp"
#include "../worker.hpp"

using std::get;
using alphabet_type = ByteAlphabet<8>;
using regex_type = automaton::Regex<alphabet_type>;

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
	//TODO: pin cores

	unsigned int inflight = 0;
	Registry::index_type connectWatermark = 0;
	//Initialization phase: issue until workers are busy, retiring only if necessary
	auto in_init = [&](){return inflight < 3*std::thread::hardware_concurrency();};
	while (in_init()) {
		if (registry.waiting_size() == 0 && inflight == 0) {
			std::cout << "exited during initialization after " << registry.registered_size() << " registrations" << std::endl;
			std::exit(0);
		}

		while (in_init() && registry.waiting_size()) {
			Registry::index_type i = registry.register_next();
			issue.put(Combine(i, retire));
			++inflight;
		}
		if (in_init() && connectWatermark != registry.registered_size()) {
			issue.put(Connect(connectWatermark, registry.registered_size(), retire));
			connectWatermark = registry.registered_size();
			++inflight;
		}
		if (in_init()) {
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
		}
	}

	//Steady state: alternate issuance and retirement, periodically issuing a connect task
	while (registry.waiting_size() || connectWatermark != registry.registered_size() || inflight) {
		if ((connectWatermark + 100) < registry.registered_size() || !registry.waiting_size()) {
			//TODO: this code duplicated from above -- use a lambda or function to deduplicate
			issue.put(Connect(connectWatermark, registry.registered_size(), retire));
			connectWatermark = registry.registered_size();
			++inflight;
		} else if (registry.waiting_size()) {
			Registry::index_type i = registry.register_next();
			//TODO: ignore uncombinable
			issue.put(Combine(i, retire));
			++inflight;
		}

		if (inflight) {
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
		}
	}
	std::cout << "exiting after " << registry.registered_size() << " registrations" << std::endl;
	std::exit(0);
}

int main(int argc, char* argv[]) {
	using R = regex_type;
//	automaton_type split = R::star(R::alt({
//			R::cat({R::lit(0), R::alt({R::lit(1), R::lit(2)})}),
//			R::cat({R::lit(1), R::alt({R::lit(0), R::lit(2)})}),
//			R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1)})})})).compile();
	automaton_type split = R::star(R::alt({
			R::cat({R::lit(0), R::alt({R::lit(0), R::lit(1), R::lit(2)})}),
			R::cat({R::lit(1), R::alt({R::lit(0), R::lit(1), R::lit(2)})}),
			R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1), R::lit(2)})})})).compile();
	split.minimize();
	canonicalize(split, 3);
//	std::cout << split << std::endl;
	auto splithash = std::hash<automaton_type>()(split);
	registry.offer(Gadget(std::move(split), 3), Provenance(0), splithash);

//	automaton_ptr split4 = R::star(R::alt({
//			R::cat({R::lit(0), R::alt({R::lit(1), R::lit(2), R::lit(3)})}),
//			R::cat({R::lit(1), R::alt({R::lit(0), R::lit(2), R::lit(3)})}),
//			R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1), R::lit(3)})}),
//			R::cat({R::lit(3), R::alt({R::lit(0), R::lit(1), R::lit(2)})})})).compile();
//	automaton_ptr split4 = R::star(R::alt({
//			R::cat({R::lit(0), R::alt({R::lit(0), R::lit(1), R::lit(2), R::lit(3)})}),
//			R::cat({R::lit(1), R::alt({R::lit(0), R::lit(1), R::lit(2), R::lit(3)})}),
//			R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1), R::lit(2), R::lit(3)})}),
//			R::cat({R::lit(3), R::alt({R::lit(0), R::lit(1), R::lit(2), R::lit(3)})})})).compile();
//	split4->minimize();
//	canonicalize(split4, 4);
//	std::cout << *split4 << std::endl;
//	while (true) {
//		mainloop(*split4);
//	}

	R noopR = R::star(R::alt({R::cat({R::lit(0), R::lit(0)}), R::cat({R::lit(1), R::lit(1)}), R::cat({R::lit(2), R::lit(2)}), R::cat({R::lit(3), R::lit(3)})}));
	automaton_type noop = noopR.compile();
	R ltr = R::alt({R::cat({R::lit(0), R::lit(1)}), R::cat({R::lit(3), R::lit(2)})});
	R rtl = R::alt({R::cat({R::lit(1), R::lit(0)}), R::cat({R::lit(2), R::lit(3)})});
	automaton_type parallelToggleBase = R::alt({R::epsilon(), ltr, R::star(R::cat({ltr, rtl})), R::cat({ltr, R::star(R::cat({rtl, ltr}))})}).compile();
	automaton_type parallelToggle = shuffleAccept(noop, parallelToggleBase);
	acceptingClosure(parallelToggle, 4);
	parallelToggle.minimize();
	canonicalize(parallelToggle, 4);
	auto parallelToggleHash = std::hash<automaton_type>()(parallelToggle);
	registry.offer(Gadget(std::move(parallelToggle), 4), Provenance(1), parallelToggleHash);

	//TODO: declare well-known gadgets as constants (maybe functions to create them?)
	//and test they do the right thing (may require teaching build script about
	//sub-project tests...)
	ltr = R::alt({R::cat({R::lit(0), R::lit(1)}), R::cat({R::lit(2), R::lit(3)})});
	rtl = R::alt({R::cat({R::lit(1), R::lit(0)}), R::cat({R::lit(3), R::lit(2)})});
	automaton_type antiparallelToggle = shuffleAccept(noop, R::alt({R::epsilon(), ltr, R::star(R::cat({ltr, rtl})), R::cat({ltr, R::star(R::cat({rtl, ltr}))})}).compile());
	acceptingClosure(antiparallelToggle, 4);
	antiparallelToggle.minimize();
	canonicalize(antiparallelToggle, 4);


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

//int main(int argc, char* argv[]) {
//	using R = regex_type;
//	automaton_type split = R::star(R::alt({
//		R::cat({R::lit(0), R::alt({R::lit(1), R::lit(2)})}),
//		R::cat({R::lit(1), R::alt({R::lit(0), R::lit(2)})}),
//		R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1)})})})).compile();
//	split.minimize();
//	canonicalize(split, 3);
//	std::cout << split << std::endl;
//	//TODO: provenance for initial gadgets
//	registry.offer(Gadget(std::move(split), 3), Provenance(100000, 0));
//	registry.register_next();
//
//	std::vector<std::pair<Gadget, Provenance>> successors;
//	combine(0, 0, std::back_inserter(successors));
//	bool offered = registry.offer(successors.front().first, successors.front().second);
//	std::cout << offered << std::endl;
//	std::cout << *successors.front().first.a_ << std::endl;
//	successors.clear();
//	registry.register_next();
//
//	connect(1, std::back_inserter(successors));
//	for (auto& p : successors)
//		std::cout << *p.first.a_ << std::endl;
//	std::cout << "ASDFASDF" << std::endl;
//	offered = registry.offer(successors[2].first, successors[2].second);
//	std::cout << offered << std::endl;
//	std::cout << *successors[2].first.a_ << std::endl;
//	successors.clear();
//	registry.register_next();
//
//	connect(2, std::back_inserter(successors));
//	offered = registry.offer(successors.front().first, successors.front().second);
//	std::cout << offered << std::endl;
//	std::cout << *successors.front().first.a_ << std::endl;
//	successors.clear();
//	registry.register_next();
//
//	automaton_type split4 = R::star(R::alt({
//		R::cat({R::lit(0), R::alt({R::lit(1), R::lit(2), R::lit(3)})}),
//		R::cat({R::lit(1), R::alt({R::lit(0), R::lit(2), R::lit(3)})}),
//		R::cat({R::lit(2), R::alt({R::lit(0), R::lit(1), R::lit(3)})}),
//		R::cat({R::lit(3), R::alt({R::lit(0), R::lit(1), R::lit(2)})})})).compile();
//	split4.minimize();
//	canonicalize(split4, 4);
//	std::cout << split4 << std::endl;
//}