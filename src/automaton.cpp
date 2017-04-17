#include "precompiled.hpp"
#include "automaton.hpp"

namespace automaton {

AutomatonBase::~AutomatonBase() = default;

namespace detail {
std::ostream& operator<<(std::ostream& os, const AutomatonReprStreamer& rs) {
	const AutomatonBase& a = rs.a;
	os << "Automaton<" << a.alphabet_size() << "> make() {\n";
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
} //namespace detail

} //namespace automaton