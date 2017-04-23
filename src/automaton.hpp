/*
 * File:   automaton.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 16, 2016, 4:36 PM
 */

#ifndef AUTOMATON_HPP
#define AUTOMATON_HPP

#include "precompiled.hpp"
#include "linear_set.hpp"

//uncomment the line below to enable debugging logging expressions
//#define AUTOMATON_DEBUG(expr) do {expr;} while(0);
#ifndef AUTOMATON_DEBUG
#define AUTOMATON_DEBUG(expr) do {} while(0);
#endif

namespace automaton {

class AutomatonBase;
template<unsigned int AlphabetSize>
class Automaton;

namespace detail {
class EdgeRangeSentinel;
class EdgeRangeFront;

struct AutomatonReprStreamer {
	const AutomatonBase& a;
};
std::ostream& operator<<(std::ostream& os, const AutomatonReprStreamer& rs);
} //namespace detail

/**
 * Automaton functionality that does not depend on the alphabet size.
 *
 * (IAutomaton and Automatonable were both considered as names.)
 */
class AutomatonBase {
public:
	using symbol_type = unsigned int; //cf. Literal
	//We could save space by using a smaller type for small automata, but it's
	//hard to know what size to use before building the automaton.  We'd only
	//save on automata that are already small, so it's not really worth it.
	using state_type = unsigned int;

	//if we could forward-declare typedefs/aliases, these would be delcared outside
	//because we can't, we'll put them here and lift them out later
	using SymbolSet = linear_set<AutomatonBase::symbol_type>;
	using StateSet = linear_set<AutomatonBase::state_type>;

	virtual ~AutomatonBase();

	/**
	 * @return the number of states in this automaton
	 */
	virtual state_type state_size() const = 0;
	/**
	 * @return the alphabet size of this automaton
	 */
	virtual symbol_type alphabet_size() const = 0;
	/**
	 * Returns the number of edges in this automaton.  This is related to the
	 * physical size of this object.
	 * @return the number of edges in this automaton
	 */
	virtual std::size_t edge_size() const = 0;
	/**
	 * Returns the number of transitions in this automaton.  This is a logical
	 * notion; multiple transitions may be stored in one physical edge.
	 * @return the number of transitions in this automaton
	 */
	virtual std::size_t transition_size() const = 0;

	virtual bool accept(state_type state) const = 0;
	/**
	 * Returns the possible next states of the automaton when reading the given
	 * symbol in the given current state.  The returned set is empty if the
	 * automaton crashes.
	 */
	virtual StateSet step(state_type state, symbol_type symbol) const = 0;
	/**
	 * Returns a set of the labels on the edge between from and to.  This set is
	 * empty if there is no edge between from and to.
	 *
	 * labels is in some sense the orthogonal operation to step: step tells
	 * where a symbol leads to, while label tells what symbols lead to a
	 * particular place.
	 */
	virtual SymbolSet labels(state_type from, state_type to) const = 0;

	range_for_pair<detail::EdgeRangeFront, detail::EdgeRangeSentinel> edges(state_type from) const;

	virtual void reserve(state_type state_capacity) = 0;
	/**
	 * Adds a new state to this automaton.  The state is rejecting and has no
	 * outgoing transitions.
	 */
	virtual state_type addState() = 0;
	/**
	 * Add transitions out of from that simulate the presence of an epsilon
	 * transition into to.  (Later modifications of to's transitions will not
	 * result in corresponding updates of from's transitions.)
	 * @return true if the automaton changed, either by adding a transition or
	 * making a non-accepting state an accepting state
	 */
	virtual bool addEpsilon(state_type from, state_type to) = 0;
	/**
	 * Adds a transition to this automaton.
	 * @return true if the automaton changed, false if the transition was
	 * already present
	 */
	virtual bool addTrans(state_type from, symbol_type on, state_type to) = 0;
	virtual bool setAccept(state_type state, bool accepts = true) = 0;

	//TODO: we can't use symbol_mask_type, but maybe we'll want an opaque token
	//type to allow e.g. copying a set of transitions

	//TODO: equality, somehow (false if alphabet sizes disagree)

	//TODO: we should have a sufficiently rich set of observer methods to
	//implement printing just once for this interface
//	friend std::ostream& operator<<(std::ostream& o, const AutomatonBase& a) {
//		o << a.state_size() << " states, " << a.transition_size() << " transitions\n";
//		//TODO: not sure if we're still tracking determinism or not
////				<< (a.deterministic() ? "" : "non") << "deterministic\n";
//		for (state_type i = 0; i < a.state_size(); ++i) {
//			o << "state " << i << (a.accept(i) ? " [accept]:\n" : ":\n");
//			//This is edge iteration: (next, symbol-set) pairs
//			for (
//		}
//
//		for (state_type i = 0; i < a.size(); ++i) {
//			o << "state " << i << (a.accept_[i] ? " [accept]:\n" : ":\n");
//			for (const Transition& t : a.transitions_[i])
//				o << "  to " << t.next_ << " on " << t.symbols_ << '\n';
//		}
//		return o;
//	}

	/**
	 * Returns an object of unspecified type that, when streamed to a
	 * std::ostream, outputs a C++ expression that constructs an Automaton equal
	 * to this one.  Use this like "std::cout << a.repr() << std::endl".
	 * @return a streamable object that outputs a repr string
	 */
	auto repr() const {
		return detail::AutomatonReprStreamer{*this};
	}
};

using SymbolSet = AutomatonBase::SymbolSet;
using StateSet = AutomatonBase::StateSet;

namespace detail {
class EdgeRangeSentinel {
	const AutomatonBase* parent_;
	AutomatonBase::state_type from_;
	EdgeRangeSentinel(const AutomatonBase* parent, AutomatonBase::state_type from) : parent_(parent), from_(from) {}
	friend AutomatonBase;
	friend bool operator==(const EdgeRangeFront& left, EdgeRangeSentinel right);
};

class EdgeRangeFront {
	EdgeRangeFront(const AutomatonBase* parent, AutomatonBase::state_type from) : parent_(parent), from_(from), cur_(findNextStartingAt(0)) {}
	const AutomatonBase* parent_;
	AutomatonBase::state_type from_;
	std::pair<SymbolSet, AutomatonBase::state_type> cur_;
	std::pair<SymbolSet, AutomatonBase::state_type> findNextStartingAt(AutomatonBase::state_type start) {
		AutomatonBase::state_type end = parent_->state_size();
		//TODO: if this gets too expensive, we'll promote destinations()
		//to AutomatonBase and cache it in this iterator
		for (AutomatonBase::state_type s = start; s < end; ++s) {
			SymbolSet symbols = parent_->labels(from_, s);
			if (!symbols.empty())
				return {std::move(symbols), s};
		}
		return {{}, end};
	}
public:
	const auto& operator*() const {
		return cur_;
	}
	auto& operator++() {
		cur_ = findNextStartingAt(cur_.second+1);
		return *this;
	}
	friend AutomatonBase;
	friend bool operator==(const EdgeRangeFront& left, EdgeRangeSentinel right);
};

inline bool operator==(const EdgeRangeFront& left, EdgeRangeSentinel right) {
		assert(left.parent_ == right.parent_ && "edge ranges from different parents");
		assert(left.from_ == right.from_ && "edge ranges from different states");
		return left.cur_.second == left.parent_->state_size();
}
inline bool operator!=(const EdgeRangeFront& left, EdgeRangeSentinel right) {
	return !(left == right);
}
} //namespace detail

inline range_for_pair<detail::EdgeRangeFront, detail::EdgeRangeSentinel> AutomatonBase::edges(state_type from) const {
	return make_range_for_pair(detail::EdgeRangeFront(this, from), detail::EdgeRangeSentinel(this, from));
}



namespace detail {
using state_type = AutomatonBase::state_type;
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

class DenseShuffleAcceptMap {
public:
	using key_type = std::tuple<state_type, state_type, bool>;
	DenseShuffleAcceptMap(std::size_t leftSize, std::size_t rightSize) : map_() {
		//silent narrowing conversion: http://stackoverflow.com/q/37928951/3614835
		map_.set_empty_key({leftSize, rightSize, false});
	}
	void insert(key_type oldstates, state_type newstate) {
		map_.insert({oldstates, newstate});
	}
	template<class Callable>
	std::pair<state_type, bool> compute_if_absent(key_type oldstates, Callable newstateProvider) {
		//dense_hashtable::find_or_insert is so close to what we want :(
		auto it = map_.find(oldstates);
		if (it != map_.end())
			return {it->second, false};
		auto r = map_.insert({oldstates, newstateProvider()});
		return {r.first->second, true};
	}
private:
	google::dense_hash_map<key_type, state_type, boost::hash<key_type>> map_;
};
} //namespace detail



template<unsigned int N> Automaton<N> empty();
template<unsigned int N> Automaton<N> all();

template<unsigned int AlphabetSize>
class Automaton final : public AutomatonBase {
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
	Automaton() : deterministic_(true) {}
	Automaton(const Automaton& a) = default;
	Automaton(Automaton&& a) = default;
	Automaton& operator=(const Automaton& a) = default;
	Automaton& operator=(Automaton&& victim) = default;

