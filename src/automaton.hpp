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
using google::dense_hash_map;
using google::dense_hash_set;

template<unsigned int AlphabetSize>
class Automaton {
public:
	using ptr = boost::intrusive_ptr<Automaton>;
	using const_ptr = boost::intrusive_ptr<const Automaton>;
private:
	using symbol_type = unsigned int; //cf. Literal
	using symbol_mask_type = automaton::bitset<AlphabetSize>;
	//We could save space by using a smaller type for small automata, but it's
	//hard to know what size to use before building the automaton.  We'd only
	//save on automata that are already small, so it's not really worth it.
	using state_type = unsigned int;
public:

	/**
	 * Returns a new Automaton that accepts the empty language.
	 */
	static ptr empty() {
		ptr a = new Automaton;
		a->addState();
		return a;
	}

	/**
	 * Returns a new Automaton that accepts all strings.
	 */
	static ptr all() {
		ptr a = new Automaton;
		a->addState();
		a->accept_.set(0);
		a->addTrans(0, ~a->outgoing(0), 0);
		return a;
	}

	/**
	 * Returns a new Automaton that accepts the empty string.
	 */
	static ptr epsilon() {
		ptr a = new Automaton();
		a->addState();
		a->accept_[0] = true;
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
	static ptr cat(std::initializer_list<const_ptr> lists) {
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
	static ptr alt(std::initializer_list<const_ptr> alternatives) {
		return alt(alternatives.begin(), alternatives.end());
	}

private:
	//We can't templatize this together with the other maps because dense_hash_map
	//needs set_empty_key.
	class DenseConjMap {
	public:
		DenseConjMap(std::size_t leftSize, std::size_t rightSize) : map_() {
			//silent narrowing conversion: http://stackoverflow.com/q/37928951/3614835
			map_.set_empty_key({leftSize, rightSize});
		}
		void insert(std::pair<state_type, state_type> oldstates, state_type newstate) {
			map_.insert({oldstates, newstate});
		}
		template<class Callable>
		std::pair<state_type, bool> compute_if_absent(std::pair<state_type, state_type> oldstates, Callable newstateProvider) {
			//dense_hashtable::find_or_insert is so close to what we want :(
			auto it = map_.find(oldstates);
			if (it != map_.end())
				return {it->second, false};
			auto r = map_.insert({oldstates, newstateProvider()});
			return {r.first->second, true};
		}
	private:
		//std::hash isn't provided for pair :(
		dense_hash_map<std::pair<state_type, state_type>, state_type,
			boost::hash<std::pair<state_type, state_type>>> map_;
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

	template <class Map>
	static ptr conj_impl(const_ptr left, const_ptr right) {
		//(left state, right state, new state)
		using state_triple = std::tuple<state_type, state_type, state_type>;
		std::stack<state_triple> worklist;
		Map newstates(left->size(), right->size());

		ptr a = new Automaton;
		//TODO: are we sure?
		a->deterministic_ = left->deterministic() && right->deterministic();
		a->addState();
		//TODO: assuming 0 is the initial state
		worklist.push({0, 0, 0});
		newstates.insert({0, 0}, 0);

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
						auto p = newstates.compute_if_absent({leftnext, rightnext}, [&]{return a->addState();});
						if (p.second)
							worklist.push({leftnext, rightnext, p.first});
						a->addTrans(ns, s, p.first);
					}
			}
		}
		return a;
	}

public:
	static ptr conj(const_ptr left, const_ptr right) {
		ptr a = nullptr;
		try {
			a = conj_impl<DenseConjMap>(left, right);
		} catch (std::bad_alloc&) {
			std::cout << "caught bad_alloc: conj_impl<DenseConjMap>" << std::endl;
		}
		if (!a)
			try {
				a = conj_impl<UnorderedConjMap>(left, right);
			} catch (std::bad_alloc&) {
				std::cout << "caught bad_alloc: conj_impl<UnorderedConjMap>" << std::endl;
			}
		if (!a)
			//No try-catch here because there's no further recovery
			a = conj_impl<SparseConjMap>(left, right);
		std::cout << "intersection: " << left->size() << ", " << right->size() << " -> " << a->size() << std::endl;
		a->removeDeadStates();
		return a;
	}

