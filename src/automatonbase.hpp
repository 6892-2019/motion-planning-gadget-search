/*
 * File:   automatonbase.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 17, 2017, 4:56 PM
 */

#ifndef AUTOMATONBASE_HPP
#define AUTOMATONBASE_HPP

#include <iosfwd>
#include <functional>
#include <utility>
#include "algoutils.hpp"
#include "linear_set.hpp"
#include "numutils.hpp"

namespace automaton {

class AutomatonBase;

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

	AutomatonBase();
	virtual ~AutomatonBase();
	AutomatonBase(const AutomatonBase&);
	AutomatonBase(AutomatonBase&&);
	AutomatonBase& operator=(const AutomatonBase&);
	AutomatonBase& operator=(AutomatonBase&&);

	/**
	 * @return the number of states in this automaton
	 */
	virtual state_type state_size() const = 0;
	/**
	 * @return the number of accepting states in this automaton
	 */
	virtual state_type accept_size() const;
	/**
	 * @return the alphabet size of this automaton
	 */
	virtual symbol_type alphabet_size() const = 0;
	/**
	 * @return the number of symbols that appear in transitions in this automaton
	 */
	virtual symbol_type active_alphabet_size() const;
	/**
	 * Returns the number of edges in this automaton.  This is the number of
	 * distinct (from, to) pairs in this automaton.
	 * @return the number of edges in this automaton
	 */
	virtual std::size_t edge_size() const;
	/**
	 * Returns the number of transitions in this automaton.  This is the number
	 * of (from, on, to) triples in this automaton.
	 * @return the number of transitions in this automaton
	 */
	virtual std::size_t transition_size() const;

	/**
	 * @return true iff this automaton is known to be deterministic
	 */
	virtual bool deterministic() const = 0;
	/**
	 * @return true iff this automaton is known to be minimal
	 */
	virtual bool minimal() const = 0;
	/**
	 * @return true iff this automaton is known to be canonical
	 */
	virtual bool canonical() const = 0;

	virtual bool accept(state_type state) const = 0;
	virtual void for_each_accept(std::function<void(state_type)> action) const;
	/**
	 * Returns the possible next states of the automaton when reading the given
	 * symbol in the given current state.  The returned set is empty if the
	 * automaton crashes.
	 */
	virtual StateSet step(state_type state, symbol_type symbol) const = 0;
	/**
	 * Returns the next state of the automaton when reading the given symbol in
	 * the given current state, or nullopt if the automaton crashed.  If the
	 * automaton is nondeterministic, the behavior is undefined.
	 */
	virtual std::optional<state_type> stepDeterministic(state_type state, symbol_type symbol) const;

	/**
	 * @return a set containing the symbols appearing on transitions from the
	 * given state (possibly empty)
	 */
	virtual SymbolSet outgoing(state_type state) const;
	/**
	 * @return a set containing the states directly reachable from the given
	 * state (possibly empty)
	 */
	virtual StateSet destinations(state_type state) const;

	/**
	 * Calls the given function once for each state directly reachable from the
	 * given state, in an arbitrary order.
	 */
	virtual void for_each_destination(state_type state, std::function<void(state_type)> action) const;

	/**
	 * Calls the given function once for each transition out of the given state,
	 * in an arbitrary order.
	 */
	virtual void for_each_transition(state_type state, std::function<void(symbol_type, state_type)> action) const;

	/**
	 * Returns a set of the labels on the edge between from and to.  This set is
	 * empty if there is no edge between from and to.
	 *
	 * labels is in some sense the orthogonal operation to step: step tells
	 * where a symbol leads to, while label tells what symbols lead to a
	 * particular place.
	 */
	virtual SymbolSet labels(state_type from, state_type to) const;

	/**
	 * Calls the given function once for each transition in this automaton, in
	 * an arbitrary order.
	 */
	virtual void for_each_transition(std::function<void(state_type, symbol_type, state_type)> action) const;

	range_for_pair<detail::EdgeRangeFront, detail::EdgeRangeSentinel> edges(state_type from) const;