	state_type state_size() const override {
		return static_cast<state_type>(transitions_.size());
	}
	symbol_type alphabet_size() const override {
		return alphabet_size_v;
	}
	std::size_t edge_size() const override {
		return std::accumulate(transitions_.begin(), transitions_.end(), static_cast<std::size_t>(0),
				[](std::size_t l, const auto& r) {return l + r.size();});
	}
	std::size_t transition_size() const override {
		std::size_t answer = 0;
		for (auto& ts : transitions_)
			for (auto t : ts)
				answer += t.symbols_.count();
		return answer;
	}

	bool accept(state_type state) const override {
		assert(state < size());
		return accept_[state];
	}

	StateSet step(state_type current, symbol_type symbol) const override {
		assert(current < transitions_.size());
		assert(symbol < AlphabetSize);
		StateSet next;
		for (const Transition& t : transitions_[current])
			if (t.symbols_[symbol])
				next.insert(t.next_);
		//We used to assert(next.size() <= 1 || !deterministic_), but the
		//addTrans calls isStateDeterministic calls step (us) when checking
		//whether to clear deterministic_.  If we change the implementation of
		//isStateDeterministic to not actually build the steps, we could add the
		//assert back.
		return next;
	}

	SymbolSet labels(state_type from, state_type to) const override {
		for (const Transition& t : transitions_[from])
			if (t.next_ == to) {
				SymbolSet ret;
				for (symbol_type a = 0; a < alphabet_size(); ++a)
					if (t.symbols_[a])
						ret.insert(a);
				return ret;
			}
		return {};
	}

	void reserve(state_type state_capacity) override {
		transitions_.reserve(state_capacity);
		accept_.reserve(state_capacity);
	}

	state_type addState() override {
		state_type s = static_cast<state_type>(transitions_.size());
		transitions_.push_back({});
		accept_.push_back(false);
		return s;
	}

	bool addEpsilon(state_type from, state_type to) override {
		bool changed = false;
		if (accept_[to]) {
			changed |= !accept_[from];
			accept_.set(from);
		}
		for (const Transition& t : transitions_[to])
			changed |= addTrans(from, t.symbols_, t.next_);
		return changed;
	}

	bool addTrans(state_type from, symbol_type symbol, state_type to) override {
		for (Transition& t : transitions_[from])
			if (t.next_ == to) {
				if (t.symbols_[symbol])
					return false;
				t.symbols_.set(symbol);
				if (deterministic_ && !isStateDeterministic(from))
					deterministic_ = false;
				return true;
			}
		transitions_[from].push_back({});
		transitions_[from].back().next_ = to;
		transitions_[from].back().symbols_.set(symbol);
		if (deterministic_ && !isStateDeterministic(from))
			deterministic_ = false;
		return true;
	}

	bool setAccept(state_type state, bool accepts = true) override {
		bool old = accept_.test(state);
		accept_.set(state, accepts);
		return accepts == old;
	}




private:
	//because just "using foo;" is illegal in class scopes, and we don't want to
	//pollute the namespace
	template<typename T, std::size_t N>
	using small_vector = boost::container::small_vector<T, N>;
//	template<typename K, typename V, typename Hash = std::hash<K>>
//	using dense_hash_map = google::dense_hash_map<K, V, Hash>;
//	template<typename T>
//	using dense_hash_set = google::dense_hash_set<T>;
private:
	template <class Map>
	static Automaton conj_impl(const Automaton& left, const Automaton& right) {
		//(left state, right state, new state)
		using state_triple = std::tuple<state_type, state_type, state_type>;
		std::stack<state_triple> worklist;
		Map newstates(left.size(), right.size());

		Automaton a;
		//TODO: are we sure?
		a.deterministic_ = left.deterministic() && right.deterministic();
		a.addState();
		//TODO: assuming 0 is the initial state
		worklist.push({0, 0, 0});
		newstates.insert({0, 0}, 0);

		while (!worklist.empty()) {
			state_type ls, rs, ns;
			std::tie(ls, rs, ns) = worklist.top();
			worklist.pop();
			a.setAccept(ns, left.accept(ls) && right.accept(rs));

			for (const Transition& lt : left.transitions_[ls])
				for (const Transition& rt : right.transitions_[rs]) {
					symbol_mask_type common = lt.symbols_ & rt.symbols_;
					if (common.any()) {
						state_type leftnext = lt.next_, rightnext = rt.next_;
						auto p = newstates.compute_if_absent({leftnext, rightnext}, [&]{return a.addState();});
						if (p.second)
							worklist.push({leftnext, rightnext, p.first});
						a.addTrans(ns, common, p.first);
					}
				}
		}
		return a;
	}
	static Automaton conj(const Automaton& left, const Automaton& right) {
		Automaton a;
		try {
			a = conj_impl<detail::DenseConjMap>(left, right);
		} catch (std::bad_alloc&) {
			AUTOMATON_DEBUG(std::cout << "caught bad_alloc: conj_impl<DenseConjMap>" << std::endl);
		}
		if (!a.isEmpty())
			try {
				a = conj_impl<detail::UnorderedConjMap>(left, right);
			} catch (std::bad_alloc&) {
				AUTOMATON_DEBUG(std::cout << "caught bad_alloc: conj_impl<UnorderedConjMap>" << std::endl);
			}
		if (!a.isEmpty())
			//No try-catch here because there's no further recovery
			a = conj_impl<detail::SparseConjMap>(left, right);
		AUTOMATON_DEBUG(std::cout << "intersection: " << left.size() << ", " << right.size() << " -> " << a.size() << std::endl);
		a.removeDeadStates();
		return a;
	}

