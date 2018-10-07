#include "precompiled.hpp"
#include "automaton.hpp"
#include "automaton-io.hpp"
#include "hopscotch/hopscotch_map.h"
#include <sparsehash/dense_hash_set>
#include <jemalloc/jemalloc.h>

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

	return compare_slowpath(left, right);
}

WorkingAutomaton::WorkingAutomaton() = default;
WorkingAutomaton::~WorkingAutomaton() = default;
WorkingAutomaton::WorkingAutomaton(const WorkingAutomaton&) = default;
WorkingAutomaton::WorkingAutomaton(WorkingAutomaton&&) = default;
WorkingAutomaton& WorkingAutomaton::operator=(const WorkingAutomaton&) = default;
WorkingAutomaton& WorkingAutomaton::operator=(WorkingAutomaton&&) = default;

bool WorkingAutomaton::addTrans(state_type from, SymbolSet on, state_type to) {
	//std::all_of short-circuits and std::accumulate takes binary ops, so
	//we'll just use the loop.
	bool changed = false;
	for (symbol_type s : on)
		changed |= addTrans(from, s, to);
	return changed;
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

void removeDeadStates(ExplodedAutomaton& a) {
	assert(!a.accept.empty());
	//Instead of
	//using a hash set or bitset to track liveness, we just go ahead and build
	//the renumbering during our backward search.  (This does have the downside
	//of using extra memory; if we stored a bitset, then we could reuse the
	//inverse index after the search finishes to store the renumbering.)
	dynarray<state_type> renumbering(a.state_size);
	std::fill(renumbering.begin(), renumbering.end(), std::numeric_limits<state_type>::max());
	state_type newNumber = 0;
	circular_deque<state_type, 16> nexts;
	nexts.reserve(static_cast<decltype(nexts)::size_type>(a.accept.size()));
	//We have to renumber 0 to 0, but 0 may not be an accept state.  Remember
	//which state we're renumbering to 0 and swap when we're done.
	state_type assignedFirst = a.accept.front();
	for (auto state : a.accept) {
		renumbering[state] = newNumber++;
		nexts.push_back(state);
	}

	std::sort(a.edges.begin(), a.edges.end(), [](const Edge& l, const Edge& r){return l.target < r.target;});
	dynarray<state_type> inverseIndex(a.state_size+1);
	auto indexBuildingPos = a.edges.begin();
	inverseIndex[0] = 0;
	for (state_type s = 0; s < a.state_size; ++s) {
		indexBuildingPos = std::find_if_not(indexBuildingPos, a.edges.end(), [s](const Edge& e){return e.target == s;});
		inverseIndex[s+1] = numeric_cast<state_type>(std::distance(a.edges.begin(), indexBuildingPos));
	}
	while (!nexts.empty() && newNumber < a.state_size) {
		state_type n = nexts.pop_back();
		for (auto start = inverseIndex[n], end = inverseIndex[n+1]; start < end; ++start) {
			state_type source = a.edges[start].source;
			if (renumbering[source] == std::numeric_limits<state_type>::max()) {
				renumbering[source] = newNumber++;
				nexts.push_back(source);
			}
		}
	}

	if (newNumber != a.state_size) {
		//Ensure we renumber 0 to 0.  (If the automaton has any reachable accept
		//states, 0 is live, so it always has a number here.)
		assert(renumbering[assignedFirst] == 0);
		std::swap(renumbering[0], renumbering[assignedFirst]);
		renumber(a, renumbering);
		a.state_size = newNumber;
	}
}

void renumber(ExplodedAutomaton& a, const dynarray<state_type>& numbering) {
	constexpr state_type dead = std::numeric_limits<state_type>::max();
	//A hybrid of std::partition and std::for_each: we'll swap dead edges
	//toward the end and transform live edges.
	auto edgesEnd = a.edges.end();
	for (auto i = a.edges.begin(); i != edgesEnd; ++i) {
		state_type newSource = numbering[i->source], newTarget = numbering[i->target];
		//If this shows up in the profile, consider making this a bitwise or and
		//just a single branch.  (two loads/one branch vs. two-ish loads/two branches)
		if (newSource == dead || newTarget == dead) {
			//scan backwards to find a live state
			while (--edgesEnd != i && (numbering[edgesEnd->source] == dead || numbering[edgesEnd->target] == dead));
			if (edgesEnd == i) break;
			std::iter_swap(i, edgesEnd);
			//i changed so we have to reload these
			newSource = numbering[i->source];
			newTarget = numbering[i->target];
		}
		i->source = newSource;
		i->target = newTarget;
	}
	a.edges.erase(edgesEnd, a.edges.end());
	auto acceptEnd = a.accept.end();
	for (auto i = a.accept.begin(); i != acceptEnd; ++i) {
		state_type newNumber = numbering[*i];
		if (newNumber == dead) {
			while (--acceptEnd != i && numbering[*acceptEnd] == dead);
			if (acceptEnd == i)
				break;
			std::iter_swap(i, acceptEnd);
			newNumber = numbering[*i];
		}
		*i = newNumber;
	}
	a.accept.erase(acceptEnd, a.accept.end());
}

namespace {
template<class Renumbering>
void implode(AutomatonBase& dest, const ExplodedAutomaton& source, Renumbering&& map) {
	assert(dest.alphabet_size() == source.alphabet_size);
	//We don't clear() ourselves because that would set deterministic_, then
	//keep it updated on every addTrans.  We'd prefer to let the caller clear
	//and/or reset it (if so privileged).
	assert(dest.state_size() == 0);

	dest.reserve(source.state_size);
	for (state_type i = 0; i < source.state_size; ++i)
		dest.addState();
	for (Edge e : source.edges)
		dest.addTrans(map[e.source], e.symbol, map[e.target]);
	for (state_type accepting : source.accept)
		dest.setAccept(map[accepting]);
}
}

void implode(AutomatonBase& dest, const ExplodedAutomaton& source) {
	implode(dest, source, identity_permutation());
}

void implodeRenumber(AutomatonBase& dest, const ExplodedAutomaton& source, const dynarray<state_type>& renumbering) {
	implode(dest, source, renumbering);
}

//A fastpath for automata with 64 or fewer states, based on bitsets stored
//directly in the hash map.
template<class AddStateAction, class AddTransAction>
void determinize_64_fastpath(const AutomatonBase& source, AddStateAction addState, AddTransAction addTrans) {
	const state_type state_size = source.state_size();
	const symbol_type alphabet_size = source.alphabet_size();
	using state_bitset = automaton::bitset<64>;
	//TODO: maybe use a pmr allocator
	tsl::hopscotch_map<state_bitset, state_type> newstate(state_size);
	circular_deque<std::pair<state_bitset, state_type>, 16> worklist;
	//TODO: can pmr this as well, if we pmr dynarray
	dynarray<state_bitset> nexts(alphabet_size);

	//Insert the initial state.
	state_bitset initial_only;
	initial_only.set(0);
	newstate.insert({initial_only, 0});
	worklist.push_back({initial_only, 0});
	addState(source.accept(0));
	state_type newStates = 1;

	while (!worklist.empty()) {
		auto [current_set, current_state] = worklist.pop_back();
		for (state_type f = current_set.find_first(); f < current_set.size(); f = current_set.find_next(f))
			source.for_each_transition(f, [&nexts](symbol_type symbol, state_type dest) {
				nexts[symbol].set(dest);
			});
		for (symbol_type s = 0; s < alphabet_size; ++s) {
			state_bitset& next = nexts[s];
			if (next.none())
				continue; //all NFA states crashed
			auto [it, inserted] = newstate.insert({next, newStates});
			if (inserted) {
				++newStates;
				bool accepting = false;
				for (auto q = next.find_first(); !accepting && q < next.size(); q = next.find_next(q))
					accepting |= source.accept(q);
				addState(accepting);
				worklist.push_back(*it);
			}
			next.reset();
			addTrans(current_state, s, it->second);
		}
	}
}

//The only complete/good implementation of monotonic_buffer_resource is in
//Boost.Container, and it has a clownshoes problem due to block_slist.  Instead
//we'll write our own "resource", which isn't actually a memory_resource, but
//easily could be if I wanted to use it with an old-style allocator (hopefully
//Boost.Container's allocator is good enough).
class monotonic_buffer_resource {
private:
	void* buf_;
	std::size_t remaining_, nextSize_;
	//We might do extra allocations here, but it's much easier to avoid freeing
	//future pointers and avoid clownshoes by keeping metadata separate (instead
	//of a singly-linked header list).
	boost::container::small_vector<std::unique_ptr<void, free_deleter>, 16> pages_;
public:
	//We don't bother with an upstream resource.
	explicit monotonic_buffer_resource(std::size_t initial_size) :
			buf_(nullptr), remaining_(0), nextSize_(initial_size) {}
	//This constructor takes an existing buffer and doesn't free it.  This might
	//be a std::array, for example.  We avoid freeing by just not remembering it.
	monotonic_buffer_resource(void* buffer, std::size_t buffer_size) :
			buf_(buffer), remaining_(buffer_size), nextSize_(buffer_size*2) {}

	void* allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
		return do_allocate(bytes, alignment);
	}
	void deallocate(void* p, std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
		return do_deallocate(p, bytes, alignment);
	}

	//extension
	template<typename T>
	T* allocate(std::size_t count, std::size_t alignment = alignof(T)) {
		std::size_t bytes = count*sizeof(T); //TODO: overflow
		return static_cast<T*>(allocate(bytes, alignment));
	}
protected: //private after a DR, but they'd be protected if we used Boost.Container's impls
	void* do_allocate(std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) {
		if (void* p = attempt(bytes, alignment))
			return p;

		nextSize_ = std::max(nextSize_, bytes);
#ifdef __SANITIZE_ADDRESS__
		std::size_t actual = nextSize_;
#else
		std::size_t actual = nallocx(nextSize_, 0);
#endif
		pages_.emplace_back(std::malloc(actual));
		buf_ = pages_.back().get();
		remaining_ = actual;
		nextSize_ = actual * 2; //TODO: tune growth factor

		void* p = attempt(bytes, alignment);
		assert(p && "we just made space for this");
		return p;
	}
	void do_deallocate(void*, std::size_t, std::size_t) {}
private:
	void* attempt(std::size_t bytes, std::size_t alignment) {
		void* r = std::align(alignment, bytes, buf_, remaining_);
		if (r) {
			//Why does std::align use void* anyway?
			buf_ = static_cast<void*>(static_cast<std::byte*>(r) + bytes);
			remaining_ -= bytes;
		}
		return r; //nullptr if we failed
	}
};

