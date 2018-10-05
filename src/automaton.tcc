#include "hopcroft.hpp"
#include <sparsehash/dense_hash_set>

namespace automaton {

namespace detail {
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
} //namespace detail


template<unsigned int AlphabetSize>
Automaton<AlphabetSize>::Automaton() : deterministic_(true), minimal_(false), canonical_(false) {}
template<unsigned int AlphabetSize>
Automaton<AlphabetSize>::Automaton(const AutomatonBase& a) : deterministic_(false), minimal_(false), canonical_(false) {
	reserve(a.state_size());
	for (state_type s = 0; s < a.state_size(); ++s) {
		addState();
		setAccept(s, a.accept(s));
	}
	a.for_each_transition([&](state_type from, symbol_type on, state_type to) {
		addTrans(from, on, to);
	});
	deterministic_ = a.deterministic();
	minimal_ = a.minimal();
	canonical_ = a.canonical();
	if (canonical())
		prepareForEquals();
}
template<unsigned int AlphabetSize>
Automaton<AlphabetSize>::~Automaton() = default;
template<unsigned int AlphabetSize>
Automaton<AlphabetSize>::Automaton(const Automaton& a) = default;
template<unsigned int AlphabetSize>
Automaton<AlphabetSize>::Automaton(Automaton&& a) = default;
template<unsigned int AlphabetSize>
std::unique_ptr<WorkingAutomaton> Automaton<AlphabetSize>::clone() const {
	return std::make_unique<Automaton>(*this);
}
template<unsigned int AlphabetSize>
Automaton<AlphabetSize>& Automaton<AlphabetSize>::operator=(const Automaton& a) = default;
template<unsigned int AlphabetSize>
Automaton<AlphabetSize>& Automaton<AlphabetSize>::operator=(Automaton&& victim) = default;

template<unsigned int AlphabetSize>
AutomatonBase::state_type Automaton<AlphabetSize>::state_size() const {
	return static_cast<state_type>(transitions_.size());
}
template<unsigned int AlphabetSize>
AutomatonBase::state_type Automaton<AlphabetSize>::accept_size() const {
	return static_cast<state_type>(accept_.count());
}
template<unsigned int AlphabetSize>
AutomatonBase::symbol_type Automaton<AlphabetSize>::alphabet_size() const {
	return alphabet_size_v;
}
template<unsigned int AlphabetSize>
[[gnu::pure]] AutomatonBase::symbol_type Automaton<AlphabetSize>::active_alphabet_size() const {
	symbol_mask_type mask;
	for (state_type s = 0; s < state_size(); ++s)
		mask |= outgoing_mask(s);
	return mask.count();
}
template<unsigned int AlphabetSize>
std::size_t Automaton<AlphabetSize>::edge_size() const {
	return std::accumulate(transitions_.begin(), transitions_.end(), static_cast<std::size_t>(0),
			[](std::size_t l, const auto& r) {return l + r.size();});
}
template<unsigned int AlphabetSize>
std::size_t Automaton<AlphabetSize>::transition_size() const {
	std::size_t answer = 0;
	for (auto& ts : transitions_)
		for (auto t : ts)
			answer += t.symbols_.count();
	return answer;
}
template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::deterministic() const {return deterministic_;}
template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::minimal() const {return minimal_;}
template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::canonical() const {return canonical_;}

template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::accept(state_type state) const {
	assert(state < state_size());
	return accept_[state];
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::for_each_accept(std::function<void(state_type)> action) const {
	for (auto s = accept_.find_first(); s < accept_.size(); s = accept_.find_next(s))
		action(static_cast<state_type>(s));
}

template<unsigned int AlphabetSize>
StateSet Automaton<AlphabetSize>::step(state_type current, symbol_type symbol) const {
	assert(current < state_size());
	assert(symbol < AlphabetSize);
	StateSet next;
	for (const Transition& t : transitions_[current])
		if (t.symbols_[symbol])
			//We can go to many t.next_, but to each t.next_ only once.
			next.insert_absent(t.next_);
	//We used to assert(next.size() <= 1 || !deterministic_), but the
	//addTrans calls isStateDeterministic calls step (us) when checking
	//whether to clear deterministic_.  If we change the implementation of
	//isStateDeterministic to not actually build the steps, we could add the
	//assert back.
	return next;
}

template<unsigned int AlphabetSize>
SymbolSet Automaton<AlphabetSize>::outgoing(state_type state) const {
	return detail::set_of_indices(outgoing_mask(state));
}

template<unsigned int AlphabetSize>
StateSet Automaton<AlphabetSize>::destinations(state_type state) const {
	StateSet dest;
	for (const Transition& t : transitions_[state])
		dest.insert_absent(t.next_);
	return dest;
}

template<unsigned int AlphabetSize>
std::optional<AutomatonBase::state_type> Automaton<AlphabetSize>::stepDeterministic(state_type current, symbol_type symbol) const {
	assert(deterministic());
	assert(current < state_size());
	assert(symbol < AlphabetSize);
	for (const Transition& t : transitions_[current])
		if (t.symbols_[symbol])
			return t.next_;
	return std::nullopt;
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::for_each_destination(state_type state, std::function<void(state_type)> action) const {
	assert(state < state_size());
	for (const auto& t : transitions_[state])
		action(t.next_);
}

template<unsigned int AlphabetSize>
SymbolSet Automaton<AlphabetSize>::labels(state_type from, state_type to) const {
	for (const Transition& t : transitions_[from])
		if (t.next_ == to)
			return detail::set_of_indices(t.symbols_);
	return {};
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::for_each_transition(state_type state, std::function<void(symbol_type, state_type)> action) const {
	for (const Transition& t : transitions_[state])
		for (symbol_type a = t.symbols_.find_first(); a < t.symbols_.size(); a = t.symbols_.find_next(a))
			action(a, t.next_);
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::for_each_transition(std::function<void(state_type, symbol_type, state_type)> action) const {
	for (state_type s = 0; s < state_size(); ++s)
		for (const Transition& t : transitions_[s])
			for (symbol_type a = t.symbols_.find_first(); a < t.symbols_.size(); a = t.symbols_.find_next(a))
				action(s, a, t.next_);
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::reserve(state_type state_capacity) {
	transitions_.reserve(state_capacity);
	accept_.reserve(state_capacity);
}

template<unsigned int AlphabetSize>
AutomatonBase::state_type Automaton<AlphabetSize>::addState() {
	state_type s = state_size();
	transitions_.push_back({});
	accept_.push_back(false);
	minimal_ = canonical_ = false;
	return s;
}

template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::addEpsilon(state_type from, state_type to) {
	bool changed = false;
	if (accept_[to]) {
		changed |= !accept_[from];
		accept_.set(from);
	}
	for (const Transition& t : transitions_[to])
		changed |= addTrans(from, t.symbols_, t.next_);
	minimal_ = canonical_ = false;
	return changed;
}

template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::addTrans(state_type from, symbol_type symbol, state_type to) {
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
	minimal_ = canonical_ = false;
	return true;
}

template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::setAccept(state_type state, bool accepts) {
	bool old = accept_.test(state);
	accept_.set(state, accepts);
	minimal_ = canonical_ = false;
	return accepts == old;
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::clear() {
	transitions_.clear();
	accept_.clear();
	deterministic_ = true;
	minimal_ = canonical_ = false;
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::shrink_to_fit() {
	transitions_.shrink_to_fit();
	for (auto& ts : transitions_)
		ts.shrink_to_fit();
	accept_.shrink_to_fit();
}

template<unsigned int AlphabetSize>
Automaton<AlphabetSize> Automaton<AlphabetSize>::shuffleAcceptDeterministic(const Automaton& left, const Automaton& right) {
	assert(left.deterministic());
	assert(right.deterministic());
	//(left state, right state, new state, left automation active)
	using state_quad = std::tuple<state_type, state_type, state_type, bool>;
	circular_deque<state_quad, 16> worklist;
	detail::DenseShuffleAcceptMap newstates(left.state_size(), right.state_size());

	Automaton a;
	//TODO: are we sure?
	a.deterministic_ = left.deterministic() && right.deterministic();
	//We have a free choice to begin with the left or with the right
	//automaton, so we have two "initial" states and call addEpsilon later.
	a.addState(); a.addState(); a.addState();
	//TODO: assuming 0 is the initial state
	worklist.push_back({0, 0, 1, true});
	newstates.insert({0, 0, true}, 1);
	worklist.push_back({0, 0, 2, false});
	newstates.insert({0, 0, false}, 2);

	while (!worklist.empty()) {
		state_type ls, rs, ns;
		bool leftactive;
		std::tie(ls, rs, ns, leftactive) = worklist.pop_back();
		a.accept_.set(ns, left.accept_[ls] && right.accept_[rs]);

		if (leftactive) {
			for (Transition lt : left.transitions_[ls]) {
				auto p = newstates.get_or_add_state({lt.next_, rs, leftactive}, a);
				if (p.second)
					worklist.push_back({lt.next_, rs, p.first, leftactive});
				a.addTrans(ns, lt.symbols_, p.first);

				//If we brought the active automaton to an accept state,
				//we can switch if we want.
				if (left.accept(lt.next_)) {
					auto q = newstates.get_or_add_state({lt.next_, rs, !leftactive}, a);
					if (q.second)
						worklist.push_back({lt.next_, rs, q.first, !leftactive});
					a.addTrans(ns, lt.symbols_, q.first);
				}
			}
		} else {
			for (Transition rt : right.transitions_[rs]) {
				auto p = newstates.get_or_add_state({ls, rt.next_, leftactive}, a);
				if (p.second)
					worklist.push_back({ls, rt.next_, p.first, leftactive});
				a.addTrans(ns, rt.symbols_, p.first);

				//If we brought the active automaton to an accept state,
				//we can switch if we want.
				if (right.accept(rt.next_)) {
					auto q = newstates.get_or_add_state({ls, rt.next_, !leftactive}, a);
					if (q.second)
						worklist.push_back({ls, rt.next_, q.first, !leftactive});
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

template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::isEmpty() {
	removeDeadStates();
	return state_size() == 1U && accept_.none();
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::determinize() {
	if (deterministic()) return;
	//TODO: I can't see any way to do this in-place, but it might be better
	//to store an edge list instead, clear, and commit back into *this, or pass
	//*this but build an edge list, then clear and replay the edge list into *this.
	Automaton a;
	detail::determinize_into(*this, a);
	MAYBE_UNUSED std::size_t oldsize = state_size();
	*this = std::move(a);
	assert(deterministic());
	AUTOMATON_DEBUG(std::cout << "determinize: " << oldsize << " -> " << state_size() << std::endl);
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::totalize() {
	state_type crash;
	bool madeCrashState = false;
	for (state_type s = 0; s < state_size(); ++s) {
		symbol_mask_type missing = ~outgoing_mask(s);
		if (missing.any()) {
			if (!madeCrashState) {
				crash = addState();
				madeCrashState = true;
			}
			addTrans(s, missing, crash);
		}
	}
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::removeDeadStates() {
	auto live = detail::live_states(*this);
	if (live.size() == state_size())
		return;
	if (live.empty()) {
		*this = empty<AlphabetSize>();
		return;
	}
	MAYBE_UNUSED std::size_t oldsize = state_size();
	//We will be minimal or canonical here, but we might be deterministic.
	//compressRenumber conservatively kills determinism, so we need to
	//preserve it ourselves.
	bool det = deterministic_;
	auto res = detail::find_dead_state_renumbering(*this, live);
	compressRenumber(static_cast<state_type>(res.first.size()), res.first.begin(), res.second.begin());
	deterministic_ = det;
	AUTOMATON_DEBUG(std::cout << "removeDeadStates: " << oldsize << " -> " << state_size() << std::endl);
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::minimize() {
	if (minimal()) {
		assert(deterministic());
		return;
	}
	detail::ExplodedAutomaton exp = detail::determinize_explode(*this);
	//All states in the result of determinize_explode are reachable.  If none of
	//them are accepting, we're done.
	if (exp.accept.empty()) {
		*this = empty<AlphabetSize>();
		return;
	}
	//The Java library explicitly checks for the all-strings automaton here,
	//but it doesn't seem to be necessary.
	//Java totalizes the automaton here (then removes the added state in
	//removeDeadStates after minimizing).  That isn't required; our Hopcroft
	//implementation understands states are not equivalent if one crashes
	//and the other doesn't.
	//Instead, we remove dead states before minimizing, to prevent
	//transitions to dead states from distinguishing states that are
	//otherwise equivalent.
	detail::removeDeadStates(exp);
	if (exp.edges.empty()) {
		assert(std::find(exp.accept.begin(), exp.accept.end(), 0) != exp.accept.end());
		*this = epsilon<AlphabetSize>();
		return;
	}

	MAYBE_UNUSED std::size_t oldsize = state_size();
	dynarray<state_type> res = detail::hopcroft(exp);
	clear();
	this->deterministic_ = false; //for speed when imploding; we set it below
	if (res.size())
		detail::implodeRenumber(*this, exp, res);
	else
		implode(*this, exp);
	deterministic_ = minimal_ = true;

	AUTOMATON_DEBUG(std::cout << "minimize: " << oldsize << " -> " << state_size() << std::endl);
#ifndef NDEBUG
	//make sure hopcroft didn't screw up
	for (state_type s = 0; s < state_size(); ++s)
		assert(isStateDeterministic(s));
#endif
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::compressRenumber(state_type newSize, const state_type* survivorFrom, const state_type* remapping) {
	for (state_type s = 0; s < newSize; ++s) {
		state_type victim = survivorFrom[s];
		if (victim != s) {
			transitions_[s] = std::move(transitions_[victim]);
			setAccept(s, accept(victim));
		}

		//Renumber and compress redundant transitions.
		auto& ts = transitions_[s];
		for (auto i = ts.size(); i-- > 0;) {
			if (remapping[ts[i].next_] == std::numeric_limits<state_type>::max())
				ts.erase(ts.begin() + i);
			else {
				ts[i].next_ = remapping[ts[i].next_];
				//compare against previously remapped transitions, iterating backwards
				for (auto j = ts.size(); j-- > (i+1);)
					if (ts[i].next_ == ts[j].next_) {
						ts[i].symbols_ |= ts[j].symbols_;
						ts.erase(ts.begin() + j);
						break; //can only be one other with same next_
					}
			}
		}
	}
	transitions_.resize(newSize);
	accept_.resize(newSize);
	deterministic_ = minimal_ = canonical_ = false;
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::canonicalize() {
	if (canonical()) {
		assert(deterministic());
		assert(minimal());
		return;
	}
	minimize();

	detail::LazyEdgeEnumerator<identity_permutation> enumerator{identity_permutation(), this};
	enumerator.runToCompletion();
	renumberStates(enumerator.renumbering.begin());
	prepareForEquals();
	canonical_ = true;
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::swapStateNumbers(state_type a, state_type b) {
	if (a == b) return;
	//This might actually be faster with a lookup table.
	for (auto& ts : transitions_)
		for (Transition& t : ts)
			if (t.next_ == a)
				t.next_ = b;
			else if (t.next_ == b)
				t.next_ = a;
	bool aa = accept_.test(a);
	accept_.set(a, accept_.test(b));
	accept_.set(b, aa);
	using std::swap;
	swap(transitions_[a], transitions_[b]);
	canonical_ = false;
	if (a == 0 || b == 0)
		minimal_ = false;
}

template<unsigned int AlphabetSize>
void Automaton<AlphabetSize>::prepareForEquals() {
	for (auto& ts : transitions_)
		//We shouldn't have two Transitions with the same destination, so we
		//sort only on next_.
		std::sort(ts.begin(), ts.end(), [](Transition a, Transition b){return a.next_ < b.next_;});
	assert(preparedForEquals());
}
template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::preparedForEquals() const {
	return std::all_of(transitions_.begin(), transitions_.end(), [](const auto& ts) {
		return std::is_sorted(ts.begin(), ts.end(), [](Transition a, Transition b) {
			return a.next_ < b.next_;
		});
	});
}
template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::operator==(const Automaton& other) const {
	assert(preparedForEquals());
	assert(other.preparedForEquals());
	return std::tie(accept_, transitions_) == std::tie(other.accept_, other.transitions_);
}
/**
 * Compares this automaton with another for structural inequality.  Call
 * prepareForEquals() on both automata first.
 */
template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::operator!=(const Automaton& other) const {
	return !(*this == other);
}

template<unsigned int AlphabetSize>
std::size_t Automaton<AlphabetSize>::working_hash() const {
	size_t h = 13;
	h = h * 31 + state_size();
	for (const auto& ts : transitions_) {
		h = h * 31 + ts.size();
		for (auto t : ts) {
			h = h * 31 + t.next_;
			h = h * 31 + std::hash<symbol_mask_type>()(t.symbols_);
		}
	}
	//punt on accept_ for now
	return h;
}

template<unsigned int AlphabetSize>
AutomatonBase::state_type Automaton<AlphabetSize>::append(const Automaton& b) {
	reserve(state_size() + b.state_size());
	state_type base = state_size();
	for (const auto& t : b.transitions_) {
		transitions_.push_back(t);
		for (Transition& nt : transitions_.back())
			nt.next_ += base;
	}
	for (state_type p = 0; p < b.state_size(); ++p)
		accept_.push_back(b.accept(p));
	deterministic_ &= b.deterministic();
	minimal_ = canonical_ = false;
	return base;
}

template<unsigned int AlphabetSize>
AutomatonBase::state_type Automaton<AlphabetSize>::append(Automaton&& b) {
	reserve(state_size() + b.state_size());
	state_type base = state_size();
	transitions_.insert(transitions_.end(), std::make_move_iterator(b.transitions_.begin()),
			std::make_move_iterator(b.transitions_.end()));
	for (state_type s = base; s < state_size(); ++s)
		for (Transition& nt : transitions_[s])
			nt.next_ += base;
	for (state_type p = 0; p < b.state_size(); ++p)
		accept_.push_back(b.accept(p));
	deterministic_ &= b.deterministic();
	minimal_ = canonical_ = false;
	return base;
}

template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::addTrans(state_type from, symbol_mask_type symbols, state_type to) {
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
	minimal_ = canonical_ = false;
	return true;
}

template<unsigned int AlphabetSize>
typename Automaton<AlphabetSize>::symbol_mask_type Automaton<AlphabetSize>::outgoing_mask(state_type state) const {
	symbol_mask_type mask;
	for (const Transition& t : transitions_[state])
		mask |= t.symbols_;
	return mask;
}

template<unsigned int AlphabetSize>
SymbolSet Automaton<AlphabetSize>::activeAlphabet() const {
	symbol_mask_type mask;
	for (state_type s = 0; s < state_size(); ++s)
		mask |= outgoing_mask(s);
	return detail::set_of_indices(mask);
}

template<unsigned int AlphabetSize>
bool Automaton<AlphabetSize>::isStateDeterministic(state_type state) const {
	symbol_mask_type overall;
	unsigned int sum = 0;
	for (const Transition& t : transitions_[state]) {
		overall |= t.symbols_;
		sum += t.symbols_.count();
	}
	return overall.count() == sum;
}
} //namespace automaton
