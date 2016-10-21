/*
 * File:   automaton.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 16, 2016, 4:36 PM
 */

#ifndef AUTOMATON_HPP
#define AUTOMATON_HPP

#include "precompiled.hpp"

namespace automaton {
namespace impl {

using boost::container::small_vector;

template<unsigned int AlphabetSize>
class Automaton {
public:
	using ptr = boost::intrusive_ptr<Automaton>;

	/**
	 * Returns a new Automaton that accepts the empty language.
	 */
	static ptr empty() {
		ptr a = new Automaton;
		a->transitions_.push_back({Transition()});
		a->transitions_.back().back().next_ = 0; //loop back to itself
		a->transitions_.back().back().symbols_.set();
		a->accept_.push_back(false);
		return a;
	}

	/**
	 * Returns a new Automaton that accepts all strings.
	 */
	static ptr all() {
		ptr a = empty();
		a->accept_.set(0);
		return a;
	}

	/**
	 * Returns a new Automaton that accepts the empty string.
	 */
	static ptr epsilon() {
		ptr a = new Automaton();
		a->transitions_.push_back({});
		a->accept_.push_back(true);
		return a;
	}

	/**
	 * Returns a new Automaton that accepts any single character.
	 */
	static ptr any() {
		ptr a = new Automaton;
		a->transitions_.resize(2);
		a->accept_.resize(2);
		//Initial state transitions to the next state on any symbol.
		a->transitions_[0].push_back(Transition());
		a->transitions_[0].back().next_ = 1;
		a->transitions_[0].back().symbols_.set();
		a->accept_[0] = false;
		//Second state is accepting, but has no outgoing transitions.
		a->accept_[1] = true;
		return a;
	}

	/**
	 * Returns a new Automaton that accepts the string containing just the given
	 * character.
	 */
	static ptr lit(unsigned int symbol) {
		ptr a = any();
		a->transitions_[0].back().symbols_.reset();
		a->transitions_[0].back().symbols_.set(symbol);
		return a;
	}

	template<class ForwardIterator>
	static ptr cat(ForwardIterator begin, ForwardIterator end) {
		if (begin == end)
			//the empty string is the identity element for concatenation
			return epsilon();

		std::size_t totalStates = 0;
		for (ForwardIterator i = begin; i != end; ++i)
			totalStates += (*i)->transitions_.size();

		ptr a = new Automaton;
		a->reserve(totalStates);
		for (ForwardIterator i = begin; i != end; ++i) {
			state_type base = a->append(*i);
			//Wire the previous automaton's accept states to the current initial
			//state (base), modifying them to not accept.
			//TODO: addEpsilon may cause p to become accepting again, so we have
			//to scan from 0 each time.  We should probably keep a set of
			//accepting /state indices to avoid the repeated scanning.
			for (state_type p = 0; p < base; ++p) {
				if (a->accept_[p]) {
					a->accept_.reset(p);
					a->addEpsilon(p, base);
				}
			}
		}
		return a;
	}
	static ptr cat(std::initializer_list<ptr> lists) {
		return cat(lists.begin(), lists.end());
	}

	template<class ForwardIterator>
	static ptr alt(ForwardIterator begin, ForwardIterator end) {
		if (begin == end)
			return empty();

		std::size_t totalStates = 1;
		for (ForwardIterator i = begin; i != end; ++i)
			totalStates += (*i)->transitions_.size();

		ptr a = new Automaton;
		a->reserve(totalStates);
		//initial state that transitions to the individual machines' states
		a->addState();
		for (ForwardIterator i = begin; i != end; ++i) {
			state_type base = a->append(*i);
			a->addEpsilon(0, base);
		}
		return a;
	}
	static ptr alt(std::initializer_list<ptr> alternatives) {
		return alt(alternatives.begin(), alternatives.end());
	}

	static ptr conj(ptr left, ptr right) {
		//(left state, right state, new state)
		using state_triple = std::tuple<state_type, state_type, state_type>;
		std::stack<state_triple> worklist;
		//std::hash isn't provided for pair :(
		std::unordered_map<std::pair<state_type, state_type>, state_type,
				boost::hash<std::pair<state_type, state_type>>> newstates;

		ptr a = new Automaton;
		a->addState();
		//TODO: assuming 0 is the initial state
		worklist.push({0, 0, 0});
		newstates[{0, 0}] = 0;

		while (!worklist.empty()) {
			state_type ls, rs, ns;
			std::tie(ls, rs, ns) = worklist.top();
			worklist.pop();
			a->accept_.set(ns, left->accept_[ls] && right->accept_[rs]);

			//This is a bit naive and may need to be revised for large alphabets.
			for (symbol_type s = 0; s < AlphabetSize; ++s) {
				auto leftnexts = left->step(ls, s);
				auto rightnexts = right->step(rs, s);
				for (state_type leftnext : leftnexts)
					for (state_type rightnext : rightnexts) {
						auto it = newstates.find({leftnext, rightnext});
						state_type newnext;
						if (it == newstates.end()) {
							newnext = a->addState();
							newstates[{leftnext, rightnext}] = newnext;
							worklist.push({leftnext, rightnext, newnext});
						} else
							newnext = it->second;
						a->addTrans(ns, s, newnext);
					}
			}
		}

		//TODO: are we sure?
		a->deterministic_ = left->deterministic() && right->deterministic();
		return a;
	}

