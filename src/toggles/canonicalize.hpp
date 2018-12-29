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
void canonicalize(automaton::WorkingAutomaton& a, const unsigned int locations, bool allowMirroring = true);

template<unsigned int N>
[[nodiscard]] automaton::Automaton<N> mirror(const automaton::Automaton<N>& a, unsigned int locations) {
	automaton::Automaton<N> b = a;
	if (locations == 1) {
		//We can't renumber the symbols/locations because there's only one, but
		//we still have to renumber the states.
		b.canonicalize();
		return b;
	} else if (locations == 2) {
		//In this special case, the mirrored and mirrored perms are the same.
		//It's just a regular canonicalize.
		canonicalize(b, locations);
		return b;
	}
	auto perms = getPerms(b.alphabet_size(), locations, false, true); //mirrored perms only
	b.canonicalizeRenumber(perms.first, perms.second);
	return b;
}
template<unsigned int N>
[[nodiscard]] automaton::Automaton<N> mirror(const automaton::Automaton<N>& a) {
	return mirror(a, a.active_alphabet_size());
}
[[nodiscard]] std::unique_ptr<automaton::WorkingAutomaton> mirror(const automaton::WorkingAutomaton& a);

#endif /* CANONICALIZE_HPP */

