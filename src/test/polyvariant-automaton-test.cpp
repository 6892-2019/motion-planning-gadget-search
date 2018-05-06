#include "polyvariant.hpp"
#include "automaton.hpp"
#include <doctest.h>

using namespace automaton;

TEST_CASE("polyvariant-automaton_Singleton") {
	polyvariant<WorkingAutomaton, Automaton<8u>> just_one{std::in_place_type<Automaton<8u>>};
	CHECK_EQ(just_one->alphabet_size(), 8);
	CHECK_EQ(just_one->state_size(), 0);
	just_one->addState();
	CHECK_EQ(just_one->state_size(), 1);
	CHECK_UNARY_FALSE(just_one->accept(0));
}

//using polyautomaton = polyvariant<WorkingAutomaton,