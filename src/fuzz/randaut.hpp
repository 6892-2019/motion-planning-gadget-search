// SPDX-License-Identifier: MIT
// Copyright 2017 Massachusetts Institute of Technology
#ifndef AUTOMATON_FUZZ_RANDAUT_HPP_INCLUDED
#define AUTOMATON_FUZZ_RANDAUT_HPP_INCLUDED

#include "automaton.hpp"

namespace automaton {

template<unsigned int N, typename RNG>
Automaton<N> generate(RNG& rng) {
	constexpr int states = 10;
	Automaton<N> a;
	std::uniform_int_distribution<AutomatonBase::state_type> state(0, states-1);
	std::uniform_int_distribution<AutomatonBase::symbol_type> symbol(0, N-1);
	std::bernoulli_distribution accept(.40);
	for (int i = 0; i < states; ++i) {
		a.addState();
		a.setAccept(i, accept(rng));
	}
	for (int i = 0; i < 15; ++i)
		a.addTrans(state(rng), symbol(rng), state(rng));
	return a;
}

/**
 * Generates a diagonal automaton on N symbols.  The diagonal automaton accepts
 * the string 0, 1, 2, 3... n-1 only.
 */
template<unsigned int N>
Automaton<N> generate_diagonal() {
	Automaton<N> a;
	a.addState();
	for (AutomatonBase::symbol_type s = 0; s < N; ++s)
		a.addTrans(s, s, a.addState());
	a.setAccept(a.state_size()-1);
	return a;
}

} //namespace automaton

#endif // AUTOMATON_FUZZ_RANDAUT_HPP_INCLUDED