	static ptr star(const_ptr b) {
		ptr a = new Automaton;
		a->reserve(1 + b->size());
		a->addState();
		a->accept_.set(0);
		state_type base = a->append(b);
		a->addEpsilon(0, 1);
		for (state_type p = base; p < a->accept_.size(); ++p)
			if (a->accept_.test(p))
				a->addEpsilon(p, 0);
		return a;
	}
	static ptr plus(const_ptr b) {
		return nOrMore(b, 1);
	}
	static ptr maybe(const_ptr b) {
		return range(b, 0, 1);
	}
	static ptr nCopies(const_ptr b, unsigned int count) {
		std::vector<const_ptr> v(count, b);
		return cat(v.begin(), v.end());
	}
	static ptr nOrMore(const_ptr b, unsigned int min) {
		if (min == 0) return star(b);
		std::vector<const_ptr> v(min, b);
		v.push_back(star(b));
		return cat(v.begin(), v.end());
	}
	static ptr range(const_ptr b, unsigned int min, unsigned int max) {
		assert(max > min);
		ptr a = nCopies(b, min); //TODO: we could save a copy by writing this inline
		a->reserve(max * b->size());
		//like cat, but not clearing the accept states (and so not scanning from
		//state 0 each time)
		state_type lastbase = 0;
		for (unsigned int i = 0; i < max - min; ++i) {
			state_type base = a->append(b);
			for (state_type q = lastbase; q < base; ++q)
				if (a->accept_[q])
					a->addEpsilon(q, base);
			lastbase = base;
		}
		return a;
	}

	static ptr comp(const_ptr b) {
		ptr a = b->clone();
		a->determinize();
		a->totalize();
		a->accept_.flip();
		//TODO: removeDeadTransitions?
		return a;
	}

	ptr clone() const {
		return new Automaton(*this);
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
		dense_hash_set<state_type> visited(static_cast<state_type>(size()));
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
		ptr a = new Automaton;
		a->addState();
		auto iterSucc = newstate.insert(std::make_pair(state_set({0}), 0));
		assert(iterSucc.second);
		worklist.push(&*(iterSucc.first));

		while (!worklist.empty()) {
			auto current = worklist.top();
			worklist.pop();

			a->accept_[current->second] = std::any_of(current->first.begin(), current->first.end(),
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
					it = newstate.insert(std::make_pair(std::move(next), a->addState())).first;
					worklist.push(&*it);
				}
				a->addTrans(current->second, s, it->second);
				assert(a->deterministic());
			}
		}