	/**
	 * Computes the accepting shuffle of the given automata.  The accepting
	 * shuffle is the language accepted by running both automata in parallel,
	 * passing each character to one or the other automaton, switching the
	 * active automaton only when both automata are in accepting states.
	 */
	static Automaton shuffleAccept(const Automaton& left, const Automaton& right) {
		//(left state, right state, new state, left automation active)
		using state_quad = std::tuple<state_type, state_type, state_type, bool>;
		std::stack<state_quad> worklist;
		detail::DenseShuffleAcceptMap newstates(left.size(), right.size());

		Automaton a;
		//TODO: are we sure?
		a.deterministic_ = left.deterministic() && right.deterministic();
		//We have a free choice to begin with the left or with the right
		//automaton, so we have two "initial" states and call addEpsilon later.
		a.addState(); a.addState(); a.addState();
		//TODO: assuming 0 is the initial state
		worklist.push({0, 0, 1, true});
		newstates.insert({0, 0, true}, 1);
		worklist.push({0, 0, 2, false});
		newstates.insert({0, 0, false}, 2);

		while (!worklist.empty()) {
			state_type ls, rs, ns;
			bool leftactive;
			std::tie(ls, rs, ns, leftactive) = worklist.top();
			worklist.pop();
			a.accept_.set(ns, left.accept_[ls] && right.accept_[rs]);

			if (leftactive) {
				for (Transition lt : left.transitions_[ls]) {
					auto p = newstates.compute_if_absent({lt.next_, rs, leftactive}, [&]{return a.addState();});
					if (p.second)
						worklist.push({lt.next_, rs, p.first, leftactive});
					a.addTrans(ns, lt.symbols_, p.first);

					//If we brought the active automaton to an accept state,
					//we can switch if we want.
					if (left.accept(lt.next_)) {
						auto q = newstates.compute_if_absent({lt.next_, rs, !leftactive}, [&]{return a.addState();});
						if (q.second)
							worklist.push({lt.next_, rs, q.first, !leftactive});
						a.addTrans(ns, lt.symbols_, q.first);
					}
				}
			} else {
				for (Transition rt : right.transitions_[rs]) {
					auto p = newstates.compute_if_absent({ls, rt.next_, leftactive}, [&]{return a.addState();});
					if (p.second)
						worklist.push({ls, rt.next_, p.first, leftactive});
					a.addTrans(ns, rt.symbols_, p.first);

					//If we brought the active automaton to an accept state,
					//we can switch if we want.
					if (right.accept(rt.next_)) {
						auto q = newstates.compute_if_absent({ls, rt.next_, !leftactive}, [&]{return a.addState();});
						if (q.second)
							worklist.push({ls, rt.next_, q.first, !leftactive});
						a.addTrans(ns, rt.symbols_, q.first);
					}
				}
			}
		}

		//Choose left or right at the start.
		a.addEpsilon(0, 1);
		a.addEpsilon(0, 2);
		return a;
	}

	//This grants more friendship then we need, but we'd have to forward-declare
	//to grant to just one instantiation, and that's not really worth it.
	template<unsigned int N>
	friend Automaton<N> conj(const Automaton<N>& left, const Automaton<N>& right);
	template<unsigned int N>
	friend Automaton<N> shuffleAccept(const Automaton<N>& left, const Automaton<N>& right);

public:
	/**
	 * Returns true if this automaton is known to be deterministic.
	 */
	bool deterministic() const {return deterministic_;}

	/**
	 * Returns the number of states in this automaton.
	 */
	state_type size() const {return static_cast<state_type>(transitions_.size());}
	/**
	 * Returns the number of transitions in this automaton.  This makes up part
	 * of the physical size of this Automaton object.
	 */
	std::size_t numTransitions() const {
		return std::accumulate(transitions_.begin(), transitions_.end(), static_cast<std::size_t>(0),
				[](std::size_t l, const auto& r) {return l + r.size();});
	}
	/**
	 * Returns the number of edges in this automaton.  An edge is a (source,
	 * symbol, dest) triple.  This is a logical measure of size not directly
	 * related to the physical size of this Automaton object.
	 */
	std::size_t edges() const {
		std::size_t answer = 0;
		for (auto& ts : transitions_)
			for (auto t : ts)
				answer += t.symbols_.count();
		return answer;
	}

	/**
	 * Runs this automaton on the given string.
	 * @returns true iff the machine accepts the given string of symbol indices
	 */
	bool run(std::initializer_list<unsigned int> string) const {
		//This overload exists because the compiler won't deduce initializer_list
		//for the IteratorRange overload below.
		return run(string.begin(), string.end());
	}
	/**
	 * Runs this automaton on the given string.
	 * @returns true iff the machine accepts the given string of symbol indices
	 */
	template<class InputIterator>
	bool run(InputIterator begin, InputIterator end) const {
		return run(boost::make_iterator_range(begin, end));
	}
	/**
	 * Runs this automaton on the given string.
	 * @returns true iff the machine accepts the given string of symbol indices
	 */
	template<class IteratorRange>
	bool run(const IteratorRange& string) const {
		//Breadth-first search.
		std::unordered_set<state_type> current, next;
		current.insert(0); //TODO: assuming 0 is the initial state
		for (unsigned int symbol : string) {
			for (state_type c : current)
				for (state_type n : step(c, symbol))
					next.insert(n);
			std::swap(current, next);
			next.clear();
		}
		return std::any_of(current.begin(), current.end(), [this](state_type s){return accept_[s];});
	}

	/**
	 * Returns true iff this automaton's language is empty (contains no
	 * strings).
	 *
	 * This function is not const because it needs to call removeDeadStates().
	 * This function is not named empty() because that name is already taken by
	 * the static member function that creates empty automata.
	 * @return true iff this automaton's language is empty
	 */
	bool isEmpty() {
		removeDeadStates();
		return size() == 1U && accept_.none();
	}

	/**
	 * Returns true iff this automaton's language is infinite.  (Not to be
	 * confused with universality, accepting the language of all strings.)
	 *
	 * This function is not const because it needs to call removeDeadStates().
	 * @return true iff this automaton's language is infinite
	 */
	bool infinite() {
		removeDeadStates();
		//If all states are live, we need only check for a cycle.
		//We could use a dynamic_bitset or a simple byte array here instead (or
		//a specialized 0..n set, if there's such a type).
		google::dense_hash_set<state_type> visited(static_cast<state_type>(size()));
		visited.set_empty_key(static_cast<state_type>(size()));
		std::vector<state_type> path;
		std::stack<boost::optional<state_type>> nexts;

		nexts.push(boost::make_optional(0U));
		while (!nexts.empty()) {
			boost::optional<state_type> n = nexts.top();
			nexts.pop();
			if (n) {
				path.push_back(*n);
				nexts.push(boost::optional<state_type>(boost::none));
				for (state_type next : destinations(path.back())) {
					//We might prefer a set; we could avoid storing path itself
					//if we store the to-be-popped element in place of the empty optional.
					if (std::find(path.begin(), path.end(), next) != path.end())
						return true;
					if (visited.find(next) == visited.end())
						nexts.push(boost::make_optional(next));
				}
			} else {
				visited.insert(path.back());
				path.pop_back();
			}
		}
		return false;
	}

