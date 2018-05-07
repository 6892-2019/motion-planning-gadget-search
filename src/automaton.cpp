#include "precompiled.hpp"
#include "automaton.hpp"
#include "packedautomaton.hpp"

using namespace automaton;

namespace {
bool compare_working(const WorkingAutomaton& left, const WorkingAutomaton& right) {
	assert(left.alphabet_size() == right.alphabet_size()); //should already have been checked in the caller
	switch (left.alphabet_size()) {
#define COMPARE_WORKING_CASE(N) case N: return static_cast<const Automaton<N>&>(left) == static_cast<const Automaton<N>&>(right);
		COMPARE_WORKING_CASE(1)
		COMPARE_WORKING_CASE(2)
		COMPARE_WORKING_CASE(3)
		COMPARE_WORKING_CASE(4)
		COMPARE_WORKING_CASE(5)
		COMPARE_WORKING_CASE(6)
		COMPARE_WORKING_CASE(7)
		COMPARE_WORKING_CASE(8)
		COMPARE_WORKING_CASE(9)
		COMPARE_WORKING_CASE(10)
		COMPARE_WORKING_CASE(11)
		COMPARE_WORKING_CASE(12)
		COMPARE_WORKING_CASE(13)
		COMPARE_WORKING_CASE(14)
		COMPARE_WORKING_CASE(15)
		COMPARE_WORKING_CASE(16)
#undef COMPARE_WORKING_CASE
	default:
		std::cout << "unhandled compare_working: " << typeid(left).name() << ", " << typeid(right).name();
		std::terminate();
	}
}

bool compare_slowpath(const AutomatonBase& left, const AutomatonBase& right) {
	//TODO: these are only useful optimizations if both left and right override
	//their default implementations; otherwise we're just wasting time
	if (left.accept_size() != right.accept_size()) return false;
	if (left.edge_size() != right.edge_size()) return false;
	if (left.transition_size() != right.transition_size()) return false;

	auto alphabet = xrange(left.alphabet_size());
	for (AutomatonBase::state_type s : xrange(left.state_size())) {
		if (left.accept(s) != right.accept(s)) return false;
		for (AutomatonBase::symbol_type a : alphabet) {
			StateSet ld = left.step(s, a), rd = right.step(s, a);
			ld.sort();
			rd.sort();
			if (ld != rd) return false;
		}
	}
	return true;
}
} //anonymous namespace