	/**
	 * @return the symbols that appear in transitions in this automaton
	 */
	virtual SymbolSet activeAlphabet() const;

	/**
	 * Runs this automaton on the given string.
	 * @returns true iff the machine accepts the given string of symbol indices
	 */
	bool run(std::initializer_list<unsigned int> string) const;
	/**
	 * Runs this automaton on the given string.
	 * @returns true iff the machine accepts the given string of symbol indices
	 */
	template<class InputIterator, class = std::void_t<typename std::iterator_traits<InputIterator>::iterator_category>>
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
		return std::any_of(current.begin(), current.end(), [this](state_type s){return accept(s);});
	}
	/**
	 * Runs this automaton on the given string.
	 * @returns true iff the machine accepts the given string of symbol indices
	 */
	template<typename... Symbols, std::enable_if_t<
		vta::are_same<symbol_type, Symbols...>::value || vta::are_same<std::make_signed_t<symbol_type>, Symbols...>::value,
		int> = 0>
	bool run(Symbols... string) const {
		//Breadth-first search.
		std::unordered_set<state_type> current, next;
		current.insert(0); //TODO: assuming 0 is the initial state
		vta::map([&](auto symbol) {
			for (state_type c : current)
				for (state_type n : this->step(c, numeric_cast<symbol_type>(symbol)))
					next.insert(n);
			std::swap(current, next);
			next.clear();
		})(string...);
		return std::any_of(current.begin(), current.end(), [this](state_type s){return accept(s);});
	}

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
	/**
	 * Add transitions to this automaton.
	 * @return true if the automaton changed, false if every transition was
	 * already present
	 */
	virtual bool addTrans(state_type from, SymbolSet on, state_type to) = 0;
	virtual bool setAccept(state_type state, bool accepts = true) = 0;
	/**
	 * Removes all states and transitions from this automaton.
	 */
	virtual void clear() = 0;

	//TODO: we can't use symbol_mask_type, but maybe we'll want an opaque token
	//type to allow e.g. copying a set of transitions

	/**
	 * Computes a hash of this object suitable for comparison against other
	 * hashes computed by this method, across all AutomatonBase implementations.
	 * Implementations may expose more efficient hashes valid only in their
	 * subhierarchy.
	 *
	 * Calling this method when canonical() returns false is usually an error.
	 */
	std::size_t hash() const;

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

bool operator==(const AutomatonBase& left, const AutomatonBase& right);
inline bool operator!=(const AutomatonBase& left, const AutomatonBase& right) {
	return !(left == right);
}

std::ostream& operator<<(std::ostream& o, const AutomatonBase& a);

using SymbolSet = AutomatonBase::SymbolSet;
using StateSet = AutomatonBase::StateSet;

namespace detail {
using state_type = AutomatonBase::state_type;
using symbol_type = AutomatonBase::symbol_type;

class EdgeRangeSentinel {
	const AutomatonBase* parent_;
	AutomatonBase::state_type from_;
	EdgeRangeSentinel(const AutomatonBase* parent, AutomatonBase::state_type from) : parent_(parent), from_(from) {}
	friend AutomatonBase;
	friend bool operator==(const EdgeRangeFront& left, EdgeRangeSentinel right);
};

class EdgeRangeFront {
	EdgeRangeFront(const AutomatonBase* parent, AutomatonBase::state_type from) :
			parent_(parent), from_(from), dests_(parent_->destinations(from_)), idx_(0), cur_(update()) {}
	const AutomatonBase* parent_;
	AutomatonBase::state_type from_;
	StateSet dests_;
	StateSet::size_type idx_;
	std::pair<SymbolSet, AutomatonBase::state_type> cur_;
	std::pair<SymbolSet, AutomatonBase::state_type> update() {
		if (idx_ < dests_.size())
			return {parent_->labels(from_, dests_[idx_]), dests_[idx_]};
		return {{}, std::numeric_limits<AutomatonBase::state_type>::max()}; //not dereferenceable
	}
public:
	const auto& operator*() const {
		return cur_;
	}
	auto& operator++() {
		++idx_;
		cur_ = update();
		return *this;
	}
	friend AutomatonBase;
	friend bool operator==(const EdgeRangeFront& left, EdgeRangeSentinel right);
};

inline bool operator==(const EdgeRangeFront& left, EdgeRangeSentinel right) {
		assert(left.parent_ == right.parent_ && "edge ranges from different parents");
		assert(left.from_ == right.from_ && "edge ranges from different states");
		return left.idx_ == left.dests_.size();
}
inline bool operator!=(const EdgeRangeFront& left, EdgeRangeSentinel right) {
	return !(left == right);
}
} //namespace detail

