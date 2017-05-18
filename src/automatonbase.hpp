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

	virtual ~AutomatonBase();

	/**
	 * @return the number of states in this automaton
	 */
	virtual state_type state_size() const = 0;
	/**
	 * @return the number of accepting states in this automaton
	 */
	virtual state_type accept_size() const = 0;
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
	virtual std::optional<state_type> stepDeterministic(state_type state, symbol_type symbol) const {
		assert(deterministic());
		assert(state < state_size());
		assert(symbol < alphabet_size());
		StateSet nexts = step(state, symbol);
		assert(nexts.size() <= 1);
		return nexts.size() ? std::make_optional(nexts.front()) : std::nullopt;
	}

	/**
	 * @return a set containing the symbols appearing on transitions from the
	 * given state (possibly empty)
	 */
	virtual SymbolSet outgoing(state_type state) const {
		assert(state < state_size());
		SymbolSet ret;
		if (deterministic())
			for (symbol_type a = 0; a < alphabet_size(); ++a)
				if (stepDeterministic(state, a))
					ret.insert_absent(a);
		else
			for (symbol_type a = 0; a < alphabet_size(); ++a)
				for (MAYBE_UNUSED state_type next : step(state, a))
					ret.insert(a);
		return ret;
	}
	/**
	 * @return a set containing the states directly reachable from the given
	 * state (possibly empty)
	 */
	virtual StateSet destinations(state_type state) const {
		assert(state < state_size());
		StateSet ret;
		if (deterministic())
			for (symbol_type a = 0; a < alphabet_size(); ++a)
				if (auto next = stepDeterministic(state, a); next)
					ret.insert(*next);
		else
			for (symbol_type a = 0; a < alphabet_size(); ++a)
				for (state_type next : step(state, a))
					ret.insert(next);
		return ret;
	}

	/**
	 * Calls the given function once for each state directly reachable from the
	 * given state, in an arbitrary order.
	 */
	virtual void for_each_destination(state_type state, std::function<void(state_type)> action) const {
		assert(state < state_size());
		for (state_type dest : destinations(state))
			action(dest);
	}
	/**
	 * Returns a set of the labels on the edge between from and to.  This set is
	 * empty if there is no edge between from and to.
	 *
	 * labels is in some sense the orthogonal operation to step: step tells
	 * where a symbol leads to, while label tells what symbols lead to a
	 * particular place.
	 */
	virtual SymbolSet labels(state_type from, state_type to) const {
		assert(from < state_size());
		assert(to < state_size());
		SymbolSet ret;
		if (deterministic())
			for (symbol_type a = 0; a < alphabet_size(); ++a)
				if (auto next = stepDeterministic(from, a); next && *next == to)
					ret.insert_absent(a);
		else
			for (symbol_type a = 0; a < alphabet_size(); ++a)
				for (MAYBE_UNUSED state_type next : step(from, a))
					if (next == to) {
						ret.insert_absent(a);
						break;
					}
		return ret;
	}

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
	/**
	 * Removes all states and transitions from this automaton.
	 */
	virtual void clear() = 0;

	//TODO: we can't use symbol_mask_type, but maybe we'll want an opaque token
	//type to allow e.g. copying a set of transitions

	//TODO: equality, somehow (false if alphabet sizes disagree)

	//Strictly speaking, this should be private virtual, existing purely to be
	//called from std::hash<AutomatonBase>.  But std::hash is just awkward
	//enough that I'm happy to expose this here.
	virtual std::size_t hash() const = 0;

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

} //namespace automaton

#endif /* AUTOMATONBASE_HPP */

