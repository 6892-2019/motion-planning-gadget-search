/*
 * File:   automaton.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 16, 2016, 4:36 PM
 */

#ifndef AUTOMATON_HPP
#define AUTOMATON_HPP

#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "bitset.hpp"

//uncomment the line below to enable debugging logging expressions
//#define AUTOMATON_DEBUG(expr) do {expr;} while(0);
#ifndef AUTOMATON_DEBUG
#define AUTOMATON_DEBUG(expr) do {} while(0);
#endif

namespace automaton {

template<unsigned int AlphabetSize>
class Automaton;

namespace detail {
using state_pair = std::pair<state_type, state_type>;
//We can't templatize this together with the other maps because dense_hash_map
//needs set_empty_key.
class DenseConjMap {
public:
	DenseConjMap(std::size_t leftSize, std::size_t rightSize) : map_() {
		//silent narrowing conversion: http://stackoverflow.com/q/37928951/3614835
		map_.set_empty_key({leftSize, rightSize});
	}
	void insert(state_pair oldstates, state_type newstate) {
		map_.insert({oldstates, newstate});
	}
	template<class Callable>
	std::pair<state_type, bool> compute_if_absent(state_pair oldstates, Callable newstateProvider) {
		//dense_hashtable::find_or_insert is so close to what we want :(
		auto it = map_.find(oldstates);
		if (it != map_.end())
			return {it->second, false};
		auto r = map_.insert({oldstates, newstateProvider()});
		return {r.first->second, true};
	}
private:
	//std::hash isn't provided for pair :(
	google::dense_hash_map<state_pair, state_type, boost::hash<state_pair>> map_;
};

template<class BackingMap>
class MapConjMap {
public:
	MapConjMap(std::size_t leftSize, std::size_t rightSize) : map_() {}
	void insert(std::pair<state_type, state_type> oldstates, state_type newstate) {
		map_.insert({oldstates, newstate});
	}
	template<class Callable>
	std::pair<state_type, bool> compute_if_absent(std::pair<state_type, state_type> oldstates, Callable newstateProvider) {
		auto it = map_.find(oldstates);
		if (it != map_.end())
			return {it->second, false};
		auto r = map_.insert({oldstates, newstateProvider()});
		return {r.first->second, true};
	}
private:
	BackingMap map_;
};
using UnorderedConjMap = MapConjMap<std::unordered_map<std::pair<state_type, state_type>,
		state_type, boost::hash<std::pair<state_type, state_type>>>>;
using SparseConjMap = MapConjMap<google::sparse_hash_map<std::pair<state_type, state_type>,
		state_type, boost::hash<std::pair<state_type, state_type>>>>;
/**
 * Implements a map of pairs as a vector of maps of elements.  On one hand,
 * our maps are smaller because each only has to store half the key; on the
 * other hand, the wasted space in one map can't be used by another, so our
 * total footprint may be larger.
 *
 * So far it doesn't seem to have worked out in either space or time, though
 * the latter might be fixed with a better hash function.
 */
//class DenseConjVectorOfMaps {
//public:
//	DenseConjVectorOfMaps(std::size_t leftSize, std::size_t rightSize) : maps_(std::min(leftSize, rightSize)), useLeft_(leftSize > rightSize) {
//		for (auto& map : maps_)
//			map.set_empty_key(std::numeric_limits<state_type>::max());
//	}
//	void insert(std::pair<state_type, state_type> oldstates, state_type newstate) {
//		if (useLeft_)
//			maps_[oldstates.second].insert({oldstates.first, newstate});
//		else
//			maps_[oldstates.first].insert({oldstates.second, newstate});
//	}
//	template<class Callable>
//	std::pair<state_type, bool> compute_if_absent(std::pair<state_type, state_type> oldstates, Callable newstateProvider) {
//		auto& map = useLeft_ ? maps_[oldstates.second] : maps_[oldstates.first];
//		state_type key = useLeft_ ? oldstates.first : oldstates.second;
//		auto it = map.find(key);
//		if (it != map.end())
//			return {it->second, false};
//		auto r = map.insert({key, newstateProvider()});
//		return {r.first->second, true};
//	}
//private:
//	struct MyHash {
//		std::size_t operator()(const state_type s) const {
//			return (s * s) + s;
////			return s ^ (s >> 8) ^ (s >> 16) ^ (s >> 24);
//		}
//	};
//	//both std::hash and boost::hash just return the state as its hash,
//	//which is very bad for dense_hash_map's power-of-two tables
//	std::vector<google::dense_hash_map<state_type, state_type, MyHash>> maps_;
//	bool useLeft_;
//};

template<typename T>
auto begin(const T& t) {
	using std::begin;
	return begin(t);
}
//work around array-pointer decay for half-ranges, where the length is irrelevant
template<typename T>
[[gnu::const]] //sure seems like GCC should figure this one out on its own
T* begin(T* ptr) {
	return ptr;
}

template<unsigned int N>
SymbolSet set_of_indices(automaton::bitset<N> mask) {
	SymbolSet set;
	set.reserve(mask.count());
	for (auto s = mask.find_first(); s < mask.size(); s = mask.find_next(s))
		set.insert_absent(s);
	return set;
}

template<typename Iter>
struct LazyEdgeEnumerator {
	const AutomatonBase* b;
	state_type state_size;
	symbol_type alphabet_size;
	dynarray<state_type> renumbering; //TODO: make this a view into a big array preinitialized to max()
	circular_deque<symbol_type, 16> queue;
	state_type idx = 0; //the order in which things are visited
	state_type cur; //the state currently being visited
	symbol_type a = 0; //the next symbol to be (after permutation) stepped with
	Iter alphabetPerm;
	bool shouldResume = false;
	LazyEdgeEnumerator(Iter perm, const AutomatonBase* automaton) : b(automaton),
			state_size(b->state_size()), alphabet_size(b->alphabet_size()),
			renumbering(state_size), alphabetPerm(perm) {
		std::fill(renumbering.begin(), renumbering.end(), std::numeric_limits<state_type>::max());
		renumbering[0] = idx++;
		queue.push_back(0);
	}
	std::tuple<state_type, state_type, symbol_type> operator()() {
		if (shouldResume)
			goto resume;
		while (!queue.empty()) {
			cur = queue.pop_front();
			for (a = 0; a < alphabet_size; ++a) {
				if (auto dest = b->stepDeterministic(cur, alphabetPerm[a])) {
					if (renumbering[*dest] == std::numeric_limits<state_type>::max()) {
						renumbering[*dest] = idx++;
						queue.push_back(*dest);
					}
					shouldResume = true;
					return {renumbering[cur], renumbering[*dest], a};
					//logically resume: goes here, but that would be an
					//error because dest is in scope, even if unused
				}
				resume: ;
			}
		}
		shouldResume = false;
		return {std::numeric_limits<state_type>::max(), std::numeric_limits<state_type>::max(), std::numeric_limits<symbol_type>::max()};
	}
	void runToCompletion() {
		if (shouldResume)
			goto resume;
		while (!queue.empty()) {
			cur = queue.pop_front();
			for (a = 0; a < alphabet_size; ++a) {
				if (auto dest = b->stepDeterministic(cur, alphabetPerm[a])) {
					if (renumbering[*dest] == std::numeric_limits<state_type>::max()) {
						renumbering[*dest] = idx++;
						queue.push_back(*dest);
					}
				}
				resume: ;
			}
		}
		assert(idx == state_size);
		assert(!std::count(renumbering.begin(), renumbering.end(), std::numeric_limits<state_type>::max()));
	}
	bool finished() const {
		//finished the outer loop and the inner loop
		return queue.empty() && a >= alphabet_size;
	}
};

//live_states is a reasonable public function, but we only use it when removing
//dead states, so we'll put it in detail for now
/**
 * Returns a set of the live states of this automaton.  A state is live iff
 * it is contained in a path from the initial state to an accept state.
 * @return the set of live states
 */
google::dense_hash_set<state_type> live_states(const AutomatonBase& a);

std::pair<dynarray<state_type>, dynarray<state_type>> find_dead_state_renumbering(const AutomatonBase& a,
		const decltype(live_states(a))& live);

void determinize_into(const AutomatonBase& source, AutomatonBase& target);

ExplodedAutomaton determinize_explode(const AutomatonBase& source);
void removeDeadStates(ExplodedAutomaton& a);
void renumber(ExplodedAutomaton& a, const dynarray<state_type>& numbering);
void implode(AutomatonBase& dest, const ExplodedAutomaton& source);
void implodeRenumber(AutomatonBase& dest, const ExplodedAutomaton& source, const dynarray<state_type>& renumbering);
} //namespace detail



template<unsigned int N> Automaton<N> empty();
template<unsigned int N> Automaton<N> all();
template<unsigned int N> Automaton<N> determinize(Automaton<N>);

template<unsigned int AlphabetSize>
class Automaton final : public WorkingAutomaton {
public:
	//awkward name to avoid clash with member function name
	//(virtual functions can't also be static constexpr)
	static constexpr unsigned int alphabet_size_v = AlphabetSize;
	using symbol_type = AutomatonBase::symbol_type;
	using symbol_mask_type = automaton::bitset<AlphabetSize>;
	using state_type = AutomatonBase::state_type;

