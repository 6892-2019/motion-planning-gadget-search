#include "precompiled.hpp"
#include "automaton.hpp"

namespace automaton {

AutomatonBase::~AutomatonBase() = default;

namespace detail {
std::ostream& operator<<(std::ostream& os, const AutomatonReprStreamer& rs) {
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

struct Tarjan {
	Tarjan(const AutomatonBase& a_) : a(a_), lowlink(a.state_size()), number(a.state_size()) {
		std::fill(number.begin(), number.end(), std::numeric_limits<state_type>::max());
		result.components_.reserve(a.state_size());
	}
	const AutomatonBase& a;
	unsigned int index;
	std::vector<state_type> stack;
	dynarray<state_type> lowlink, number;
	SCCs result;

	void strongconnect(state_type v) {
		lowlink[v] = number[v] = index++;
		stack.push_back(v);
		a.for_each_destination(v, [&](state_type w) {
			if (number[w] == std::numeric_limits<state_type>::max()) {
				strongconnect(w);
				//TODO: apparently lowlink is only used in strongconnect(v), so
				//we could make it the return value instead of an array
				lowlink[v] = std::min(lowlink[v], lowlink[w]);
			} else if (number[w] < number[v] && std::find(stack.begin(), stack.end(), w) != stack.end())
				lowlink[v] = std::min(lowlink[v], number[w]);
		});
		if (lowlink[v] == number[v]) {
			result.indices_.push_back(static_cast<unsigned int>(result.components_.size()));
			while (!stack.empty() && number[stack.back()] >= number[v]) {
				result.components_.push_back(stack.back());
				stack.pop_back();
			}
		}
	}
	SCCs compute() {
		for (state_type w = 0; w < a.state_size(); ++w)
			if (number[w] == std::numeric_limits<state_type>::max())
				strongconnect(w);
		result.indices_.push_back(static_cast<unsigned int>(result.components_.size()));

		for (state_type s = 0; s < a.state_size(); ++s)
			//Each state is in exactly one component.
			assert(std::count(result.components_.begin(), result.components_.end(), s) == 1);

		return std::move(result);
	}
};
} //namespace detail

SCCs find_components(const AutomatonBase& a) {
	return detail::Tarjan(a).compute();
}

} //namespace automaton