class vectorish {
	constexpr static std::size_t minimum_alloc_size = 16;
	monotonic_buffer_resource& alloc_;
	//begin_ points 1 ahead of the start of the memory block to allow space for
	//the size in the data()/raw representation.
	state_type* begin_;
	state_type* end_;
	state_type* capacity_;
public:
	vectorish(monotonic_buffer_resource& alloc) : alloc_(alloc) {
		state_type* p = alloc_.allocate<state_type>(minimum_alloc_size);
		begin_ = p + 1; //leave space for size in data() representation
		end_ = begin_;
		capacity_ = p + minimum_alloc_size;
	}
	state_type size() {
		return static_cast<state_type>(end_ - begin_);
	}
	bool empty() {
		return size() == 0;
	}
	state_type* begin() {
		return begin_;
	}
	state_type* end() {
		return end_;
	}
	state_type* data() {
		*(begin_ - 1) = size();
		return begin_ - 1;
	}
	void push_back(state_type state) {
		if (end_ == capacity_) {
			std::size_t cap = capacity_ - (begin_-1);
			std::size_t newcap = std::max(cap * 2, minimum_alloc_size);
			state_type* p = alloc_.allocate<state_type>(newcap);
			end_ = std::copy(begin(), end(), p+1);
			//Just futureproofing in case we change to, e.g., a pool resource.
			alloc_.deallocate(begin_ - 1, cap * sizeof(state_type));
			begin_ = p + 1;
			capacity_ = p + newcap;
		}
		*end_++ = state;
	}
	//for erase-unique idiom, that's it
	void eraseAfter(state_type* newEnd) {
		end_ = newEnd;
	}
	void clear() {
		end_ = begin_;
	}
	/**
	 * Stop managing the memory; create a new vector in any remaining capacity.
	 */
	void release() {
		assert((*(begin_-1) == size()) && "releasing without setting size; shouldn't you have called data() first?");
		if (end_ == capacity_) {
			state_type* p = alloc_.allocate<state_type>(minimum_alloc_size);
			begin_ = p + 1; //leave space for size in data() representation
			end_ = begin_;
			capacity_ = p + minimum_alloc_size;
		} else {
			begin_ = end_ + 1;
			end_ = begin_;
			//capacity_ unchanged
			//In particular, if there's just one unit of capacity left, we get a
			//0-capacity vector, but push_back handles that fine.
		}
	}
};