	/**
	 * Constructs an Automaton with no states or transitions.
	 * TODO: many methods assume there's at least one state; this isn't wrong,
	 * but we should add assertions to make it explicit
	 */
	Automaton();
	Automaton(const AutomatonBase& a);
	~Automaton();
	Automaton(const Automaton& a);
	Automaton(Automaton&& a);
	std::unique_ptr<WorkingAutomaton> clone() const override;
	Automaton& operator=(const Automaton& a);
	Automaton& operator=(Automaton&& victim);

	state_type state_size() const override;
	state_type accept_size() const override;
	symbol_type alphabet_size() const override;
	[[gnu::pure]] symbol_type active_alphabet_size() const override;
	std::size_t edge_size() const override;
	std::size_t transition_size() const override;

	bool deterministic() const override;
	bool minimal() const override;
	bool canonical() const override;

	bool accept(state_type state) const override;

	void for_each_accept(std::function<void(state_type)> action) const override;

	StateSet step(state_type current, symbol_type symbol) const override;

	SymbolSet outgoing(state_type state) const override;

	/**
	 * Returns the states directly reachable from the given state.
	 * @return the states directly reachable from the given state
	 */
	StateSet destinations(state_type state) const override;

	std::optional<state_type> stepDeterministic(state_type current, symbol_type symbol) const override;

