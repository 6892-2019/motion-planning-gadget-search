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
		a->deterministic_ = true;
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
		a->deterministic_ = true;
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
		a->deterministic_ = true;
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
		a->transitions_.reserve(totalStates);
		a->accept_.resize(totalStates);
		for (ForwardIterator i = begin; i != end; ++i) {
			ptr b = *i;
			//Copy states into a, renumbering around the states that already exist.
			state_type base = static_cast<state_type>(a->transitions_.size());
			for (const auto& t : b->transitions_) {
				a->transitions_.push_back(t);
				for (Transition& nt : a->transitions_.back())
					nt.next_ += base;
			}
			for (std::size_t p = 0, q = base; q < a->transitions_.size(); ++p, ++q)
				a->accept_[q] = b->accept_[p];

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
		a->deterministic_ = false;
		return a;
	}
	static ptr cat(std::initializer_list<ptr> lists) {
		return cat(lists.begin(), lists.end());
	}

	/**
	 * Returns true if this automaton is known to be deterministic.
	 */
	bool deterministic() const {return deterministic_;}

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
	Automaton() {}

	using symbol_type = unsigned int; //cf. Literal
//	using symbol_mask_type = boost::uint_t<AlphabetSize>::least;
	using symbol_mask_type = std::bitset<AlphabetSize>;
	//We could save space by using a smaller type for small automata, but it's
	//hard to know what size to use before building the automaton.  We'd only
	//save on automata that are already small, so it's not really worth it.
	using state_type = unsigned int;
	struct Transition {
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
	 * Add transitions out of from that simulate the presence of an epsilon
	 * transition into to.  (Later modifications of to's transitions will not
	 * result in corresponding updates of from's transitions.)
	 */
	void addEpsilon(state_type from, state_type to) {
		if (accept_[from])
			accept_.set(to);
		transitions_[from].insert(transitions_[from].end(), transitions_[to].begin(), transitions_[to].end());
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