		*this = std::move(*a);
		assert(deterministic());
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
			*this = std::move(*empty());
			return;
		}
		std::size_t oldsize = size();
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
		std::cout << "removeDeadStates: " << oldsize << " -> " << size() << std::endl;
	}

	void minimize() {
		determinize();
		//The Java library explicitly checks for the all-strings automaton here,
		//but it doesn't seem to be necessary.
		totalize();
		std::size_t oldsize = size();
		HopcroftMinimizer(*this).minimize();
		std::cout << "minimize: " << oldsize << " -> " << size() << std::endl;
		removeDeadStates();
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

private:
	Automaton() : deterministic_(true), refcount_(0) {}
	//copy everything but the refcount
	Automaton(const Automaton& a) : transitions_(a.transitions_), accept_(a.accept_),
			deterministic_(a.deterministic_), refcount_(0) {}
	//move everything but the reference counter (which is tied to the physical object)
	Automaton& operator=(Automaton&& victim) {
		transitions_ = std::move(victim.transitions_);
		accept_ = std::move(victim.accept_);
		deterministic_ = victim.deterministic_;
		return *this;
	}

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
		assert(current < transitions_.size());
		assert(symbol < AlphabetSize);
		small_vector<state_type, 4> next;
		for (const Transition& t : transitions_[current])
			if (t.symbols_[symbol])
				next.push_back(t.next_);
		//We used to assert(next.size() <= 1 || !deterministic_), but the
		//addTrans calls isStateDeterministic calls step (us) when checking
		//whether to clear deterministic_.  If we change the implementation of
		//isStateDeterministic to not actually build the steps, we could add the
		//assert back.
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
	state_type append(const_ptr b) {
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
		if (accept_[to]) {
			changed |= !accept_[from];
			accept_.set(from);
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
		std::sort(dest.begin(), dest.end());
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
	dense_hash_set<state_type> liveStates() const {
		//If for some reason we care about reachable but not live states, we're
		//computing them here of necessity.
		dense_hash_set<state_type> live(static_cast<state_type>(size())), visited(static_cast<state_type>(size()));
		live.set_empty_key(static_cast<state_type>(size()));
		visited.set_empty_key(static_cast<state_type>(size()));
		std::vector<state_type> path;
		std::stack<boost::optional<state_type>> nexts;
		auto markPathLive = [&]() {
			//Add path elements to the live set until we find a state
			//already in the live set, after which all previous states
			//have already been marked live.
			for (auto i = path.rbegin(); i != path.rend(); ++i)
				if (!live.insert(*i).second)
					return;
		};

		nexts.push(boost::make_optional(0U));
		while (!nexts.empty()) {
			boost::optional<state_type> n = nexts.top();
			nexts.pop();
			if (n) {
				//We might have already visited this state while it was waiting
				//on the stack.
				if (!visited.insert(*n).second) {
					if (live.find(*n) != live.end())
						markPathLive();
					continue;
				}
				path.push_back(*n);
				if (accept_[path.back()])
					markPathLive();
				nexts.push(boost::optional<state_type>(boost::none));
				for (state_type next : destinations(path.back()))
					nexts.push(boost::make_optional(next));
			} else
				path.pop_back();
		}
		return live;
	}

	class HopcroftMinimizer final {
	public:
		HopcroftMinimizer(Automaton& a) : a_(a), partitions_(a.size()), partitionBounds_(),
				stateToPartition_(a.size()), inv_(a.size() * AlphabetSize),
				invStart_(a.size() * (AlphabetSize+1)), L_(), inL_(a.size()),
				move_(a.size()), moveSize_(), suspects_() {}

		void minimize() {
			if (!buildInverseAndInitializePartitions()) return;
			for (symbol_type i = 0; i < AlphabetSize; ++i)
				addBetter(0, 1, i);
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
			unsigned int nonfinalIdx = 0, finalIdx = static_cast<unsigned int>(partitions_.size() - 1);
			for (state_type s = 0; s < a_.size(); ++s) {
				for (symbol_type a = 0; a < AlphabetSize; ++a) {
					auto target = a_.step(s, a);
					assert(target.size() <= 1 && "nondeterministic?");
					if (!target.empty())
						edgelist.push_back(InverseEntry{s, a, target.front()});
				}

				if (a_.accept_[s]) {
					stateToPartition_[s] = {1, finalIdx};
					partitions_[finalIdx--] = s;
				} else {
					stateToPartition_[s] = {0, nonfinalIdx};
					partitions_[nonfinalIdx++] = s;
				}
			}
			edgelist.push_back(InverseEntry{std::numeric_limits<state_type>::max(), std::numeric_limits<symbol_type>::max(), std::numeric_limits<state_type>::max()});

			assert(nonfinalIdx == finalIdx+1 && "didn't partition all the states somehow");
			if (nonfinalIdx == partitions_.size()) {
				a_ = std::move(*Automaton<AlphabetSize>::empty());
				return false;
			} else if ((finalIdx+1) == 0U) {
				a_ = std::move(*Automaton<AlphabetSize>::all());
				return false;
			}

			assert(edgelist.size() == (a_.size() * AlphabetSize + 1) && "automaton not total");
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
			assert(invEltsIdx == inv_.size());

			partitionBounds_.push_back({0, nonfinalIdx});
			partitionBounds_.push_back({nonfinalIdx, static_cast<state_type>(partitions_.size())});
			moveSize_.push_back(0);
			moveSize_.push_back(0);
			return true;
		}

		void collect(state_type part, symbol_type symbol) {
			checkRep();
			suspects_.clear();
			for (state_type target : partition(part))
				for (state_type source : inverseStep(target, symbol)) {
					state_type invPart = stateToPartition_[source].first;
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
			//TODO: we should exit immediately in trivial cases, so this can be >= 2
			assert(partitionBounds_.size() >= 1);
			for (const auto& p : partitionBounds_)
				assert(p.second - p.first > 0);
			assert(moveSize_.size() == partitionBounds_.size());
			assert(suspects_.size() <= partitionBounds_.size());
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

	//mutable == thread-safe in C++11+
	mutable std::atomic<unsigned int> refcount_;
	friend void intrusive_ptr_add_ref(const Automaton* p) noexcept {
		++p->refcount_;
	}
	friend void intrusive_ptr_release(const Automaton* p) noexcept {
		if (!(--p->refcount_))
			delete p;
	}
};

} //namespace impl
} //namespace automaton

#endif /* AUTOMATON_HPP */

