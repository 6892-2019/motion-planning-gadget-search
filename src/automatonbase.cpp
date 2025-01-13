// SPDX-License-Identifier: MIT
// Copyright 2017 Massachusetts Institute of Technology
#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "stringutils.hpp"

using namespace automaton;

AutomatonBase::AutomatonBase() = default;
AutomatonBase::~AutomatonBase() = default;
AutomatonBase::AutomatonBase(const AutomatonBase&) = default;
AutomatonBase::AutomatonBase(AutomatonBase&&) = default;
AutomatonBase& AutomatonBase::operator=(const AutomatonBase&) = default;
AutomatonBase& AutomatonBase::operator=(AutomatonBase&&) = default;

AutomatonBase::state_type AutomatonBase::accept_size() const {
	state_type count = 0;
	for (state_type s : xrange(state_size()))
		if (accept(s))
			++count;
	return count;
}
AutomatonBase::symbol_type AutomatonBase::active_alphabet_size() const {
	return numeric_cast<symbol_type>(activeAlphabet().size());
}
std::size_t AutomatonBase::edge_size() const {
	std::size_t count = 0;
	for (state_type s : xrange(state_size()))
		count += destinations(s).size();
	return count;
}
std::size_t AutomatonBase::transition_size() const {
	std::size_t count = 0;
	auto alphabet = xrange(alphabet_size());
	for (state_type s : xrange(state_size()))
		for (symbol_type a : alphabet)
			count += step(s, a).size();
	return count;
}

void AutomatonBase::for_each_accept(std::function<void(state_type)> action) const {
	for (state_type s : xrange(state_size()))
		if (accept(s))
			action(s);
}

std::optional<AutomatonBase::state_type> AutomatonBase::stepDeterministic(state_type state, symbol_type symbol) const {
	assert(deterministic());
	assert(state < state_size());
	assert(symbol < alphabet_size());
	StateSet nexts = step(state, symbol);
	assert(nexts.size() <= 1);
	return nexts.size() ? std::make_optional(nexts.front()) : std::nullopt;
}

AutomatonBase::SymbolSet AutomatonBase::outgoing(state_type state) const {
	assert(state < state_size());
	SymbolSet ret;
	if (deterministic()) {
		for (symbol_type a : xrange(alphabet_size()))
			if (stepDeterministic(state, a))
				ret.insert_absent(a);
	} else
		for (symbol_type a : xrange(alphabet_size()))
			for (MAYBE_UNUSED state_type next : step(state, a))
				ret.insert(a);
	return ret;
}

AutomatonBase::StateSet AutomatonBase::destinations(state_type state) const {
	assert(state < state_size());
	StateSet ret;
	if (deterministic()) {
		for (symbol_type a : xrange(alphabet_size()))
			if (auto next = stepDeterministic(state, a))
				ret.insert(*next);
	} else
		for (symbol_type a : xrange(alphabet_size()))
			for (state_type next : step(state, a))
				ret.insert(next);
	return ret;
}

void AutomatonBase::for_each_destination(state_type state, std::function<void(state_type)> action) const {
	assert(state < state_size());
	for (state_type dest : destinations(state))
		action(dest);
}

void AutomatonBase::for_each_transition(state_type state, std::function<void(symbol_type, state_type)> action) const {
	assert(state < state_size());
	if (deterministic()) {
		for (symbol_type a : xrange(alphabet_size()))
			if (auto next = stepDeterministic(state, a))
				action(a, *next);
	} else
		for (symbol_type a : xrange(alphabet_size()))
			for (state_type next : step(state, a))
				action(a, next);
}

AutomatonBase::SymbolSet AutomatonBase::labels(state_type from, state_type to) const {
	assert(from < state_size());
	assert(to < state_size());
	SymbolSet ret;
	if (deterministic()) {
		for (symbol_type a : xrange(alphabet_size()))
			if (auto next = stepDeterministic(from, a); next && *next == to)
				ret.insert_absent(a);
	} else
		for (symbol_type a : xrange(alphabet_size()))
			for (MAYBE_UNUSED state_type next : step(from, a))
				if (next == to) {
					ret.insert_absent(a);
					break;
				}
	return ret;
}

