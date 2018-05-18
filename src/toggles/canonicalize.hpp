/*
 * File:   canonicalize.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on April 15, 2017, 9:37 PM
 */

#ifndef CANONICALIZE_HPP
#define CANONICALIZE_HPP

#include "automaton.hpp"

[[gnu::const]]
std::pair<const unsigned int* const*, const unsigned int* const*>
getPerms(unsigned int alphabetSize, unsigned int locations, bool normal, bool mirrored);

template<unsigned int N>
void canonicalize(automaton::Automaton<N>& a, const unsigned int locations, bool allowMirroring = true) {
	if (locations == 1) {
		//We can't renumber the symbols/locations because there's only one, but
		//we still have to renumber the states.
		a.canonicalize();
		return;
	}
	auto perms = getPerms(a.alphabet_size(), locations, true, allowMirroring);
	a.canonicalizeRenumber(perms.first, perms.second);
}

#endif /* CANONICALIZE_HPP */