namespace automaton {

bool operator==(const AutomatonBase& left, const AutomatonBase& right) {
	if (left.alphabet_size() != right.alphabet_size()) return false;
	if (left.state_size() != right.state_size()) return false;

	if (auto l = dynamic_cast<const WorkingAutomaton*>(&left),
			r = dynamic_cast<const WorkingAutomaton*>(&right); l && r)
		return compare_working(*l, *r);
	if (auto l = dynamic_cast<const PackedAutomaton*>(&left),
			r = dynamic_cast<const PackedAutomaton*>(&right); l && r)
		return *l == *r;

	return compare_slowpath(left, right);
}

WorkingAutomaton::WorkingAutomaton() = default;
WorkingAutomaton::~WorkingAutomaton() = default;
WorkingAutomaton::WorkingAutomaton(const WorkingAutomaton&) = default;
WorkingAutomaton::WorkingAutomaton(WorkingAutomaton&&) = default;
WorkingAutomaton& WorkingAutomaton::operator=(const WorkingAutomaton&) = default;
WorkingAutomaton& WorkingAutomaton::operator=(WorkingAutomaton&&) = default;

bool WorkingAutomaton::addTrans(state_type from, SymbolSet on, state_type to) {
	return std::any_of(on.begin(), on.end(), [&](symbol_type s){return this->addTrans(from, s, to);});
}

auto WorkingAutomaton::append(const AutomatonBase& b) -> state_type {
	state_type base = state_size(), theirs = b.state_size();
	reserve(base + theirs);
	for (state_type i : xrange(theirs)) {
		addState();
		setAccept(base + i, b.accept(i));
	}
	b.for_each_transition([&](state_type from, symbol_type on, state_type to) {
		addTrans(from + base, on, to + base);
	});
	assert(b.state_size() == 0 || (!minimal() && !canonical()));
	return base;
}

bool WorkingAutomaton::infinite() {
	removeDeadStates();
	//If all states are live, we need only check for a cycle.
	//We could use a dynamic_bitset or a simple byte array here instead (or
	//a specialized 0..n set, if there's such a type).
	google::dense_hash_set<state_type> visited(state_size());
	visited.set_empty_key(state_size());
	std::vector<state_type> path;
	circular_deque<std::optional<state_type>, 16> nexts;

	nexts.push_back(0U);
	while (!nexts.empty()) {
		std::optional<state_type> n = nexts.pop_back();
		if (n) {
			path.push_back(*n);
			nexts.push_back(std::nullopt);
			for (state_type next : destinations(path.back())) {
				//We might prefer a set; we could avoid storing path itself
				//if we store the to-be-popped element in place of the empty optional.
				if (std::find(path.begin(), path.end(), next) != path.end())
					return true;
				if (visited.find(next) == visited.end())
					nexts.push_back(next);
			}
		} else {
			visited.insert(path.back());
			path.pop_back();
		}
	}
	return false;
}

namespace detail {
google::dense_hash_set<state_type> live_states(const AutomatonBase& a) {
	const state_type state_size = a.state_size();
	//This set will be used as the visited set for the forward search, then
	//reused as the live set for the backward search.
	google::dense_hash_set<state_type> live(state_size);
	live.set_empty_key(std::numeric_limits<state_type>::max());

	//TODO: there's not yet a good way to use std::sort on separate vectors
	//See http://stackoverflow.com/q/13840998/3614835.
	std::vector<std::pair<state_type, state_type>> inverseEdgelist;
	//Exact sizing (counting transitions) is probably not worth it, but we
	//know there's at least this much.
	inverseEdgelist.reserve(state_size);

	circular_deque<state_type, 16> nexts;
	nexts.push_back(0);
	live.insert(0);
	while (!nexts.empty()) {
		state_type n = nexts.pop_back();
		a.for_each_destination(n, [&](state_type next) {
			inverseEdgelist.push_back({next, n});
			if (live.insert(next).second)
				nexts.push_back(next);
		});
	}

	//At this point, inverseEdgelist is complete for all reachable states.
	std::sort(inverseEdgelist.begin(), inverseEdgelist.end());
	std::vector<state_type> inverse;
	inverse.reserve(inverseEdgelist.size());
	std::transform(inverseEdgelist.begin(), inverseEdgelist.end(),
		std::back_inserter(inverse), [](const auto& p){return p.second;});
	std::vector<decltype(inverse)::iterator> inverseIdx;
	inverseIdx.reserve(state_size + 1);
	inverseIdx.push_back(inverse.begin());
	decltype(inverseEdgelist)::iterator edgelistPos = inverseEdgelist.begin();
	for (state_type s = 0; s < state_size; ++s) {
		edgelistPos = std::find_if_not(edgelistPos, inverseEdgelist.end(), [s](auto& p){return p.first == s;});
		inverseIdx.push_back(inverse.begin() + std::distance(inverseEdgelist.begin(), edgelistPos));
	}
	inverseEdgelist.clear();
	inverseEdgelist.shrink_to_fit();

	auto inverseDestinations = [&](state_type s) {
		assert(s < inverseIdx.size());
		return boost::make_iterator_range(inverseIdx[s], inverseIdx[s+1]);
	};

	//We want to initialize the live set to the reachable accept states, but
	//we can't clear it until we've decided which accept states are reachable.
	//We'll iterate once noting any unreachable accept states, then make
	//another pass to initialize the live set.  (It's tempting to just clear
	//the accept flag for those states, but we can't modify the automaton here.)
	google::dense_hash_set<state_type> unreachableAccepts;
	unreachableAccepts.set_empty_key(std::numeric_limits<state_type>::max());
	a.for_each_accept([&](state_type state) {
		if (!live.count(state))
			unreachableAccepts.insert(state);
	});
	live.clear_no_resize();
	assert(nexts.empty());
	a.for_each_accept([&](state_type state) {
		if (!unreachableAccepts.count(state)) {
			live.insert(state);
			nexts.push_back(state);
		}
	});
	while (!nexts.empty()) {
		state_type n = nexts.pop_back();
		for (state_type next : inverseDestinations(n))
			if (live.insert(next).second)
				nexts.push_back(next);
	}
	return live;
}

std::pair<dynarray<state_type>, dynarray<state_type>> find_dead_state_renumbering(const AutomatonBase& a, const decltype(live_states(a))& live) {
	//If any states are live, the initial state must be one of them.
	assert(live.count(0) == 1);
	dynarray<state_type> survivorFrom(live.size()), remap(a.state_size());
	//some states don't get mapped anywhere; allow distinguishing this
	std::fill(remap.begin(), remap.end(), std::numeric_limits<state_type>::max());
	boost::dynamic_bitset<std::size_t> newnumbers(live.size());
	newnumbers.set();
	for (state_type s : live)
		//live states keep their numbers if possible
		if (s < live.size()) {
			survivorFrom[s] = s;
			remap[s] = s;
			newnumbers.reset(s);
		}
	unsigned long freestart = 0;
	for (state_type s : live)
		if (s >= live.size()) {
			freestart = newnumbers.find_next(freestart);
			state_type dest = static_cast<state_type>(freestart);
			survivorFrom[dest] = s;
			remap[s] = dest;
			newnumbers.reset(freestart);
		}
	return {std::move(survivorFrom), std::move(remap)};
}

void determinize_into(const AutomatonBase& source, AutomatonBase& target) {
	assert(source.alphabet_size() == target.alphabet_size());
	assert(target.state_size() == 0);
	const state_type state_size = source.state_size();
	const symbol_type alphabet_size = source.alphabet_size();

	const int DETERMINIZE_PAGE_SIZE = 4096, DETERMINIZE_PAGE_UNITS = DETERMINIZE_PAGE_SIZE/sizeof(state_type);
	//manages page lifetime: free them all at the end
	boost::container::small_vector<std::unique_ptr<state_type, free_deleter>, 8> page_handles;
	page_handles.emplace_back(static_cast<state_type*>(std::malloc(DETERMINIZE_PAGE_SIZE)));
	//points to the start of the current set
	state_type* alloc_next = page_handles.front().get();
	//points past the end of the current set (next append slot)
	state_type* alloc_cur = alloc_next;
	//points to the end of the current page
	state_type* alloc_page_end = page_handles.front().get()+DETERMINIZE_PAGE_UNITS;
	auto set_append = [&](state_type state) {
		if (alloc_cur == alloc_page_end) {
			if (alloc_next == page_handles.back().get()) {
				//TODO: realloc this set into a double-sized page (then skip the below new-page alloc)
				std::cout << "set size of full page\n";
				std::terminate();
			}
			//TODO: if we're allocating lots of pages, may want to start doubling size
			page_handles.emplace_back(static_cast<state_type*>(std::malloc(DETERMINIZE_PAGE_SIZE)));
			//copy the current partial set into the new page
			state_type* next_next = page_handles.back().get();
			state_type* next_cur = std::copy(alloc_next, alloc_cur, next_next);
			alloc_next = next_next;
			alloc_cur = next_cur;
			alloc_page_end = alloc_next + DETERMINIZE_PAGE_UNITS;
		}
		++(*alloc_next); //increment size first
		//If we're appending the 0 size of a new set, this write clobbers
		//the previous increment (on purpose).
		*alloc_cur++ = state;
	};
	set_append(0); //each set begins with a size
	auto commit_set = [&]() -> state_type* {
		state_type* set_start = alloc_next;
		alloc_next = alloc_cur;
		set_append(0); //size of the next set
		return set_start;
	};
	auto clear_set = [&]() {
		*alloc_next = 0; //reset the size
		alloc_cur = alloc_next+1;
	};

	auto set_begin = [](state_type* set) {
		return set+1;
	};
	auto set_end = [&](state_type* set) {
		return set_begin(set) + *set;
	};
	auto set_size = [&](state_type* set) {
		assert(*set == numeric_cast<state_type>(set_end(set) - set_begin(set)));
		return *set;
	};

	auto set_equal = [&](state_type* left, state_type* right) {
		//TODO: this could be *left == *right && !memcmp(left, right, *left + 1);
		//^we have to check the size first so we don't read off the end of a page
		return std::equal(set_begin(left), set_end(left), set_begin(right), set_end(right));
	};
	auto set_hash = [](state_type* set) {
		//this includes the size of the set as the first element of the hash
		return farmhash::Hash(reinterpret_cast<char*>(set), (*set + 1) * sizeof(*set));
	};
	state_type empty_set = 0; //a "set" with just a size 0; empty sets represent crashes, so we'll never insert one
	google::dense_hash_map<state_type*, state_type,
			decltype(std::ref(set_hash)), decltype(std::ref(set_equal))> newstate(state_size,
			std::ref(set_hash), std::ref(set_equal));
	newstate.set_empty_key(&empty_set);
	circular_deque<std::pair<state_type*, state_type>, 16> worklist;

	target.reserve(state_size); //a reasonable lower bound for connected automata
	target.addState();
	set_append(0);
	state_type* first_set = commit_set();
	newstate.insert({first_set, 0});
	worklist.push_back({first_set, 0});

	while (!worklist.empty()) {
		auto [current_set, current_state] = worklist.pop_back();
		target.setAccept(current_state, std::any_of(set_begin(current_set), set_end(current_set),
				[&source](state_type s) {return source.accept(s);}));

		//TODO: it may be better to build one set per symbol in parallel here,
		//so we can use for_each_transition, avoiding repeated scans over the edges
		for (symbol_type s = 0; s < alphabet_size; ++s) {
			for (state_type f : make_range_for_pair(set_begin(current_set), set_end(current_set)))
				for (state_type t : source.step(f, s))
					set_append(t);
			std::sort(set_begin(alloc_next), set_end(alloc_next));
			state_type* new_set_end = std::unique(set_begin(alloc_next), set_end(alloc_next));
			*alloc_next = numeric_cast<state_type>(new_set_end - set_begin(alloc_next));
			alloc_cur = new_set_end;
			if (!set_size(alloc_next))
				continue; //all NFA states crashed
			auto it = newstate.find(alloc_next);
			if (it == newstate.end()) {
				it = newstate.insert({commit_set(), target.addState()}).first;
				worklist.push_back(*it);
			} else
				clear_set();
			target.addTrans(current_state, s, it->second);
			assert(target.deterministic());
		}
	}
}

struct Tarjan {
	Tarjan(const AutomatonBase& a_) : a(a_), state_size(a.state_size()), number(state_size) {
		std::fill(number.begin(), number.end(), std::numeric_limits<state_type>::max());
		result.components_.reserve(state_size);
	}
	const AutomatonBase& a;
	state_type state_size;
	unsigned int index;
	std::vector<state_type> stack;
	dynarray<state_type> number;
	SCCs result;

