#ifndef AUTOMATON_TEST_UTIL_HPP_INCLUDED
#define AUTOMATON_TEST_UTIL_HPP_INCLUDED

#include "automaton.hpp"
#include <doctest.h>

//for custom assertion failure messages
//I couldn't get the compiler to find an operator<< overload, shrug
std::string to_string(const std::vector<unsigned int>& v);

std::vector<std::vector<unsigned int>> allStrings(unsigned int alphabetSize, unsigned int length);

/**
 * Asserts that the given automata are equivalent on all strings by checking all
 * strings up to the given length, and by other tests.  (Do not call this method
 * if the automata may differ on longer strings.)
 */
template<unsigned int AlphabetSize>
void equivalentOnAllStrings(const automaton::Automaton<AlphabetSize>& a, const automaton::Automaton<AlphabetSize>& b, int length, int lineno = -1) {
	for (auto& string : allStrings(AlphabetSize, length))
		//CHECK_EQ_MESSAGE(a.run(string), b.run(string), to_string(string) << " from line " << lineno);
		CHECK_EQ(a.run(string), b.run(string));
	automaton::ComparisonResult<AlphabetSize> cmp = compare_languages(a, b);
	//CHECK_MESSAGE(cmp.equal(), " from line " << lineno << "\n" << cmp.leftButNotRight << '\n' << cmp.rightButNotLeft << std::endl);
	CHECK_UNARY(cmp.equal());
	//TODO: make these const
//	EXPECT_EQ(a.isEmpty(), b.isEmpty()) << " from line " << lineno;
//	EXPECT_EQ(a.infinite(), b.infinite()) << " from line " << lineno;
}

#endif // AUTOMATON_TEST_UTIL_HPP_INCLUDED