	void determinize() {
		if (deterministic()) return;
		//We manually sort before inserting in newstate.
		//Unfortunately there's no small_flat_set...
		//TODO: use StateSet!
		using state_set = small_vector<state_type, 4>;
		auto hasher = [](const state_set& set) {
			//could be std::accumulate, I guess
			std::size_t accum = 0;
			for (state_type t : set)
				accum = accum * 17 + t;
			return accum;
		};

		//0 bucket count is fine: http://stackoverflow.com/q/14179441/3614835
		std::unordered_map<state_set, state_type, decltype(hasher)> newstate(0, hasher);
		//pointers to keys in newstate
		std::stack<const typename decltype(newstate)::value_type*> worklist;
		Automaton a;
		a.addState();
		auto iterSucc = newstate.insert(std::make_pair(state_set({0}), 0));
		assert(iterSucc.second);
		worklist.push(&*(iterSucc.first));

		while (!worklist.empty()) {
			auto current = worklist.top();
			worklist.pop();

			a.accept_[current->second] = std::any_of(current->first.begin(), current->first.end(),
					[this](state_type state) {return this->accept_[state];});

			state_set next;
			for (symbol_type s = 0; s < AlphabetSize; ++s) {
				next.clear();
				for (state_type f : current->first)
					for (state_type t : step(f, s))
						if (std::find(next.begin(), next.end(), t) == next.end())
							next.push_back(t);
				if (next.empty())
					continue; //all NFA states crashed
				std::sort(next.begin(), next.end());
				//TODO: is there a computeIfAbsent equivalent?
				auto it = newstate.find(next);
				if (it == newstate.end()) {
					it = newstate.insert(std::make_pair(std::move(next), a.addState())).first;
					worklist.push(&*it);
				}
				a.addTrans(current->second, s, it->second);
				assert(a.deterministic());
			}
		}

		MAYBE_UNUSED std::size_t oldsize = size();
		*this = std::move(a);
		assert(deterministic());
		AUTOMATON_DEBUG(std::cout << "determinize: " << oldsize << " -> " << size() << std::endl);
	}

	/**
	 * Fills in any missing transitions with transitions to an explicit crash
	 * state.
	 */
	void totalize() {
		state_type crash;
		bool madeCrashState = false;
		for (state_type s = 0; s < transitions_.size(); ++s) {
			symbol_mask_type missing = ~outgoing(s);
			if (missing.any()) {
				if (!madeCrashState) {
					crash = addState();
					madeCrashState = true;
				}
				addTrans(s, missing, crash);
			}
		}
	}

	/**
	 * Removes dead states and transitions from this automaton.
	 */
	void removeDeadStates() {
		auto live = liveStates();
		if (live.size() == size())
			return;
		if (live.empty()) {
			*this = empty<AlphabetSize>();
			return;
		}
		//If any states are live, the initial state must be one of them.
		assert(live.count(0) == 1);
		MAYBE_UNUSED std::size_t oldsize = size();
		//maps old state numbers to new state numbers
		natural_map<state_type, state_type> renumber(static_cast<state_type>(size()));
		boost::dynamic_bitset<std::size_t> newnumbers(live.size());
		newnumbers.set();
		for (state_type s : live)
			//live states keep their numbers if possible
			if (s < live.size()) {
				renumber.insert({s, s});
				newnumbers.reset(s);
			}
		unsigned long freestart = 0;
		for (state_type s : live)
			if (s >= live.size()) {
				freestart = newnumbers.find_next(freestart);
				renumber.insert({s, static_cast<state_type>(freestart)});
				newnumbers.reset(freestart);
			}

		for (const std::pair<state_type, state_type> p : renumber) {
			auto& nt = transitions_[p.second];
			if (p.first != p.second) {
				transitions_[p.second] = std::move(transitions_[p.first]);
				accept_[p.second] = accept_[p.first];
			}
			//iterate backwards to gracefully remove
			for (auto i = nt.size(); i-- > 0;) {
				auto it = renumber.find(nt[i].next_);
				if (it != renumber.end())
					nt[i].next_ = (*it).second;
				else
					nt.erase(nt.begin()+i);
			}
		}

		transitions_.resize(live.size());
		accept_.resize(live.size());
		//TODO: shrink_to_fit?
		AUTOMATON_DEBUG(std::cout << "removeDeadStates: " << oldsize << " -> " << size() << std::endl);
	}

	void minimize() {
		determinize();
		//The Java library explicitly checks for the all-strings automaton here,
		//but it doesn't seem to be necessary.
		//Java totalizes the automaton here (then removes the added state in
		//removeDeadStates after minimizing).  That isn't required; our Hopcroft
		//implementation understands states are not equivalent if one crashes
		//and the other doesn't.
		//Instead, we remove dead states before minimizing, to prevent
		//transitions to dead states from distinguishing states that are
		//otherwise equivalent.
		removeDeadStates();
		MAYBE_UNUSED std::size_t oldsize = size();
		HopcroftMinimizer(*this).minimize();
		AUTOMATON_DEBUG(std::cout << "minimize: " << oldsize << " -> " << size() << std::endl);
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
		renumberAlphabet(0, size(), symbols);
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
	}

//	/**
//	 * Adjust the transitions from each state in the given range by adding
//	 * distance (which may be negative) to each symbol starting with symbolBegin.
//	 */
//	void slideAlphabet(state_type stateBegin, state_type stateEnd,
//			symbol_type symbolBegin, std::make_signed_t<symbol_type> distance) {
//		if (distance == 0) return;
//		for (state_type s = stateBegin; s != stateEnd; ++s)
//			//We could slide the only bit off the right or overwrite the only bit while sliding left.
//			for (auto ti = transitions_[s].begin(); ti != transitions_[s].end();) {
//				ti->symbols_.slide(symbolBegin, distance);
//				if (ti->symbols_.none())
//					ti = transitions_[s].erase(ti);
//				else
//					++ti;
//			}
//		//TODO: update determinism flag
//	}
//
//	/**
//	 * Adjust the transitions from each state in the given range by rotating the
//	 * symbols in the given range right by the given distance.
//	 */
//	void rotateAlphabet(state_type stateBegin, state_type stateEnd,
//			symbol_type symbolBegin, symbol_type symbolEnd, std::make_signed_t<symbol_type> distance) {
//		if (distance == 0) return;
//		for (state_type s = stateBegin; s != stateEnd; ++s)
//			for (Transition& t : transitions_[s])
//				t.symbols_.rotate_range(symbolBegin, symbolEnd, distance);
//		//TODO: update determinism flag
//	}