	state_type strongconnect(state_type v) {
		state_type lowlink = index++;
		number[v] = lowlink;
		stack.push_back(v);
		a.for_each_destination(v, [&](state_type w) {
			if (number[w] == std::numeric_limits<state_type>::max()) {
				state_type otherlowlink = strongconnect(w);
				//TODO: apparently lowlink is only used in strongconnect(v), so
				//we could make it the return value instead of an array
				lowlink = std::min(lowlink, otherlowlink);
			} else if (number[w] < number[v] && std::find(stack.begin(), stack.end(), w) != stack.end())
				lowlink = std::min(lowlink, number[w]);
		});
		if (lowlink == number[v]) {
			result.indices_.push_back(static_cast<unsigned int>(result.components_.size()));
			while (!stack.empty() && number[stack.back()] >= number[v]) {
				result.components_.push_back(stack.back());
				stack.pop_back();
			}
		}
		return lowlink;
	}
	SCCs compute() {
		for (state_type w = 0; w < state_size; ++w)
			if (number[w] == std::numeric_limits<state_type>::max())
				strongconnect(w);
		result.indices_.push_back(static_cast<unsigned int>(result.components_.size()));

		for (state_type s = 0; s < state_size; ++s)
			//Each state is in exactly one component.
			assert(std::count(result.components_.begin(), result.components_.end(), s) == 1);

		return std::move(result);
	}
};
} //namespace detail

SCCs find_components(const AutomatonBase& a) {
	return detail::Tarjan(a).compute();
}

std::unique_ptr<WorkingAutomaton> make_working(unsigned int size) {
	switch(size) {
#define MAKE_WORKING_CASE(I) case I: return std::make_unique<Automaton<I>>();
		MAKE_WORKING_CASE(1)
		MAKE_WORKING_CASE(2)
		MAKE_WORKING_CASE(3)
		MAKE_WORKING_CASE(4)
		MAKE_WORKING_CASE(5)
		MAKE_WORKING_CASE(6)
		MAKE_WORKING_CASE(7)
		MAKE_WORKING_CASE(8)
		MAKE_WORKING_CASE(9)
		MAKE_WORKING_CASE(10)
		MAKE_WORKING_CASE(11)
		MAKE_WORKING_CASE(12)
		MAKE_WORKING_CASE(13)
		MAKE_WORKING_CASE(14)
		MAKE_WORKING_CASE(15)
		MAKE_WORKING_CASE(16)
//		MAKE_WORKING_CASE(17)
//		MAKE_WORKING_CASE(18)
//		MAKE_WORKING_CASE(19)
//		MAKE_WORKING_CASE(20)
//		MAKE_WORKING_CASE(21)
//		MAKE_WORKING_CASE(22)
//		MAKE_WORKING_CASE(23)
//		MAKE_WORKING_CASE(24)
//		MAKE_WORKING_CASE(25)
//		MAKE_WORKING_CASE(26)
//		MAKE_WORKING_CASE(27)
//		MAKE_WORKING_CASE(28)
//		MAKE_WORKING_CASE(29)
//		MAKE_WORKING_CASE(30)
//		MAKE_WORKING_CASE(31)
//		MAKE_WORKING_CASE(32)
#undef MAKE_WORKING_CASE
	default:
		std::cout << "bad make_working size: " << size << "\n" << std::flush;
		std::abort();
	}
}

} //namespace automaton