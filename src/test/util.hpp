// SPDX-License-Identifier: MIT
// Copyright 2018 Massachusetts Institute of Technology
// Copyright 2025 Jeffrey Bosboom
#ifndef AUTOMATON_TEST_UTIL_HPP_INCLUDED
#define AUTOMATON_TEST_UTIL_HPP_INCLUDED

#include "automaton.hpp"
#include "automaton-regexesque.hpp" //for compare_languages
#include <doctest/doctest.h>

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
void equivalentOnAllStrings(const automaton::Automaton<AlphabetSize>& a, const automaton::Automaton<AlphabetSize>& b, int, int = -1) {
	automaton::ComparisonResult<AlphabetSize> cmp = compare_languages(a, b);
	CHECK_UNARY(cmp.equal()); //TODO: on failure, print some witnesses
}

#endif // AUTOMATON_TEST_UTIL_HPP_INCLUDED
