#include "precompiled.hpp"
#include "automatonbase.hpp"

using namespace automaton;

AutomatonBase::~AutomatonBase() = default;

AutomatonBase::state_type AutomatonBase::accept_size() const {
	state_type count = 0;
	for (state_type s = 0; s < state_size(); ++s)
		if (accept(s))
			++count;
	return count;
}
AutomatonBase::symbol_type AutomatonBase::active_alphabet_size() const {
	return numeric_cast<symbol_type>(activeAlphabet().size());
}
std::size_t AutomatonBase::edge_size() const {
	std::size_t count = 0;
	for (state_type s = 0; s < state_size(); ++s)
		count += destinations(s).size();
	return count;
}
std::size_t AutomatonBase::transition_size() const {
	std::size_t count = 0;
	for (state_type s = 0; s < state_size(); ++s)
		for (symbol_type a = 0; a < alphabet_size(); ++a)
			count += step(s, a).size();
	return count;
}

void AutomatonBase::for_each_accept(std::function<void(state_type)> action) const {
	for (state_type s = 0; s < state_size(); ++s)
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
		for (symbol_type a = 0; a < alphabet_size(); ++a)
			if (stepDeterministic(state, a))
				ret.insert_absent(a);
	} else
		for (symbol_type a = 0; a < alphabet_size(); ++a)
			for (MAYBE_UNUSED state_type next : step(state, a))
				ret.insert(a);
	return ret;
}

AutomatonBase::StateSet AutomatonBase::destinations(state_type state) const {
	assert(state < state_size());
	StateSet ret;
	if (deterministic()) {
		for (symbol_type a = 0; a < alphabet_size(); ++a)
			if (auto next = stepDeterministic(state, a))
				ret.insert(*next);
	} else
		for (symbol_type a = 0; a < alphabet_size(); ++a)
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
		for (symbol_type a = 0; a < alphabet_size(); ++a)
			if (auto next = stepDeterministic(state, a))
				action(a, *next);
	} else
		for (symbol_type a = 0; a < alphabet_size(); ++a)
			for (state_type next : step(state, a))
				action(a, next);
}

AutomatonBase::SymbolSet AutomatonBase::labels(state_type from, state_type to) const {
	assert(from < state_size());
	assert(to < state_size());
	SymbolSet ret;
	if (deterministic()) {
		for (symbol_type a = 0; a < alphabet_size(); ++a)
			if (auto next = stepDeterministic(from, a); next && *next == to)
				ret.insert_absent(a);
	} else
		for (symbol_type a = 0; a < alphabet_size(); ++a)
			for (MAYBE_UNUSED state_type next : step(from, a))
				if (next == to) {
					ret.insert_absent(a);
					break;
				}
	return ret;
}

void AutomatonBase::for_each_transition(std::function<void(state_type, symbol_type, state_type)> action) const {
	if (deterministic()) {
		for (state_type from = 0; from < state_size(); ++from)
			for (symbol_type a = 0; a < alphabet_size(); ++a)
				if (auto next = stepDeterministic(from, a))
					action(from, a, *next);
	} else
		for (state_type from = 0; from < state_size(); ++from)
			for (symbol_type a = 0; a < alphabet_size(); ++a)
				for (state_type next : step(from, a))
					action(from, a, next);
}


AutomatonBase::SymbolSet AutomatonBase::activeAlphabet() const {
	SymbolSet ret;
	for (symbol_type a = 0; a < alphabet_size(); ++a)
		for (symbol_type s = 0; s < state_size(); ++s)
			if (!step(s, a).empty()) {
				ret.insert_absent(a);
				break;
			}
	return ret;
}

bool AutomatonBase::run(std::initializer_list<unsigned int> string) const {
	//This overload exists because the compiler won't deduce initializer_list
	//for the IteratorRange overload.
	return run(string.begin(), string.end());
}

std::size_t AutomatonBase::hash() const {
	std::size_t h = 13;
	h = 31*h + state_size();
	h = 31*h + alphabet_size();
	for (state_type s = 0; s < state_size(); ++s) {
		for (symbol_type a = 0; a < alphabet_size(); ++a) {
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
		strings.push_back(boost::lexical_cast<std::string>(s));
	return boost::algorithm::join(strings, ", ");
}
}

std::ostream& automaton::operator<<(std::ostream& o, const AutomatonBase& a) {
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

	auto length = boost::lexical_cast<std::string>(a.state_size() - 1).size();
	auto leftpad = [length](auto thing) {
		auto s = boost::lexical_cast<std::string>(thing);
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