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
	just_one->addTrans(0, 7, 0);
}

TEST_CASE("polyvariant-automaton_Doubleton") {
	polyvariant<WorkingAutomaton, Automaton<2u>, Automaton<8u>> q{std::in_place_type<Automaton<8u>>};
	CHECK_EQ(q->alphabet_size(), 8);
	CHECK_EQ(q->state_size(), 0);
	q->addState();
	CHECK_EQ(q->state_size(), 1);
	CHECK_UNARY_FALSE(q->accept(0));
	q->addTrans(0, 7, 0); //should be an error if we're using the 2u alternative
}

//using polyautomaton = polyvariant<WorkingAutomaton,