	void for_each_destination(state_type state, std::function<void(state_type)> action) const override;

	//TODO: templated overload of for_each_destination, for use when this
	//object's actual type is known (not via AutomatonBase)

	SymbolSet labels(state_type from, state_type to) const override;

	void for_each_transition(state_type state, std::function<void(symbol_type, state_type)> action) const override;

	void for_each_transition(std::function<void(state_type, symbol_type, state_type)> action) const override;

	void reserve(state_type state_capacity) override;

	state_type addState() override;

	bool addEpsilon(state_type from, state_type to) override;

	bool addTrans(state_type from, symbol_type symbol, state_type to) override;

	bool setAccept(state_type state, bool accepts = true) override;

	void clear() override;

	void shrink_to_fit();


private:
	//because just "using foo;" is illegal in class scopes, and we don't want to
	//pollute the namespace
	template<typename T, std::size_t N>
	using small_vector = boost::container::small_vector<T, N>;
private:
	template <class Map>
	static Automaton conj_impl(const Automaton& left, const Automaton& right) {
		//(left state, right state, new state)
		using state_triple = std::tuple<state_type, state_type, state_type>;
		circular_deque<state_triple, 16> worklist;
		Map newstates(left.state_size(), right.state_size());

		Automaton a;
		//TODO: are we sure?
		a.deterministic_ = left.deterministic() && right.deterministic();
		a.addState();
		//TODO: assuming 0 is the initial state
		worklist.push_back({0, 0, 0});
		newstates.insert({0, 0}, 0);

		while (!worklist.empty()) {
			state_type ls, rs, ns;
			std::tie(ls, rs, ns) = worklist.pop_back();
			a.setAccept(ns, left.accept(ls) && right.accept(rs));

			for (const Transition& lt : left.transitions_[ls])
				for (const Transition& rt : right.transitions_[rs]) {
					symbol_mask_type common = lt.symbols_ & rt.symbols_;
					if (common.any()) {
						state_type leftnext = lt.next_, rightnext = rt.next_;
						auto p = newstates.compute_if_absent({leftnext, rightnext}, [&]{return a.addState();});
						if (p.second)
							worklist.push_back({leftnext, rightnext, p.first});
						a.addTrans(ns, common, p.first);
					}
				}
		}
		return a;
	}

	static Automaton shuffleAcceptDeterministic(const Automaton& left, const Automaton& right);

	//This grants more friendship then we need, but we'd have to forward-declare
	//to grant to just one instantiation, and that's not really worth it.
	template<unsigned int N>
	friend Automaton<N> conj(const Automaton<N>& left, const Automaton<N>& right);
	template<unsigned int N>
	friend Automaton<N> shuffleAccept(const Automaton<N>& left, const Automaton<N>& right);

public:
	/**
	 * Returns true iff this automaton's language is empty (contains no
	 * strings).
	 *
	 * This function is not const because it needs to call removeDeadStates().
	 * This function is not named empty() because that name is already taken by
	 * the static member function that creates empty automata.
	 * @return true iff this automaton's language is empty
	 */
	bool isEmpty();

	void determinize() override;

	/**
	 * Fills in any missing transitions with transitions to an explicit crash
	 * state.
	 */
	void totalize() override;

	/**
	 * Removes dead states and transitions from this automaton.
	 */
	void removeDeadStates() override;

	void minimize() override;

private:
	/**
	 * Compresses the states of this automaton.
	 */
	void compressRenumber(state_type newSize, const state_type* survivorFrom, const state_type* remapping);

public:
	/**
	 * Renumbers states to bring this automaton into a canonical form. Canonical
	 * automata are structurally equal iff they accept the same language.
	 */
	void canonicalize() override;