	/**
	 * Renumbers states and symbols.  After this method returns, state i is
	 * numbered states[i].  The given iterator must point to a permutation of
	 * size equal to the number of states in this automaton.  The sequence
	 * pointed to by the iterator will be modified.  Note that renumbering state
	 * 0 to any other number may change the language accepted by this automaton.
	 */
	template<class RandomAccessIterator>
	void renumberStates(RandomAccessIterator states) {
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
	}

	/**
	 * Renumbers states and symbols.  After this method returns, state i is
	 * numbered states[i] and symbol i is numbered symbols[i].  Both iterators
	 * must point to permutations of the appropriate size.
	 */
	template<class RandomAccessIterator1, class RandomAccessIterator2>
	void renumber(RandomAccessIterator1 states, RandomAccessIterator2 symbols) {
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
	void prepareForEquals() {
		for (auto& ts : transitions_)
			//We shouldn't have two Transitions with the same destination, so we
			//sort only on next_.
			std::sort(ts.begin(), ts.end(), [](Transition a, Transition b){return a.next_ < b.next_;});
		assert(preparedForEquals());
	}

private:
	bool preparedForEquals() const {
		return std::all_of(transitions_.begin(), transitions_.end(), [](const auto& ts) {
			return std::is_sorted(ts.begin(), ts.end(), [](Transition a, Transition b) {
				return a.next_ < b.next_;
			});
		});
	}

public:
	/**
	 * Compares this automaton with another for structural equality.  Call
	 * prepareForEquals() on both automata first.
	 */
	bool operator==(const Automaton& other) const {
		assert(preparedForEquals());
		assert(other.preparedForEquals());
		return std::tie(accept_, transitions_) == std::tie(other.accept_, other.transitions_);
	}
	/**
	 * Compares this automaton with another for structural inequality.  Call
	 * prepareForEquals() on both automata first.
	 */
	bool operator!=(const Automaton& other) const {
		return !(*this == other);
	}

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

public:
	/**
	 * Copies all states from the given automaton into this automaton, adjusting
	 * transition numbers as required.  This does not add any transitions to the
	 * newly-copied states.
	 * @return the number of states of this automaton before appending;
	 * equivalently, the number of the first inserted state (if any)
	 */
	state_type append(const Automaton& b) {
		//TODO: this may result in pathological behavior if we're appending
		//repeatedly, each time allocating "just enough" instead of e.g. doubling
		reserve(size() + b.size());
		state_type base = static_cast<state_type>(transitions_.size());
		for (const auto& t : b.transitions_) {
			transitions_.push_back(t);
			for (Transition& nt : transitions_.back())
				nt.next_ += base;
		}
		for (std::size_t p = 0; p < b.size(); ++p)
			accept_.push_back(b.accept_[p]);
		deterministic_ &= b.deterministic();
		return base;
	}



	/**
	 * Adds the given transitions to this automaton.
	 * @return true if the automaton changed, false if all transitions were
	 * already present
	 */
	bool addTrans(state_type from, symbol_mask_type symbols, state_type to) {
		for (Transition& t : transitions_[from])
			if (t.next_ == to) {
				auto before = t.symbols_;
				t.symbols_ |= symbols;
				if (t.symbols_ == before)
					return false;
				if (deterministic_ && !isStateDeterministic(from))
					deterministic_ = false;
				return true;
			}
		transitions_[from].push_back(Transition(to, symbols));
		if (deterministic_ && !isStateDeterministic(from))
			deterministic_ = false;
		return true;
	}

	/**
	 * Returns a mask of the symbols for which the given state has outgoing
	 * transitions.
	 * @return a mask of the symbols for which the given state has outgoing
	 * transitions.
	 */
	symbol_mask_type outgoing(state_type state) const {
		symbol_mask_type mask;
		for (const Transition& t : transitions_[state])
			mask |= t.symbols_;
		return mask;
	}

	/**
	 * Returns the states directly reachable from the given state.
	 * @return the states directly reachable from the given state
	 */
	small_vector<state_type, 4> destinations(state_type state) const {
		small_vector<state_type, 4> dest;
		for (const Transition& t : transitions_[state])
			dest.push_back(t.next_);
#ifndef NDEBUG
		//addTrans enforces we don't have duplicate transitions; check that here.
		//Use a copy to avoid changing the semantics of debug and release builds
		//(though logically it shouldn't matter...).
		small_vector<state_type, 4> destcopy(dest);
		std::sort(destcopy.begin(), destcopy.end());
		assert(std::adjacent_find(dest.begin(), dest.end()) == dest.end());
#endif
		return dest;
	}

	/**
	 * Returns true iff the given state transitions to at most one state on
	 * every symbol.
	 */
	bool isStateDeterministic(state_type state) const {
		for (symbol_type s = 0; s < AlphabetSize; ++s)
			if (step(state, s).size() > 1)
				return false;
		return true;
	}

	/**
	 * Returns a set of the live states of this automaton.  A state is live iff
	 * it is contained in a path from the initial state to an accept state.
	 * @return the set of live states
	 */
	google::dense_hash_set<state_type> liveStates() const {
		//This set will be used as the visited set for the forward search, then
		//reused as the live set for the backward search.
		google::dense_hash_set<state_type> live(static_cast<state_type>(size()));
		live.set_empty_key(static_cast<state_type>(size()));

		//TODO: there's not yet a good way to use std::sort on separate vectors
		//See http://stackoverflow.com/q/13840998/3614835.
		std::vector<std::pair<state_type, state_type>> inverseEdgelist;
		//Exact sizing (counting transitions) is probably not worth it, but we
		//know there's at least this much.
		inverseEdgelist.reserve(size());

		std::stack<state_type> nexts;
		nexts.push(0);
		live.insert(0);
		while (!nexts.empty()) {
			state_type n = nexts.top();
			nexts.pop();
			for (const Transition& t : transitions_[n])
				inverseEdgelist.push_back({t.next_, n});
			for (state_type next : destinations(n))
				if (live.insert(next).second)
					nexts.push(next);
		}

		//At this point, inverseEdgelist is complete for all reachable states.
		std::sort(inverseEdgelist.begin(), inverseEdgelist.end());
		std::vector<state_type> inverse;
		inverse.reserve(inverseEdgelist.size());
		std::transform(inverseEdgelist.begin(), inverseEdgelist.end(),
			std::back_inserter(inverse), [](const auto& p){return p.second;});
		std::vector<decltype(inverse)::iterator> inverseIdx;
		inverseIdx.reserve(size() + 1);
		inverseIdx.push_back(inverse.begin());
		decltype(inverseEdgelist)::iterator edgelistPos = inverseEdgelist.begin();
		for (state_type s = 0; s < size(); ++s) {
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
		//the accept flag for those states, but liveStates() is const.)
		google::dense_hash_set<state_type> unreachableAccepts;
		unreachableAccepts.set_empty_key(static_cast<state_type>(size()));
		for (auto s = accept_.find_first(); s < accept_.size(); s = accept_.find_next(s)) {
			state_type state = static_cast<state_type>(s);
			if (!live.count(state))
				unreachableAccepts.insert(state);
		}
		live.clear_no_resize();
		assert(nexts.empty());
		for (auto s = accept_.find_first(); s < accept_.size(); s = accept_.find_next(s)) {
			assert(s < size());
			state_type state = static_cast<state_type>(s);
			if (!unreachableAccepts.count(state)) {
				live.insert(state);
				nexts.push(state);
			}
		}
		while (!nexts.empty()) {
			state_type n = nexts.top();
			nexts.pop();
			for (state_type next : inverseDestinations(n))
				if (live.insert(next).second)
					nexts.push(next);
		}
		return live;
	}

private:
	class HopcroftMinimizer final {
	public:
		HopcroftMinimizer(Automaton& a) : a_(a), partitions_(a.size()), partitionBounds_(),
				//TODO: now that the automaton isn't total, inv_ should be
				//allocated after building the inverse edge list, so that it can
				//be sized just right.
				stateToPartition_(a.size()), inv_(a.size() * AlphabetSize),
				invStart_(a.size() * (AlphabetSize+1)), L_(), inL_(a.size()),
				move_(a.size()), moveSize_(), suspects_() {}

		void minimize() {
			if (!buildInverseAndInitializePartitions()) return;
			initializeWaitingSet();
			while (!L_.empty()) {
				auto pair = remove();
				collect(pair.first, pair.second);
				refine();
				//When we get here, L_ is nearly empty and processing each pair
				//is cheap (because partitions are singletons), so it may not be
				//worth checking in this loop.  If not, we definitely want to
				//check in finish() to avoid copying a bunch for no reason.
				if (partitionBounds_.size() == a_.size())
					return;
			}
			finish();
		}
	private:
		Automaton& a_;
		//Every partition contains at least one state, so there can only be as
		//many states as partitions.
		dynarray<state_type> partitions_;
		std::vector<std::pair<state_type, state_type>> partitionBounds_;
		//first is the partition, second is the index into partitions_
		dynarray<std::pair<state_type, state_type>> stateToPartition_;
		dynarray<state_type> inv_;
		dynarray<std::size_t> invStart_;
		std::queue<std::pair<state_type, symbol_type>> L_;
		dynarray<symbol_mask_type> inL_;
		dynarray<state_type> move_;
		std::vector<state_type> moveSize_;
		std::vector<state_type> suspects_;

		/**
		 * @return true if we should continue; flase if the automaton is the
		 * trivial empty-language or all-strings automaton, in which case we're
		 * already done
		 */
		bool buildInverseAndInitializePartitions() {
			struct InverseEntry {
				state_type source;
				symbol_type symbol;
				state_type target;
				bool operator==(const InverseEntry& other) const {
					return source == other.source &&
							symbol == other.symbol &&
							target == other.target;
				}
				bool operator!=(const InverseEntry& other) const {
					return !(*this == other);
				}
				bool operator<(const InverseEntry& other) const {
					return std::tie(target, symbol, source) < std::tie(other.target, other.symbol, other.source);
				}
			};

			std::vector<InverseEntry> edgelist;
			edgelist.reserve(a_.size() * AlphabetSize + 1);
			bool crashed = false;
			unsigned int nonfinalIdx = 0, finalIdx = static_cast<unsigned int>(partitions_.size() - 1);
			for (state_type s = 0; s < a_.size(); ++s) {
				for (symbol_type a = 0; a < AlphabetSize; ++a) {
					auto target = a_.step(s, a);
					assert(target.size() <= 1 && "nondeterministic?");
					if (target.empty())
						crashed = true;
					else
						edgelist.push_back(InverseEntry{s, a, target.front()});
				}

				if (a_.accept_[s])
					partitions_[finalIdx--] = s;
				else
					partitions_[nonfinalIdx++] = s;
			}
			edgelist.push_back(InverseEntry{std::numeric_limits<state_type>::max(), std::numeric_limits<symbol_type>::max(), std::numeric_limits<state_type>::max()});

			assert(nonfinalIdx == finalIdx+1 && "didn't partition all the states somehow");
			if (nonfinalIdx == partitions_.size()) {
				a_ = empty<AlphabetSize>();
				return false;
			} else if ((finalIdx+1) == 0U && !crashed) {
				a_ = all<AlphabetSize>();
				return false;
			}

			if (crashed) {
				//Because we didn't totalize, we need to manually partition
				//crashing vs. non-crashing on each symbol.
				std::vector<typename decltype(partitions_)::iterator> bounds = {
					partitions_.begin(), partitions_.begin()+nonfinalIdx, partitions_.end()
				}, newbounds;
				for (symbol_type s = 0; s < AlphabetSize; ++s) {
					newbounds.clear();
					for (typename decltype(bounds)::size_type i = 0; i < bounds.size() - 1; ++i) {
						newbounds.push_back(bounds[i]);
						newbounds.push_back(std::partition(bounds[i], bounds[i+1], [this, s](state_type state) {
							//TODO: we're really just checking if we crash; may be
							//worth adding stepDeterministic or crashes or something
							//else that stops at the first valid transition
							return a_.step(state, s).empty();
						}));
						newbounds.push_back(bounds[i+1]);
					}
					newbounds.erase(std::unique(newbounds.begin(), newbounds.end()), newbounds.end());
					bounds.swap(newbounds);
				}
				partitionBounds_.reserve(bounds.size()-1);
				for (state_type p = 0; p < bounds.size()-1; ++p)
					partitionBounds_.push_back({bounds[p] - partitions_.begin(), bounds[p+1] - partitions_.begin()});
				moveSize_.resize(partitionBounds_.size(), 0);
			} else {
				partitionBounds_.push_back({0, nonfinalIdx});
				partitionBounds_.push_back({nonfinalIdx, static_cast<state_type>(partitions_.size())});
				moveSize_.push_back(0);
				moveSize_.push_back(0);
			}

			for (state_type p = 0; p < partitionBounds_.size(); ++p) {
				auto bounds = partitionBounds_[p];
				//Sort for locality when accessing stateToPartition_.
				std::sort(partitions_.begin() + bounds.first, partitions_.begin() + bounds.second);
				for (state_type i = bounds.first; i != bounds.second; ++i)
					stateToPartition_[partitions_[i]] = {p, i};
			}

			//TODO: use a parallel sort (beyond a size threshold)
			std::sort(edgelist.begin(), edgelist.end());
			std::size_t invEltsIdx = 0;
			for (state_type stateIdx = 0; stateIdx < a_.size(); ++stateIdx) {
				for (symbol_type symbolIdx = 0; symbolIdx < AlphabetSize; ++symbolIdx) {
					invStart_[stateIdx * (AlphabetSize+1) + symbolIdx] = invEltsIdx;
					while (edgelist[invEltsIdx].target == stateIdx &&
							edgelist[invEltsIdx].symbol == symbolIdx) {
						inv_[invEltsIdx] = edgelist[invEltsIdx].source;
						++invEltsIdx;
					}
				}
				invStart_[stateIdx * (AlphabetSize+1) + AlphabetSize] = invEltsIdx;
			}
			//TODO: we can reassert this when inv_ is lazily sized
//			assert(invEltsIdx == inv_.size());
			return true;
		}

		void initializeWaitingSet() {
			//For each symbol, add all but the largest partition to the waiting set.
			//TODO: factor this into max_element_transform algorithm
			state_type maxPartition = 0, maxPartitionSize = partitionSize(0);
			for (state_type p = 1; p < partitionBounds_.size(); ++p)
				if (partitionSize(p) > maxPartitionSize) {
					maxPartitionSize = partitionSize(p);
					maxPartition = p;
				}

			for (symbol_type i = 0; i < AlphabetSize; ++i)
				for (state_type p = 0; p < partitionBounds_.size(); ++p)
					if (p != maxPartition)
						add(p, i);
		}

		void collect(state_type part, symbol_type symbol) {
			checkRep();
			suspects_.clear();
			for (state_type target : partition(part))
				for (state_type source : inverseStep(target, symbol)) {
					state_type invPart = stateToPartition_[source].first;
					//TODO: if partitionSize(invPart) == 1, we shouldn't bother
					//adding it to suspects (and checking it later).  This might
					//be important for no-op re-minimization of large automata,
					//to avoid unnecessarily growing suspects_.  (Test first.)
					if (moveSize_[invPart] == 0) //only add to suspects if not already present
						suspects_.push_back(invPart);
					move_[partitionBounds_[invPart].first + (moveSize_[invPart]++)] = source;
					assert(moveSize_[invPart] <= partitionSize(invPart));
				}
			checkRep();
		}

		void refine() {
			checkRep();
			for (state_type part : suspects_) {
				if (moveSize_[part] < partitionSize(part)) {
					state_type newPart = split(part);
					//This is done unconditionally outside this if.
//					moveSize_[part] = 0;
					for (symbol_type symbol = 0; symbol < AlphabetSize; ++symbol)
						if (contains(part, symbol))
							add(newPart, symbol);
						else
							addBetter(part, newPart, symbol);
				}
				//In Knuutila's paper, cls_head[B].counter is only cleared when
				//refinement actually happens.  I think that's wrong, because
				//just because we couldn't refine with respect to one state-symbol
				//pair doesn't mean we won't with another, but not clearing
				//counter prevents this partition from entering suspects again.
				moveSize_[part] = 0;
			}
			checkRep();
		}

		state_type split(state_type part) {
			checkRep();
			state_type newPart = static_cast<state_type>(partitionBounds_.size());
			//swap states in move[part] into the front of partitions[part]
			for (state_type i = 0; i < moveSize_[part]; ++i) {
				state_type victimIdx = partitionBounds_[part].first + i;
				state_type beneficiaryState = move_[victimIdx];
				state_type beneficiaryIdx = stateToPartition_[beneficiaryState].second;
				state_type victimState = partitions_[victimIdx];
				assert(stateToPartition_[victimState].second == victimIdx);
				std::swap(partitions_[victimIdx], partitions_[beneficiaryIdx]);
				std::swap(stateToPartition_[victimState].second, stateToPartition_[beneficiaryState].second);
				stateToPartition_[beneficiaryState].first = newPart;
			}
			state_type oldPartOldStart = partitionBounds_[part].first;
			partitionBounds_[part].first += moveSize_[part];
			partitionBounds_.push_back({oldPartOldStart, partitionBounds_[part].first});
			//partitionBounds_[part].second stays where it is
			moveSize_.push_back(0);
			checkRep();
			return newPart;
		}

		void finish() {
			state_type initialStatePart = stateToPartition_[0].first;
			//Free memory.
			stateToPartition_.clear();
			inv_.clear();
			invStart_.clear();
			decltype(L_)().swap(L_);
			inL_.clear();
			moveSize_.clear();
			moveSize_.shrink_to_fit();
			suspects_.clear();
			suspects_.shrink_to_fit();

			Automaton<AlphabetSize> newA;
			newA.transitions_.resize(partitionBounds_.size());
			newA.accept_.resize(partitionBounds_.size());
			state_type newstate = 1; //0 handled specially
			//Pick an exemplar from each partition and build a renumbering map.
			//If the partition contains 0 (the initial state), we must renumber
			//to 0, but otherwise we can assign an arbitrary new number.
			for (decltype(partitionBounds_.size()) i = 0; i < partitionBounds_.size(); ++i) {
				state_type beneficiary = i == initialStatePart ? 0 : newstate++;
				for (state_type state : partition(static_cast<state_type>(i)))
					//Reuse move_ as the renumbering map
					move_[state] = beneficiary;
				//TODO: choose exemplars with few transitions to speed up renumbering?
				state_type victim = partitions_[partitionBounds_[i].first];
				newA.transitions_[beneficiary] = std::move(a_.transitions_[victim]);
				newA.accept_[beneficiary] = a_.accept_[victim];
			}

			//Free more memory.
			partitions_.clear();
			partitionBounds_.clear();
			partitionBounds_.shrink_to_fit();
			//We're going to move-assign over these; might as well free them now.
			a_.transitions_.clear();
			a_.transitions_.shrink_to_fit();
			a_.accept_.clear();
			a_.accept_.shrink_to_fit();

			//Renumber and compress redundant transitions.
			for (auto& ts : newA.transitions_) {
				for (Transition& t : ts)
					t.next_ = move_[t.next_];
				//iterate backwards to gracefully erase
				for (auto i = ts.size(); i-- > 0;) {
					//TODO: this is n^2, we may have a problem here
					for (decltype(i) j = 0; j < i; ++j)
						if (ts[i].next_ == ts[j].next_) {
							ts[j].symbols_ |= ts[i].symbols_;
							ts.erase(ts.begin() + i);
							break;
						}
				}
			}
			a_ = std::move(newA);
		}

		boost::iterator_range<const state_type*> partition(state_type partitionIdx) const {
			assert(partitionIdx < partitionBounds_.size());
			auto p = partitionBounds_[partitionIdx];
			return boost::make_iterator_range(&partitions_[0] + p.first, &partitions_[0] + p.second);
		}
		boost::iterator_range<const state_type*> inverseStep(state_type target, symbol_type symbol) const {
			assert(target <= a_.size());
			assert(symbol <= AlphabetSize);
			std::size_t start = target * (AlphabetSize+1) + symbol;
			assert((start + 1) < invStart_.size());
			return boost::make_iterator_range(&inv_[0] + invStart_[start], &inv_[0] + invStart_[start+1]);
		}

		state_type partitionSize(state_type partitionIdx) const {
			assert(partitionIdx < partitionBounds_.size());
			return partitionBounds_[partitionIdx].second - partitionBounds_[partitionIdx].first;
		}

		void addBetter(state_type partA, state_type partB, symbol_type symbol) {
			//This is the |B| criterion.
			int part = (partitionSize(partA) <= partitionSize(partB)) ? partA : partB;
			add(part, symbol);
		}
		void add(state_type part, symbol_type symbol) {
			checkRep();
			assert(!contains(part, symbol));
			L_.push({part, symbol});
			inL_[part][symbol] = true;
			checkRep();
		}
		bool contains(int part, int symbol) const {
			checkRep();
			return inL_[part][symbol];
		}
		std::pair<state_type, symbol_type> remove() {
			checkRep();
			auto pair = L_.front();
			L_.pop();
			inL_[pair.first][pair.second] = false;
			checkRep();
			return pair;
		}

		void checkRep() const {
#ifndef NDEBUG
			//We exit early in the trivial empty/all cases, so we always have at
			//least accept/reject partitions.
			assert(partitionBounds_.size() >= 2);
			for (const auto& p : partitionBounds_)
				assert(p.second - p.first > 0);
			assert(moveSize_.size() == partitionBounds_.size());
			assert(suspects_.size() <= partitionBounds_.size());

			//All states in each partition should have the same accept status.
			//(Established in the very first split.)
			for (state_type i = 0; i < partitionBounds_.size(); ++i)
				for (state_type j = partitionBounds_[i].first; j < partitionBounds_[i].second - 1; ++j)
					assert(a_.accept_[partitions_[j]] == a_.accept_[partitions_[j+1]]);

			//TODO: every element in partitions_ is within a parititionBounds_ element
			//TODO: partitions_ is a permutation of [0..n) (is there a cheap way to check?)
			//TODO: inL is true iff L contains the state-symbol pair (if is cheap, only-if is costly)

			//It's a fixed invariant so maybe not worth checking constantly, but
			//invStart_ should be nondecreasing and contain only valid indices
			//into inv_.
#endif //NDEBUG
		}
	};

	template<class Alphabet, class Callable>
	void enumerateRecurse(std::vector<state_type>& stateStack, std::vector<typename Alphabet::symbol_type>& symbolString, Callable callback) {
		state_type cur = stateStack.back();
		if (accept_[cur])
			callback(symbolString);
		//For large alphabets we're better off walking the bitsets.
		for (symbol_type s = 0; s < AlphabetSize; ++s) {
			auto nexts = step(cur, s);
			assert(nexts.size() <= 1 && "should be deterministic");
			if (nexts.empty())
				continue;
			state_type next = nexts.front();
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

	friend std::ostream& operator<<(std::ostream& o, const Automaton& a) {
		o << a.size() << " states, " << a.numTransitions() << " transitions, "
				<< (a.deterministic() ? "" : "non") << "deterministic\n";
		for (state_type i = 0; i < a.size(); ++i) {
			o << "state " << i << (a.accept_[i] ? " [accept]:\n" : ":\n");
			for (const Transition& t : a.transitions_[i])
				o << "  to " << t.next_ << " on " << t.symbols_ << '\n';
		}
		return o;
	}

	friend class std::hash<Automaton>;
};

/**
 * Returns an Automaton that accepts the empty language.
 */
template<unsigned int N>
Automaton<N> empty() {
	Automaton<N> a;
	a.addState();
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
	return a;
}

/**
 * Returns an Automaton that accepts only the string containing just the given
 * symbol.
 */
template<unsigned int N>
Automaton<N> lit(typename Automaton<N>::symbol_type symbol) {
	Automaton<N> a;
	a.addState();
	a.addState();
	a.setAccept(1);
	a.addTrans(0, symbol, 1);
	return a;
}

//TODO: varargs lit, iterator-range lit(?)

template<unsigned int N, class ForwardIterator>
Automaton<N> cat(ForwardIterator begin, ForwardIterator end) {
	if (begin == end)
		//the empty string is the identity element for concatenation
		return epsilon<N>();

	typename Automaton<N>::state_type totalStates = 0;
	for (ForwardIterator i = begin; i != end; ++i)
		totalStates += i->state_size();

	Automaton<N> a;
	a.reserve(totalStates);
	for (ForwardIterator i = begin; i != end; ++i) {
		typename Automaton<N>::state_type base = a.append(*i);
		//Wire the previous automaton's accept states to the current initial
		//state (base), modifying them to not accept.
		//TODO: addEpsilon may cause p to become accepting again, so we have
		//to scan from 0 each time.  We should probably keep a set of
		//accepting /state indices to avoid the repeated scanning.
		for (typename Automaton<N>::state_type p = 0; p < base; ++p) {
			if (a.accept(p)) {
				a.setAccept(p, false);
				a.addEpsilon(p, base);
			}
		}
	}
	return a;
}
template<unsigned int N>
Automaton<N> cat(std::initializer_list<Automaton<N>> list) {
	return cat<N>(list.begin(), list.end());
}

template<unsigned int N, class ForwardIterator>
Automaton<N> alt(ForwardIterator begin, ForwardIterator end) {
	if (begin == end)
		return {};

	typename Automaton<N>::state_type totalStates = 0;
	for (ForwardIterator i = begin; i != end; ++i)
		totalStates += i->state_size();

	Automaton<N> a;
	a.reserve(totalStates);
	//initial state that transitions to the individual machines' states
	a.addState();
	for (ForwardIterator i = begin; i != end; ++i) {
		typename Automaton<N>::state_type base = a.append(*i);
		a.addEpsilon(0, base);
	}
	return a;
}
template<unsigned int N>
Automaton<N> alt(std::initializer_list<Automaton<N>> list) {
	return alt<N>(list.begin(), list.end());
}

template<unsigned int N>
Automaton<N> conj(const Automaton<N>& left, const Automaton<N>& right) {
	return Automaton<N>::conj(left, right);
}
//TODO: we could add vararg/iterator-range overloads of conj for convenience,
//but we know from experience actually doing a multi-way conj is worse

template<unsigned int N>
Automaton<N> star(const Automaton<N>& b) {
	Automaton<N> a;
	a.reserve(b.size() + 1);
	a.addState();
	a.setAccept(0);
	typename Automaton<N>::state_type base = a.append(b);
	a.addEpsilon(0, 1);
	for (typename Automaton<N>::state_type p = base; p < a.size(); ++p)
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
	return cat<N>(boost::make_indirect_iterator(v.begin()),
					boost::make_indirect_iterator(v.end()));
}
template<unsigned int N>
Automaton<N> nOrMore(const Automaton<N>& a, unsigned int min) {
	if (min == 0) return star(a);
	std::vector<const Automaton<N>*> v(min, &a);
	Automaton<N> rest = star(a);
	v.push_back(&rest);
	return cat<N>(boost::make_indirect_iterator(v.begin()),
					boost::make_indirect_iterator(v.end()));
}
template<unsigned int N>
Automaton<N> range(const Automaton<N>& a, unsigned int min, unsigned int max) {
	assert(max > min);
	//TODO: inlining nCopies would save a copy
	Automaton<N> ret = nCopies(a, min);
	ret.reserve(max * a.size());
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
	for (typename Automaton<N>::state_type s = 0; s < a.size(); ++s)
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
	return Automaton<N>::shuffleAccept(left, right);
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

} //namespace automaton

namespace std {
template<unsigned int N>
struct hash<automaton::Automaton<N>> {
	size_t operator()(const automaton::Automaton<N>& a) const {
		size_t h = 13;
		h = h * 31 + a.size();
		for (const auto& ts : a.transitions_) {
			h = h * 31 + ts.size();
			for (auto t : ts) {
				h = h * 31 + t.next_;
				h = h * 31 + std::hash<automaton::bitset<N>>()(t.symbols_);
			}
		}
		//punt on accept_ for now
		return h;
	}
};
} //namespace std

#endif /* AUTOMATON_HPP */

