// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
#ifndef CANONICALIZE_HPP
#define CANONICALIZE_HPP

#include "automaton.hpp"

[[gnu::const]]
std::pair<const unsigned int* const*, const unsigned int* const*>
getPerms(unsigned int alphabetSize, unsigned int locations, bool normal, bool mirrored);

namespace automaton {
namespace detail {
bool addMaximalNops(automaton::WorkingAutomaton& a, unsigned int locations);
}
}

template<unsigned int N>
unsigned int canonicalize(automaton::Automaton<N>& a, const unsigned int locations, bool allowMirroring = true) {
	automaton::detail::addMaximalNops(a, locations);
	if (locations == 1) {
		//We can't renumber the symbols/locations because there's only one, but
		//we still have to renumber the states.
		a.canonicalize();
		return 0;
	}
	auto perms = getPerms(a.alphabet_size(), locations, true, allowMirroring);
	auto used = a.canonicalizeRenumber(perms.first, perms.second);
	auto iter_index = std::find(perms.first, perms.second, used);
	assert(iter_index != perms.second);
	return numeric_cast<unsigned int>(iter_index - perms.first);
}
unsigned int canonicalize(automaton::WorkingAutomaton& a, const unsigned int locations, bool allowMirroring = true);

template<unsigned int N>
[[nodiscard]] std::pair<automaton::Automaton<N>, unsigned int> mirror(const automaton::Automaton<N>& a, unsigned int locations) {
	automaton::Automaton<N> b = a;
	automaton::detail::addMaximalNops(b, locations);
	if (locations == 1) {
		//We can't renumber the symbols/locations because there's only one, but
		//we still have to renumber the states.
		b.canonicalize();
		return {b, 0};
	} else if (locations == 2) {
		//In this special case, the mirrored and mirrored perms are the same.
		//It's just a regular canonicalize.
		unsigned int index = canonicalize(b, locations);
		return {b, index};
	}
	auto perms = getPerms(b.alphabet_size(), locations, false, true); //mirrored perms only
	auto used = b.canonicalizeRenumber(perms.first, perms.second);
	auto iter_index = std::find(perms.first, perms.second, used);
	assert(iter_index != perms.second);
	//We add locations to the index because the mirrored permutations come after
	//the regular permutations.  This lets us map an index to a permutation
	//without worrying about the context.
	return {b, numeric_cast<unsigned int>(iter_index - perms.first) + locations};
}
template<unsigned int N>
[[nodiscard]] std::pair<automaton::Automaton<N>, unsigned int> mirror(const automaton::Automaton<N>& a) {
	return mirror(a, a.active_alphabet_size());
}
[[nodiscard]] std::pair<std::unique_ptr<automaton::WorkingAutomaton>, unsigned int> mirror(const automaton::WorkingAutomaton& a);

#endif /* CANONICALIZE_HPP */

