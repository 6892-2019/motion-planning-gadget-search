#include "precompiled.hpp"
#include "automaton.hpp"
#include "packedautomaton.hpp"

using namespace automaton;

namespace {
bool compare_working(const detail::WorkingAutomaton& left, const detail::WorkingAutomaton& right) {
	assert(left.alphabet_size() == right.alphabet_size()); //should already have been checked in the caller
	switch (left.alphabet_size()) {
#define COMPARE_WORKING_CASE(N) case N: return static_cast<const Automaton<N>&>(left) == static_cast<const Automaton<N>&>(right);
		COMPARE_WORKING_CASE(1)
		COMPARE_WORKING_CASE(2)
		COMPARE_WORKING_CASE(3)
		COMPARE_WORKING_CASE(4)
		COMPARE_WORKING_CASE(5)
		COMPARE_WORKING_CASE(6)
		COMPARE_WORKING_CASE(7)
		COMPARE_WORKING_CASE(8)
		COMPARE_WORKING_CASE(9)
		COMPARE_WORKING_CASE(10)
		COMPARE_WORKING_CASE(11)
		COMPARE_WORKING_CASE(12)
		COMPARE_WORKING_CASE(13)
		COMPARE_WORKING_CASE(14)
		COMPARE_WORKING_CASE(15)
		COMPARE_WORKING_CASE(16)
#undef COMPARE_WORKING_CASE
	default:
		std::cout << "unhandled compare_working: " << typeid(left).name() << ", " << typeid(right).name();
		std::terminate();
	}
}

bool compare_slowpath(const AutomatonBase& left, const AutomatonBase& right) {
	//TODO: these are only useful optimizations if both left and right override
	//their default implementations; otherwise we're just wasting time
	if (left.accept_size() != right.accept_size()) return false;
	if (left.edge_size() != right.edge_size()) return false;
	if (left.transition_size() != right.transition_size()) return false;

	for (AutomatonBase::state_type s = 0; s < left.state_size(); ++s) {
		if (left.accept(s) != right.accept(s)) return false;
		for (AutomatonBase::symbol_type a = 0; a < left.alphabet_size(); ++a) {
			StateSet ld = left.step(s, a), rd = right.step(s, a);
			ld.sort();
			rd.sort();
			if (ld != rd) return false;
		}
	}
	return true;
}
} //anonymous namespace



namespace automaton {

AutomatonBase::~AutomatonBase() = default;
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

bool operator==(const AutomatonBase& left, const AutomatonBase& right) {
	if (left.alphabet_size() != right.alphabet_size()) return false;
	if (left.state_size() != right.state_size()) return false;

	if (auto l = dynamic_cast<const detail::WorkingAutomaton*>(&left),
			r = dynamic_cast<const detail::WorkingAutomaton*>(&right); l && r)
		return compare_working(*l, *r);
	if (auto l = dynamic_cast<const PackedAutomaton*>(&left),
			r = dynamic_cast<const PackedAutomaton*>(&right); l && r)
		return *l == *r;

	return compare_slowpath(left, right);
}

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