	/**
	 * Renumbers states and symbols to bring this automaton into a canonical
	 * form, possibly with a different accepted language.
	 */
	template<typename Iter>
	void canonicalizeRenumber(Iter alphabetPermsBegin, Iter alphabetPermsEnd) {
		assert(alphabetPermsBegin != alphabetPermsEnd);
		//Can't test canonical_ here, because it means "canonical with respect
		//to the accepted language", and we might change the language.  We still
		//set canonical_ when we're done, because we are canonical for the new
		//language.
		minimize();

		using automaton::detail::begin;
		std::vector<detail::LazyEdgeEnumerator<decltype(begin(*alphabetPermsBegin))>> enumerators;
		enumerators.reserve(std::distance(alphabetPermsBegin, alphabetPermsEnd));
		for (auto perm : make_range_for_pair(alphabetPermsBegin, alphabetPermsEnd))
			enumerators.emplace_back(begin(perm), this);

		while (enumerators.size() > 1 && !enumerators[0].finished()) {
			decltype(enumerators[0]()) leastEdge{std::numeric_limits<state_type>::max(),
					std::numeric_limits<state_type>::max(), std::numeric_limits<symbol_type>::max()};
			for (auto i = enumerators.size(); i-- > 0;) {
				auto edge = enumerators[i]();
				if (edge > leastEdge)
					enumerators.erase(enumerators.begin()+i);
				else if (edge < leastEdge) {
					leastEdge = edge;
					enumerators.erase(enumerators.begin()+i+1, enumerators.end());
				}
			}
		}
		//there may be multiple enumerators remaining, but they are equivalent
		enumerators[0].runToCompletion();
		renumber(enumerators[0].renumbering.begin(), begin(enumerators[0].alphabetPerm));
		prepareForEquals();
		canonical_ = true;
	}

private:
	template<class RandomAccessIterator>
	static symbol_mask_type renumberAlphabet(symbol_mask_type cur, RandomAccessIterator map) {
		symbol_mask_type ns;
		for (symbol_type a = 0; a < alphabet_size_v; ++a) {
			symbol_type i = map[a];
			if (i == std::numeric_limits<symbol_type>::max())
				ns.reset(a); //no-op, because we initialized to 0 above
			else if (i == (std::numeric_limits<symbol_type>::max()-1))
				ns.set(a);
			else
				ns.set(a, cur[i]);
		}
		return ns;
	}

public:
	/**
	 * Renumbers the symbols on transitions out of all states in this automaton
	 * by looking up through the given iterator.  Mapping a symbol to
	 * std::numeric_limits<symbol_type>::max() stores a constant 0 (no
	 * transition for that symbol); mapping to max()-1 stores a constant 1
	 * (transition on that symbol).
	 */
	template<class RandomAccessIterator>
	void renumberAlphabet(RandomAccessIterator symbols) {
		renumberAlphabet(0, state_size(), symbols);
	}

	/**
	 * Renumbers the symbols on transitions out of the states in the given range
	 * by looking up through the given iterator.  Mapping a symbol to
	 * std::numeric_limits<symbol_type>::max() stores a constant 0 (no
	 * transition for that symbol); mapping to max()-1 stores a constant 1
	 * (transition on that symbol).
	 */
	template<class RandomAccessIterator>
	void renumberAlphabet(state_type stateBegin, state_type stateEnd, RandomAccessIterator symbols) {
		for (state_type s = stateBegin; s != stateEnd; ++s) {
			for (Transition& t : transitions_[s])
				t.symbols_ = renumberAlphabet(t.symbols_, symbols);
			//we may have emptied a transition
			transitions_[s].erase(std::remove_if(transitions_[s].begin(), transitions_[s].end(),
					[](Transition& t){return t.symbols_.none();}), transitions_[s].end());
			if (deterministic() && !isStateDeterministic(s))
				deterministic_ = false;
		}
		minimal_ = canonical_ = false;
	}

	/**
	 * Renumbers states.  After this method returns, state i is numbered
	 * states[i].  The given iterator must point to a permutation of size equal
	 * to the number of states in this automaton.  The sequence pointed to by
	 * the iterator will be modified.  Note that renumbering state 0 to any
	 * other number may change the language accepted by this automaton.
	 */
	template<class RandomAccessIterator>
	void renumberStates(RandomAccessIterator states) {
		if (states[0] != 0)
			minimal_ = false;
		for (auto& ts : transitions_)
			for (Transition& t : ts)
				t.next_ = states[t.next_];
		//apply_reverse_permutation destroys the permutation, so we'll copy the bitset
		//and manually permute.  (The bitset is smaller than the permutation.)
		boost::dynamic_bitset<std::size_t> accept;
		accept.resize(transitions_.size()); //yes, resize, not reserve
		for (state_type i = 0; i < transitions_.size(); ++i)
			accept.set(states[i], accept_.test(i));
		accept_ = std::move(accept);
		apply_reverse_permutation(transitions_.begin(), transitions_.end(), states);
		canonical_ = false;
	}

	/**
	 * Swaps the given state numbers.  This is somewhat more efficient than
	 * calling renumberStates if only a few states are being swapped and avoids
	 * having to allocate a permutation array.  Note that renumbering state 0
	 * to any other number may change the language accepted by this automaton.
	 */
	void swapStateNumbers(state_type a, state_type b) override;