inline range_for_pair<detail::EdgeRangeFront, detail::EdgeRangeSentinel> AutomatonBase::edges(state_type from) const {
	return make_range_for_pair(detail::EdgeRangeFront(this, from), detail::EdgeRangeSentinel(this, from));
}


class WorkingAutomaton : public AutomatonBase {
public:
	WorkingAutomaton();
	~WorkingAutomaton();
	WorkingAutomaton(const WorkingAutomaton&);
	WorkingAutomaton(WorkingAutomaton&&);
	//We shouldn't be calling clone() directly on an Automaton, but see
	//https://stackoverflow.com/a/6925201/3614835 if preserving the concrete
	//type is actually necessary.
	virtual std::unique_ptr<WorkingAutomaton> clone() const = 0;
	WorkingAutomaton& operator=(const WorkingAutomaton&);
	WorkingAutomaton& operator=(WorkingAutomaton&&);
	virtual void removeDeadStates() = 0;
	virtual void totalize() = 0;
	virtual void determinize() = 0;
	virtual void minimize() = 0;
	virtual void canonicalize() = 0;
	virtual void swapStateNumbers(state_type a, state_type b) = 0;
	virtual std::size_t working_hash() const = 0;
	using AutomatonBase::addTrans;
	bool addTrans(state_type from, SymbolSet on, state_type to) override;
	state_type append(const AutomatonBase& b);

	/**
	 * Returns true iff this automaton's language is infinite.  (Not to be
	 * confused with universality, accepting the language of all strings.)
	 *
	 * This function is not const because it needs to call removeDeadStates().
	 * @return true iff this automaton's language is infinite
	 */
	bool infinite();
};

std::unique_ptr<WorkingAutomaton> make_working(unsigned int size);

namespace detail {
//TODO: both automatonbase.cpp and automaton.hpp want this class, but other
//users of automatonbase.hpp don't.  We could move it to its own header, or
//outright replace it with a map actually supporting compute_if_absent.
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
	std::pair<state_type, bool> get_or_add_state(key_type oldstates, AutomatonBase& automaton) {
		//dense_hashtable::find_or_insert is so close to what we want :(
		auto it = map_.find(oldstates);
		if (it != map_.end())
			return {it->second, false};
		auto r = map_.insert({oldstates, automaton.addState()});
		return {r.first->second, true};
	}
private:
	google::dense_hash_map<key_type, state_type, boost::hash<key_type>> map_;
};

//std::unique_ptr<WorkingAutomaton> shuffleAcceptDeterministic(
//		const WorkingAutomaton& left, const WorkingAutomaton& right, unsigned int alphabet_size);
} //namespace detail



/**
 * Computes the accepting shuffle of the given automata.  The accepting
 * shuffle is the language accepted by running both automata in parallel,
 * passing each character to one or the other automaton, switching the
 * active automaton only when both automata are in accepting states.
 */
std::unique_ptr<WorkingAutomaton> shuffleAccept(const WorkingAutomaton& left,
		const WorkingAutomaton& right,
		unsigned int alphabet_size);
/**
 * Computes the accepting shuffle of the given automata.  The accepting
 * shuffle is the language accepted by running both automata in parallel,
 * passing each character to one or the other automaton, switching the
 * active automaton only when both automata are in accepting states.
 */
std::unique_ptr<WorkingAutomaton> shuffleAccept(WorkingAutomaton&& left, WorkingAutomaton&& right,
		unsigned int alphabet_size);

} //namespace automaton

#endif /* AUTOMATONBASE_HPP */