	static ptr star(ptr b) {
		ptr a = new Automaton;
		a->reserve(1 + b->size());
		a->addState();
		state_type base = a->append(b);
		a->addEpsilon(0, 1);
		for (state_type p = base; p < a->accept_.size(); ++p)
			if (a->accept_.test(p))
				a->addEpsilon(p, 0);
		return a;
	}

	/**
	 * Returns true if this automaton is known to be deterministic.
	 */
	bool deterministic() const {return deterministic_;}

	/**
	 * Returns the number of states in this automaton.
	 */
	std::size_t size() const {return transitions_.size();}
	/**
	 * Returns the number of transitions in this automaton.
	 */
	std::size_t numTransitions() const {
		return std::accumulate(transitions_.begin(), transitions_.end(), static_cast<std::size_t>(0),
				[](std::size_t l, const auto& r) {return l + r.size();});
	}

	/**
	 * Returns true if the machine accepts the given string (as symbol indices)
	 * and false otherwise.
	 */
	bool run(std::initializer_list<unsigned int> string) const {
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
private:
	Automaton() : deterministic_(true) {}

	using symbol_type = unsigned int; //cf. Literal
//	using symbol_mask_type = boost::uint_t<AlphabetSize>::least;
	using symbol_mask_type = std::bitset<AlphabetSize>;
	//We could save space by using a smaller type for small automata, but it's
	//hard to know what size to use before building the automaton.  We'd only
	//save on automata that are already small, so it's not really worth it.
	using state_type = unsigned int;
	struct Transition {
		Transition() = default;
		Transition(state_type next, symbol_mask_type symbols) : next_(next), symbols_(symbols) {}
		state_type next_;
		symbol_mask_type symbols_;
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
	 * Returns the possible next states of the automaton when reading the given
	 * symbol in the given current state.  This may be empty if the machine
	 * crashed.
	 */
	small_vector<state_type, 4> step(state_type current, symbol_type symbol) const {
		small_vector<state_type, 4> next;
		for (const Transition& t : transitions_[current])
			if (t.symbols_[symbol])
				next.push_back(t.next_);
		//TODO: if we have multiple transitions to the same state, we're
		//technically deterministic.  Maybe return a set-like type?
		assert(next.size() <= 1 || !deterministic_);
		return next;
	}

	/**
	 * Reserves space in this automaton for the given number of states.
	 */
	void reserve(std::size_t size) {
		transitions_.reserve(size);
		accept_.reserve(size);
	}

	/**
	 * Copies all states from the given automaton into this automaton, adjusting
	 * transition numbers as required.  This does not add any transitions to the
	 * newly-copied states.
	 * @return the number of states of this automaton before appending;
	 * equivalently, the number of the first inserted state (if any)
	 */
	state_type append(ptr b) {
		//TODO: this may result in pathological behavior if we're appending
		//repeatedly, each time allocating "just enough" instead of e.g. doubling
		reserve(size() + b->size());
		state_type base = static_cast<state_type>(transitions_.size());
		for (const auto& t : b->transitions_) {
			transitions_.push_back(t);
			for (Transition& nt : transitions_.back())
				nt.next_ += base;
		}
		for (std::size_t p = 0; p < b->size(); ++p)
			accept_.push_back(b->accept_[p]);
		deterministic_ &= b->deterministic();
		return base;
	}

	/**
	 * Adds a new state to this automaton.  The state is rejecting and has no
	 * outgoing transitions.
	 */
	state_type addState() {
		state_type s = static_cast<state_type>(transitions_.size());
		transitions_.push_back({});
		accept_.push_back(false);
		return s;
	}

	/**
	 * Add transitions out of from that simulate the presence of an epsilon
	 * transition into to.  (Later modifications of to's transitions will not
	 * result in corresponding updates of from's transitions.)
	 * @return true if the automaton changed, either by adding a transition or
	 * making a non-accepting state an accepting state
	 */
	bool addEpsilon(state_type from, state_type to) {
		bool changed = false;
		if (accept_[from]) {
			changed |= !accept_[to];
			accept_.set(to);
		}
		for (const Transition& t : transitions_[to])
			changed |= addTrans(from, t.symbols_, t.next_);
		return changed;
	}

	/**
	 * Adds a transition to this automaton.
	 * @return true if the automaton changed, false if the transition was
	 * already present
	 */
	bool addTrans(state_type from, symbol_type symbol, state_type to) {
		for (Transition& t : transitions_[from])
			if (t.next_ == to) {
				if (t.symbols_[symbol])
					return false;
				t.symbols_.set(symbol);
				if (!isStateDeterministic(from))
					deterministic_ = false;
				return true;
			}
		transitions_[from].push_back({});
		transitions_[from].back().next_ = to;
		transitions_[from].back().symbols_.set(symbol);
		if (!isStateDeterministic(from))
			deterministic_ = false;
		return true;
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
				if (!isStateDeterministic(from))
					deterministic_ = false;
				return true;
			}
		transitions_[from].push_back(Transition(to, symbols));
		if (!isStateDeterministic(from))
			deterministic_ = false;
		return true;
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

	std::atomic<unsigned int> refcount_;
	friend void intrusive_ptr_add_ref(Automaton* p) noexcept {
		++p->refcount_;
	}
	friend void intrusive_ptr_release(Automaton* p) noexcept {
		if (!(--p->refcount_))
			delete p;
	}
};

} //namespace impl
} //namespace automaton

#endif /* AUTOMATON_HPP */