	/**
	 * Renumbers states and symbols.  After this method returns, state i is
	 * numbered states[i] and symbol i is numbered symbols[i].  Both iterators
	 * must point to permutations of the appropriate size.
	 */
	template<class RandomAccessIterator1, class RandomAccessIterator2>
	void renumber(RandomAccessIterator1 states, RandomAccessIterator2 symbols) {
		if (states[0] != 0)
			minimal_ = false;
		//Renumber transitions_[*].next_ and .symbols_, then swap transitions_.
		//This doesn't just call renumberStates followed by renumberAlphabet to
		//preserve locality when iterating transitions_.
		for (auto& ts : transitions_)
			for (Transition& t : ts) {
				t.next_ = states[t.next_];
				t.symbols_ = renumberAlphabet(t.symbols_, symbols);
				//We're assuming it's actually a permutation, and so not checking
				//for transitions becoming empty or determinism changing.
				//TODO: we really should check, or assert it's a permutation
			}
		//apply_reverse_permutation destroys the permutation, so we'll copy the bitset
		//and manually permute.  (The bitset is smaller than the permutation.)
		boost::dynamic_bitset<std::size_t> accept;
		accept.resize(transitions_.size()); //yes, resize, not reserve
		for (state_type i = 0; i < transitions_.size(); ++i)
			accept.set(states[i], accept_.test(i));
		accept_ = std::move(accept);
		apply_reverse_permutation(transitions_.begin(), transitions_.end(), states);
		canonical_ = false;
	}

	/**
	 * Enumerates the strings accepted by this automaton.
	 *
	 * This function is not const because it may need to determinize the
	 * automaton (to ensure each string is only generated once).
	 */
	template<class Alphabet, class Callable>
	void enumerate(Callable callback) {
		determinize();
		removeDeadStates();
		std::vector<state_type> stateStack;
		stateStack.push_back(0);
		std::vector<typename Alphabet::symbol_type> symbolString;
		enumerateRecurse<Alphabet>(stateStack, symbolString, callback);
	}

	/**
	 * Prepares this automaton for equality testing.
	 *
	 * TODO: this is a hack.  We should move canonicalization into Automaton and
	 * do any preparation there instead.
	 */
	void prepareForEquals();

private:
	bool preparedForEquals() const;

public:
	/**
	 * Compares this automaton with another for structural equality.  Call
	 * prepareForEquals() on both automata first.
	 */
	bool operator==(const Automaton& other) const;
	/**
	 * Compares this automaton with another for structural inequality.  Call
	 * prepareForEquals() on both automata first.
	 */
	bool operator!=(const Automaton& other) const;

	std::size_t working_hash() const override;

private:

	struct Transition {
		Transition() = default;
		Transition(state_type next, symbol_mask_type symbols) : next_(next), symbols_(symbols) {}
		state_type next_;
		symbol_mask_type symbols_;
		bool operator==(Transition other) const {
			return std::tie(next_, symbols_) == std::tie(other.next_, other.symbols_);
		}
		bool operator!=(Transition other) const {
			return !(*this == other);
		}
		friend class std::hash<Transition>;
	};

	/**
	 * For each state, a list of transitions leaving that state.
	 */
	std::vector<small_vector<Transition, 4>> transitions_;
	/**
	 * For each state in the automaton, true if the state is an accept state.
	 */
	boost::dynamic_bitset<std::size_t> accept_;
	/**
	 * True if this automaton is known to be deterministic and false otherwise.
	 */
	bool deterministic_;
	/**
	 * True if this automaton is known to be minimal and false otherwise.
	 */
	bool minimal_;
	/**
	 * True if this automaton is known to be canonical and false otherwise.
	 */
	bool canonical_;

public:
	/**
	 * Copies all states from the given automaton into this automaton, adjusting
	 * transition numbers as required.  This does not add any transitions to the
	 * newly-copied states.
	 * @return the number of states of this automaton before appending;
	 * equivalently, the number of the first inserted state (if any)
	 */
	state_type append(const Automaton& b);

	state_type append(Automaton&& b);

	/**
	 * Adds the given transitions to this automaton.
	 * @return true if the automaton changed, false if all transitions were
	 * already present
	 */
	bool addTrans(state_type from, symbol_mask_type symbols, state_type to);

	/**
	 * Returns a mask of the symbols for which the given state has outgoing
	 * transitions.
	 * @return a mask of the symbols for which the given state has outgoing
	 * transitions.
	 */
	symbol_mask_type outgoing_mask(state_type state) const;

	/**
	 * @return a set containing the symbols that appear as labels on transitions
	 * in this automaton.
	 */
	SymbolSet activeAlphabet() const override;

	/**
	 * Returns true iff the given state transitions to at most one state on
	 * every symbol.
	 */
	bool isStateDeterministic(state_type state) const;
private:
	template<class Alphabet, class Callable>
	void enumerateRecurse(std::vector<state_type>& stateStack, std::vector<typename Alphabet::symbol_type>& symbolString, Callable callback) {
		state_type cur = stateStack.back();
		if (accept_[cur])
			callback(symbolString);
		//For large alphabets we're better off walking the bitsets.
		for (symbol_type s = 0; s < AlphabetSize; ++s) {
			auto nexts = stepDeterministic(cur, s);
			if (!nexts) continue;
			state_type next = *nexts;
			//We only enumerate finite languages, so we shouldn't visit the
			//same state more than once.
			assert(std::find(stateStack.begin(), stateStack.end(), next) == stateStack.end());
			stateStack.push_back(next);
			symbolString.push_back(Alphabet::at(s));
			enumerateRecurse<Alphabet>(stateStack, symbolString, callback);
			assert(symbolString.back() == Alphabet::at(s));
			symbolString.pop_back();
			assert(stateStack.back() == next);
			stateStack.pop_back();
		}
	}