template<class AddStateAction, class AddTransAction>
void determinize(const AutomatonBase& source, AddStateAction addState, AddTransAction addTrans) {
	if (source.state_size() <= 64)
		return determinize_64_fastpath(source, addState, addTrans);
	const state_type state_size = source.state_size();
	const symbol_type alphabet_size = source.alphabet_size();
	const int DETERMINIZE_INITIAL_SIZE = 4096;
	monotonic_buffer_resource alloc(DETERMINIZE_INITIAL_SIZE);

	//A set is a sorted sequence of state_type, with a preceding state_type
	//storing the set's size.
	auto set_begin = [](state_type* set) {
		return set+1;
	};
	auto set_end = [&](state_type* set) {
		return set_begin(set) + *set;
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
	tsl::hopscotch_map<state_type*, state_type,
			decltype(std::ref(set_hash)), decltype(std::ref(set_equal)),
			std::allocator<std::pair<state_type*, state_type>>, //TODO: pmr?
			30, true /* store the hash */> newstate(state_size,
			std::ref(set_hash), std::ref(set_equal));
	circular_deque<std::pair<state_type*, state_type>, 16> worklist;

	//We have one open/pending set of next states per symbol.
	vectorish* nexts = alloc.allocate<vectorish>(alphabet_size);
	for (symbol_type i = 0; i < alphabet_size; ++i)
		::new (nexts+i) vectorish(alloc);

	//Insert the initial state.
	state_type newStates = 0;
	nexts[0].push_back(0);
	state_type* first_set = nexts[0].data();
	newstate.insert({first_set, newStates});
	worklist.push_back({first_set, newStates++});
	addState(source.accept(0));
	nexts[0].release();

	while (!worklist.empty()) {
		auto [current_set, current_state] = worklist.pop_back();
		for (state_type f : make_range_for_pair(set_begin(current_set), set_end(current_set)))
			source.for_each_transition(f, [nexts](symbol_type symbol, state_type dest) {
				nexts[symbol].push_back(dest);
			});
		for (symbol_type s = 0; s < alphabet_size; ++s) {
			vectorish& next = nexts[s];
			if (next.empty())
				continue; //all NFA states crashed
			std::sort(next.begin(), next.end());
			next.eraseAfter(std::unique(next.begin(), next.end()));
			auto [it, inserted] = newstate.insert({next.data(), newStates});
			if (inserted) {
				newStates++;
				bool accepting = std::any_of(next.begin(), next.end(),
						[&source](state_type state) {return source.accept(state);});
				addState(accepting);
				next.release();
				worklist.push_back(*it);
			} else
				next.clear();
			addTrans(current_state, s, it->second);
		}
	}
}

void determinize_into(const AutomatonBase& source, AutomatonBase& target) {
	assert(source.alphabet_size() == target.alphabet_size());
	assert(target.state_size() == 0);
	target.reserve(source.state_size()); //a reasonable lower bound for connected automata
	determinize(source, [&target](bool b){
			state_type newState = target.addState();
			if (b) target.setAccept(newState);
		}, [&target](auto from, auto on, auto to) {
			target.addTrans(from, on, to);
			assert(target.deterministic());
		});
}

ExplodedAutomaton determinize_explode(const AutomatonBase& source) {
	std::vector<Edge> edges;
	std::vector<state_type> accept;
	state_type stateSize = 0;
	determinize(source, [&accept, &stateSize](bool b) {
			if (b) accept.push_back(stateSize);
			++stateSize;
		},
		[&edges](auto from, auto on, auto to){
			edges.push_back({from, on, to});
		});
	return {edges, accept, stateSize, source.alphabet_size()};
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

namespace detail {
void do_lit(AutomatonBase& a, std::initializer_list<symbol_type> symbols) {
	a.reserve(static_cast<state_type>(symbols.size() + 1));
	state_type last = a.addState(), next;
	for (unsigned int symbol : symbols) {
		next = a.addState();
		a.addTrans(last, symbol, next);
		last = next;
	}
	a.setAccept(last);
}
}

} //namespace automaton