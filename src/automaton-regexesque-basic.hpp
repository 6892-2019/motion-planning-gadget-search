// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
#ifndef AUTOMATON_REGEXESQUE_BASIC_HPP
#define AUTOMATON_REGEXESQUE_BASIC_HPP

//Simple regexesque functions that automaton.tcc uses but most automaton.hpp
//clients don't use directly.

#include "automaton.hpp"

namespace automaton {

/**
 * Returns an Automaton that accepts the empty language.
 */
template<unsigned int N>
Automaton<N> empty() {
	Automaton<N> a;
	a.addState();
	a.minimal_ = a.canonical_ = true;
	return a;
}

/**
 * Returns an Automaton that accepts the language of all strings.
 */
template<unsigned int N>
Automaton<N> all() {
	Automaton<N> a;
	a.addState();
	a.setAccept(0);
	for (typename Automaton<N>::symbol_type s = 0; s < N; ++s)
		a.addTrans(0, s, 0);
	a.minimal_ = a.canonical_ = true;
	return a;
}

/**
 * Returns an Automaton that accepts only the empty string.
 */
template<unsigned int N>
Automaton<N> epsilon() {
	Automaton<N> a;
	a.addState();
	a.setAccept(0);
	a.minimal_ = a.canonical_ = true;
	return a;
}

} //namespace automaton

#endif /* AUTOMATON_REGEXESQUE_BASIC_HPP */