void AutomatonBase::for_each_transition(std::function<void(state_type, symbol_type, state_type)> action) const {
	auto alphabet = xrange(alphabet_size());
	if (deterministic()) {
		for (state_type from : xrange(state_size()))
			for (symbol_type a : alphabet)
				if (auto next = stepDeterministic(from, a))
					action(from, a, *next);
	} else
		for (state_type from : xrange(state_size()))
			for (symbol_type a : alphabet)
				for (state_type next : step(from, a))
					action(from, a, next);
}

AutomatonBase::SymbolSet AutomatonBase::activeAlphabet() const {
	SymbolSet ret;
	auto states = xrange(state_size());
	for (symbol_type a : xrange(alphabet_size()))
		for (symbol_type s : states)
			if (!step(s, a).empty()) {
				ret.insert_absent(a);
				break;
			}
	return ret;
}

bool AutomatonBase::run(std::initializer_list<symbol_type> string) const {
	//Breadth-first search.
	tsl::hopscotch_set<state_type> current, next;
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

std::size_t AutomatonBase::hash() const {
	std::size_t h = 13;
	const auto states = state_size();
	const auto alphabet = alphabet_size();
	h = 31*h + states;
	h = 31*h + alphabet;
	for (state_type s = 0; s < states; ++s) {
		for (symbol_type a = 0; a < alphabet; ++a) {
			StateSet dests = step(s, a);
			dests.sort();
			for (state_type d : dests)
				h = 31*h + d;
		}
		//If we ever want to mix in accept, find a way to do it more than one bit at a time.
	}
	return h;
}

namespace {
std::string stringize(SymbolSet set) {
	std::vector<std::string> strings;
	set.sort();
	for (auto s : set)
		strings.push_back(std::to_string(s));
	return join(strings, ", ");
}

template<class Streamish>
Streamish& into_stream(Streamish& o, const AutomatonBase& a) {
	o << a.state_size() << " states (" << a.accept_size() << " accepting), "
			<< a.edge_size() << " edges, " << a.transition_size() << " transitions";
	if (a.deterministic())
		o << ", deterministic";
	if (a.minimal())
		o << ", minimal";
	if (a.canonical())
		o << ", canonical";
	o << "\n";
	o << a.alphabet_size() << " symbols, " << a.active_alphabet_size() << " active: "
			//this is a bit wasteful: join a string only to print it
			<< stringize(a.activeAlphabet()) << "\n";

	auto length = std::to_string(a.state_size() - 1).size();
	auto leftpad = [length](auto thing) {
		auto s = std::to_string(thing);
		while (s.size() < length)
			s = " " + s; //waste
		return s;
	};

	for (AutomatonBase::state_type i = 0; i < a.state_size(); ++i) {
		o << "state " << leftpad(i) << (a.accept(i) ? " [accept]:\n" : ":\n");
		for (auto p : a.edges(i)) {
			p.first.sort();
			o << "  to " << leftpad(p.second) << " on " << stringize(p.first) << "\n";
		}
	}
	return o;
}
}

std::ostream& automaton::operator<<(std::ostream& o, const AutomatonBase& a) {
	return into_stream(o, a);
}

std::ostream& automaton::detail::operator<<(std::ostream& os, const AutomatonReprStreamer& rs) {
	const AutomatonBase& a = rs.a;
	os << "Automaton<" << a.alphabet_size() << "> make" << a.hash()<< "() {\n";
	os << "\tAutomaton<" << a.alphabet_size() << "> a;\n";
	os << "\ta.reserve(" << a.state_size() << ");\n";
	os << "\tfor (AutomatonBase::state_type s = 0; s < " << a.state_size() << "; ++s)\n";
	os << "\t\ta.addState();\n";
	os << "\tfor (AutomatonBase::state_type s : {";
	for (AutomatonBase::state_type s = 0; s < a.state_size(); ++s)
		if (a.accept(s))
			os << s << ", ";
	os << "})\n";
	os << "\t\ta.setAccept(s);\n";
	//TODO: add a (non-virtual) addTrans overload taking an initializer_list<symbol_type>
	for (AutomatonBase::state_type from = 0; from < a.state_size(); ++from)
		for (auto edge : a.edges(from))
			for (auto on : edge.first)
				os << "\ta.addTrans(" << from << ", " << on << ", " << edge.second << ");\n";
	os << "\treturn a;\n";
	os << "}";
	return os;
}

namespace {
/**
 * @return a determinized copy of the given automaton
 */
std::unique_ptr<WorkingAutomaton> determinize(const WorkingAutomaton& a) {
	std::unique_ptr<WorkingAutomaton> b = a.clone();
	b->determinize();
	return b;
}
///**
// * @return a determinized copy of the given automaton
// */
//std::unique_ptr<WorkingAutomaton> determinize(WorkingAutomaton&& a) {
//	a.determinize();
//	//TODO: clone-by-move
//}

//Contents copied in from the Automaton static member function.  Best I can
//tell, that function benefits from avoiding using edges() (and thus SymbolSet).
std::unique_ptr<WorkingAutomaton> shuffleAcceptDeterministic(
		const WorkingAutomaton& left, const WorkingAutomaton& right, unsigned int alphabet_size) {
	assert(left.deterministic());
	assert(right.deterministic());
	using state_type = WorkingAutomaton::state_type;
	//(left state, right state, new state, left automation active)
	using state_quad = std::tuple<state_type, state_type, state_type, bool>;
	circular_deque<state_quad, 16> worklist;
	automaton::detail::DenseShuffleAcceptMap newstates(left.state_size(), right.state_size());

	std::unique_ptr<WorkingAutomaton> ret = make_working(alphabet_size);
	WorkingAutomaton& a = *ret;
	//TODO: If we know we're not deterministic, we'd like to say so up front to
	//not waste time checking.  Also, according to the old code this is a copy
	//of, we're not sure this is correct, anyway.
//	a.deterministic_ = left.deterministic() && right.deterministic();
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
		a.setAccept(ns, left.accept(ls) && right.accept(rs));

		if (leftactive) {
			for (auto&& [symbols, next] : left.edges(ls)) {
				auto p = newstates.get_or_add_state({next, rs, leftactive}, a);
				if (p.second)
					worklist.push_back({next, rs, p.first, leftactive});
				a.addTrans(ns, symbols, p.first);

				//If we brought the active automaton to an accept state,
				//we can switch if we want.
				if (left.accept(next)) {
					auto q = newstates.get_or_add_state({next, rs, !leftactive}, a);
					if (q.second)
						worklist.push_back({next, rs, q.first, !leftactive});
					a.addTrans(ns, symbols, q.first);
				}
			}
		} else {
			for (auto&& [symbols, next] : right.edges(rs)) {
				auto p = newstates.get_or_add_state({ls, next, leftactive}, a);
				if (p.second)
					worklist.push_back({ls, next, p.first, leftactive});
				a.addTrans(ns, symbols, p.first);

				//If we brought the active automaton to an accept state,
				//we can switch if we want.
				if (right.accept(next)) {
					auto q = newstates.get_or_add_state({ls, next, !leftactive}, a);
					if (q.second)
						worklist.push_back({ls, next, q.first, !leftactive});
					a.addTrans(ns, symbols, q.first);
				}
			}
		}
	}

	//Choose left or right at the start.
	a.addEpsilon(0, 1);
	a.addEpsilon(0, 2);
	return ret;
}
} //end anonymous namespace

namespace automaton {

std::string to_string(const AutomatonBase& a) {
	StringBuilder sb;
	into_stream(sb, a);
	return std::move(sb).data();
}

std::unique_ptr<WorkingAutomaton> shuffleAccept(const WorkingAutomaton& left,
		const WorkingAutomaton& right,
		unsigned int alphabet_size) {
	return shuffleAcceptDeterministic(
			left.deterministic() ? left : *determinize(left),
			right.deterministic() ? right : *determinize(right),
			alphabet_size);
}
std::unique_ptr<WorkingAutomaton> shuffleAccept(WorkingAutomaton&& left, WorkingAutomaton&& right,
		unsigned int alphabet_size) {
	//We own these automata, so we can safely reuse them.
	left.determinize();
	right.determinize();
	return shuffleAcceptDeterministic(std::move(left), std::move(right), alphabet_size);
}

} //namespace automaton