	//Friend these to let them set minimal_ and canonical_.
	template<unsigned int N>
	friend Automaton<N> empty();
	template<unsigned int N>
	friend Automaton<N> all();
	template<unsigned int N>
	friend Automaton<N> epsilon();
	template<unsigned int N>
	friend Automaton<N> any();
	template<unsigned int N>
	friend Automaton<N> lit(std::initializer_list<typename Automaton<N>::symbol_type> symbols);

	void setFlagsHack(bool deterministic, bool minimal, bool canonical) override {
		//implications converted to disjunctions:
		assert(!minimal || deterministic);
		assert(!canonical || minimal);
		deterministic_ = deterministic;
		minimal_ = minimal;
		canonical_ = canonical;
		if (this->canonical())
			prepareForEquals();
	}
};

#define AUTOMATON_EXTERN_TEMPLATE extern
#define AUTOMATON_SIZE 1
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 2
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 3
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 4
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 5
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 6
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 7
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 8
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 9
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 10
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 12
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 13
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 14
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 15
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 16
#include "automaton-instantiations.hpp"
#undef AUTOMATON_EXTERN_TEMPLATE

/**
 * Returns an Automaton that accepts the empty language.
 */
template<unsigned int N>
Automaton<N> empty() {
	Automaton<N> a;
	a.addState();
	a.minimal_ = a.canonical_ = true;
	return a;
}

/**
 * Returns an Automaton that accepts the language of all strings.
 */
template<unsigned int N>
Automaton<N> all() {
	Automaton<N> a;
	a.addState();
	a.setAccept(0);
	for (typename Automaton<N>::symbol_type s = 0; s < N; ++s)
		a.addTrans(0, s, 0);
	a.minimal_ = a.canonical_ = true;
	return a;
}

/**
 * Returns an Automaton that accepts only the empty string.
 */
template<unsigned int N>
Automaton<N> epsilon() {
	Automaton<N> a;
	a.addState();
	a.setAccept(0);
	a.minimal_ = a.canonical_ = true;
	return a;
}

/**
 * Returns an Automaton that accepts any single character.
 */
template<unsigned int N>
Automaton<N> any() {
	Automaton<N> a;
	a.addState();
	a.addState();
	a.setAccept(1);
	for (typename Automaton<N>::symbol_type s = 0; s < N; ++s)
		a.addTrans(0, s, 1);
	a.minimal_ = a.canonical_ = true;
	return a;
}

namespace detail {
void do_lit(AutomatonBase& a, std::initializer_list<symbol_type> symbols);
}

/**
 * Returns an Automaton that accepts only the string containing just the given
 * symbol(s).
 */
template<unsigned int N>
Automaton<N> lit(std::initializer_list<typename Automaton<N>::symbol_type> symbols) {
	Automaton<N> a;
	detail::do_lit(a, symbols);
	a.minimal_ = true;
	if (a.state_size() <= 2)
		a.canonical_ = true;
	return a;
}

/**
 * Returns an Automaton that accepts only the string containing just the given
 * symbol(s).
 */
template<unsigned int N, typename... Symbols>
Automaton<N> lit(Symbols... symbols) {
	return lit<N>({numeric_cast<typename Automaton<N>::symbol_type>(symbols)...});
}

namespace detail {

template<class A, typename enabled = std::enable_if_t<std::is_base_of_v<AutomatonBase, A>>>
struct alphabet_size_trait : std::integral_constant<unsigned int, 0> {};
template<unsigned int N>
struct alphabet_size_trait<Automaton<N>> : std::integral_constant<unsigned int, Automaton<N>::alphabet_size_v> {};

template<class ...Automata>
constexpr unsigned int deduce_size() {
	if constexpr (!sizeof...(Automata))
		throw "cannot deduce from empty pack";
	unsigned int sizes[sizeof...(Automata)] = {alphabet_size_trait<Automata>::value...};
	unsigned int current = 0;
	for (unsigned int i : sizes) {
		if (current == 0)
			current = i; //i may be 0, but that's a no-op
		if (current != 0 && i != 0 && i != current)
			throw "size mismatch";
	}
	if (current == 0)
		throw "no size provided";
	return current;
}

template<class ForwardIterator, class = std::void_t<typename std::iterator_traits<ForwardIterator>::iterator_category>>
AutomatonBase::state_type total_states(ForwardIterator begin, ForwardIterator end) {
	return std::accumulate(begin, end, 0u,
			[](AutomatonBase::state_type x, const AutomatonBase& a) {
				return x + a.state_size();
			});
}

template<unsigned int N, class Source, class = std::enable_if_t<std::is_base_of<AutomatonBase, std::decay_t<Source>>::value>>
void cat_once(Automaton<N>& target, Source&& source) {
	auto base = target.append(std::forward<Source>(source));
	//Wire the previous automaton's accept states to the current initial
	//state (base), modifying them to not accept.
	//TODO: addEpsilon may cause p to become accepting again, so we have
	//to scan from 0 each time.  We should probably keep a set of
	//accepting /state indices to avoid the repeated scanning.
	for (typename Automaton<N>::state_type p = 0; p < base; ++p) {
		if (target.accept(p)) {
			target.setAccept(p, false);
			target.addEpsilon(p, base);
		}
	}
}

template<unsigned int N, class Source, class = std::enable_if_t<std::is_base_of<AutomatonBase, std::decay_t<Source>>::value>>
void alt_once(Automaton<N>& target, Source&& source) {
	auto base = target.append(std::forward<Source>(source));
	target.addEpsilon(0, base);
}
} //namespace detail

template<class ForwardIterator, unsigned int N = std::iterator_traits<ForwardIterator>::value_type::alphabet_size_v>
Automaton<N> cat(ForwardIterator begin, ForwardIterator end) {
	if (begin == end)
		//the empty string is the identity element for concatenation
		return epsilon<N>();

	Automaton<N> a;
	a.reserve(detail::total_states(begin, end));
	std::for_each(begin, end, [&](auto& v){detail::cat_once(a, v);});
	return a;
}
template<unsigned int N>
Automaton<N> cat(std::initializer_list<Automaton<N>> list) {
	return cat(list.begin(), list.end());
}

template<unsigned int N>
Automaton<N> cat() {
	return epsilon<N>();
}

template<typename... Automata>
auto cat(Automata&&... rest) {
	constexpr unsigned int N = detail::deduce_size<std::decay_t<Automata>...>();
	return cat<N, Automata...>(std::forward<Automata>(rest)...);
}
template<unsigned int N, typename... Automata>
Automaton<N> cat(Automata&&... rest) {
	Automaton<N> a;
	a.reserve((0u + ... + rest.state_size()));
	(detail::cat_once(a, std::forward<Automata>(rest)), ...);
	return a;
}

template<class ForwardIterator, unsigned int N = std::iterator_traits<ForwardIterator>::value_type::alphabet_size_v>
auto alt(ForwardIterator begin, ForwardIterator end) {
	if (begin == end)
		return empty<N>();

	Automaton<N> a;
	a.reserve(detail::total_states(begin, end) + 1);
	//initial state that transitions to the individual machines' states
	a.addState();
	std::for_each(begin, end, [&](auto& v){detail::alt_once(a, v);});
	return a;
}
template<unsigned int N>
Automaton<N> alt(std::initializer_list<Automaton<N>> list) {
	return alt(list.begin(), list.end());
}

template<unsigned int N>
Automaton<N> alt() {
	return empty<N>();
}

template<typename... Automata>
auto alt(Automata&&... rest) {
	constexpr unsigned int N = detail::deduce_size<std::decay_t<Automata>...>();
	return alt<N, Automata...>(std::forward<Automata>(rest)...);
}
template<unsigned int N, typename... Automata>
Automaton<N> alt(Automata&&... rest) {
	Automaton<N> a;
	a.reserve((0u + ... + rest.state_size()));
	(detail::alt_once(a, std::forward<Automata>(rest)), ...);
	return a;
}

template<unsigned int N>
Automaton<N> conj(const Automaton<N>& left, const Automaton<N>& right) {
	Automaton<N> a;
	try {
		a = Automaton<N>::template conj_impl<detail::DenseConjMap>(left, right);
	} catch (std::bad_alloc&) {
		AUTOMATON_DEBUG(std::cout << "caught bad_alloc: conj_impl<DenseConjMap>" << std::endl);
	}
	if (!a.isEmpty())
		try {
			a = Automaton<N>::template conj_impl<detail::UnorderedConjMap>(left, right);
		} catch (std::bad_alloc&) {
			AUTOMATON_DEBUG(std::cout << "caught bad_alloc: conj_impl<UnorderedConjMap>" << std::endl);
		}
	if (!a.isEmpty())
		//No try-catch here because there's no further recovery
		a = Automaton<N>::template conj_impl<detail::SparseConjMap>(left, right);
	AUTOMATON_DEBUG(std::cout << "intersection: " << left.state_size() << ", " << right.state_size() << " -> " << a.state_size() << std::endl);
	a.removeDeadStates();
	return a;
}
//TODO: we could add vararg/iterator-range overloads of conj for convenience,
//but we know from experience actually doing a multi-way conj is worse

template<unsigned int N>
Automaton<N> star(const Automaton<N>& b) {
	Automaton<N> a;
	a.reserve(b.state_size() + 1);
	a.addState();
	a.setAccept(0);
	typename Automaton<N>::state_type base = a.append(b);
	a.addEpsilon(0, 1);
	for (typename Automaton<N>::state_type p = base; p < a.state_size(); ++p)
		if (a.accept(p))
			a.addEpsilon(p, 0);
	return a;
}

template<unsigned int N>
Automaton<N> plus(const Automaton<N>& a) {
	return nOrMore(a, 1);
}
template<unsigned int N>
Automaton<N> maybe(const Automaton<N>& a) {
	return range(a, 0, 1);
}
template<unsigned int N>
Automaton<N> nCopies(const Automaton<N>& a, unsigned int count) {
	std::vector<const Automaton<N>*> v(count, &a);
	return cat(boost::make_indirect_iterator(v.begin()),
					boost::make_indirect_iterator(v.end()));
}
template<unsigned int N>
Automaton<N> nOrMore(const Automaton<N>& a, unsigned int min) {
	if (min == 0) return star(a);
	std::vector<const Automaton<N>*> v(min, &a);
	Automaton<N> rest = star(a);
	v.push_back(&rest);
	return cat(boost::make_indirect_iterator(v.begin()),
					boost::make_indirect_iterator(v.end()));
}
template<unsigned int N>
Automaton<N> range(const Automaton<N>& a, unsigned int min, unsigned int max) {
	assert(max > min);
	//TODO: inlining nCopies would save a copy
	Automaton<N> ret = nCopies(a, min);
	ret.reserve(max * a.state_size());
	//like cat, but not clearing the accept states (and so not scanning from state 0 each time)
	typename Automaton<N>::state_type lastbase = 0;
	for (typename Automaton<N>::state_type i = 0; i < max - min; ++i) {
		typename Automaton<N>::state_type base = ret.append(a);
		for (typename Automaton<N>::state_type q = lastbase; q < base; ++q)
			if (ret.accept(q))
				ret.addEpsilon(q, base);
		lastbase = base;
	}
	return ret;
}

template<unsigned int N>
Automaton<N> comp(Automaton<N> a) {
	a.determinize();
	a.totalize();
	//TODO: we could add a method to flip everything at once (or make comp a friend)
	for (typename Automaton<N>::state_type s = 0; s < a.state_size(); ++s)
		a.setAccept(s, !a.accept(s));
	return a;
}

/**
 * Computes the accepting shuffle of the given automata.  The accepting
 * shuffle is the language accepted by running both automata in parallel,
 * passing each character to one or the other automaton, switching the
 * active automaton only when both automata are in accepting states.
 */
template<unsigned int N>
Automaton<N> shuffleAccept(const Automaton<N>& left, const Automaton<N>& right) {
	return Automaton<N>::shuffleAcceptDeterministic(
			left.deterministic() ? left : automaton::determinize(left),
			right.deterministic() ? right : automaton::determinize(right));
}

/**
 * @return a determinized copy of the given automaton
 */
template<unsigned int N>
Automaton<N> determinize(Automaton<N> a) {
	a.determinize();
	return a;
}

/**
 * @return a minimized copy of the given automaton
 */
template<unsigned int N>
Automaton<N> minimize(Automaton<N> a) {
	a.minimize();
	return a;
}

/**
 * @return a canonicalized copy of the given automaton
 */
template<unsigned int N>
Automaton<N> canonicalize(Automaton<N> a) {
	a.canonicalize();
	return a;
}

/**
 * @return a canonicalized, possibly-symbol-renumbered copy of the given
 * automaton
 */
template<unsigned int N, typename Iter>
Automaton<N> canonicalizeRenumber(Automaton<N> a, Iter alphabetPermsBegin, Iter alphabetPermsEnd) {
	a.canonicalizeRenumber(alphabetPermsBegin, alphabetPermsEnd);
	return a;
}

/**
 * Returns true iff the given automata accept the same language.
 */
template<unsigned int N>
bool same_language(const Automaton<N>& left, const Automaton<N>& right) {
	return conj(left, comp(right)).isEmpty() && conj(comp(left), right).isEmpty();
}

template<unsigned int N>
struct ComparisonResult {
	//whether left accepts strings right doesn't or vice-versa
	Automaton<N> leftButNotRight, rightButNotLeft;
	bool equal() {return leftButNotRight.isEmpty() && rightButNotLeft.isEmpty();}
	bool smaller() {return leftButNotRight.empty() && !rightButNotLeft.empty();}
	bool larger() {return !leftButNotRight.empty() && rightButNotLeft.empty();}
	bool incomparable() {return !leftButNotRight.empty() && !rightButNotLeft.empty();}
	//TODO: methods to compute witnesses, for fluent use of compare_languages?
};
/**
 * Compares the languages accepted by the given automata.
 */
template<unsigned int N>
ComparisonResult<N> compare_languages(const Automaton<N>& left, const Automaton<N>& right) {
	return {conj(left, comp(right)), conj(comp(left), right)};
}

namespace detail {
struct Tarjan;
}

class SCCs {
public:
	unsigned int size() const {
		return static_cast<unsigned int>(indices_.size()-1);
	}
	auto begin(unsigned int component) const {
		return components_.begin() + indices_[component];
	}
	auto end(unsigned int component) const {
		return components_.begin() + indices_[component+1];
	}
private:
	using state_type = AutomatonBase::state_type;
	std::vector<AutomatonBase::state_type> components_;
	std::vector<unsigned int> indices_;
	friend struct detail::Tarjan;
};

SCCs find_components(const AutomatonBase& a);

} //namespace automaton

namespace std {
template<unsigned int N>
struct hash<automaton::Automaton<N>> {
	size_t operator()(const automaton::Automaton<N>& a) const {
		return a.working_hash();
	}
};
} //namespace std

#endif /* AUTOMATON_HPP */

