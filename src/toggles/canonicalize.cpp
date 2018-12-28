#include "precompiled.hpp"
#include "canonicalize.hpp"

using namespace automaton;

namespace {
#include "canonicalize-perms-inl.hpp"
}

[[gnu::const]]
std::pair<const unsigned int* const*, const unsigned int* const*>
getPerms(unsigned int alphabetSize, unsigned int locations, bool normal, bool mirrored) {
	if (normal && mirrored)
		return allperms[alphabetSize][locations];
	if (normal)
		return m0perms[alphabetSize][locations];
	if (mirrored)
		return m1perms[alphabetSize][locations];
	__builtin_unreachable();
}

void canonicalize(WorkingAutomaton& a, const unsigned int locations, bool allowMirroring) {
	switch (a.alphabet_size()) {
		#define GADGETDEFS_CANONICALIZE_CASE(N) case N: canonicalize(static_cast<Automaton<N>&>(a), a.active_alphabet_size(), allowMirroring); break;
		GADGETDEFS_CANONICALIZE_CASE(1)
		GADGETDEFS_CANONICALIZE_CASE(2)
		GADGETDEFS_CANONICALIZE_CASE(3)
		GADGETDEFS_CANONICALIZE_CASE(4)
		GADGETDEFS_CANONICALIZE_CASE(5)
		GADGETDEFS_CANONICALIZE_CASE(6)
		GADGETDEFS_CANONICALIZE_CASE(7)
		GADGETDEFS_CANONICALIZE_CASE(8)
		GADGETDEFS_CANONICALIZE_CASE(9)
		GADGETDEFS_CANONICALIZE_CASE(10)
		GADGETDEFS_CANONICALIZE_CASE(11)
		GADGETDEFS_CANONICALIZE_CASE(12)
		GADGETDEFS_CANONICALIZE_CASE(13)
		GADGETDEFS_CANONICALIZE_CASE(14)
		GADGETDEFS_CANONICALIZE_CASE(15)
		GADGETDEFS_CANONICALIZE_CASE(16)
	#undef GADGETDEFS_CANONICALIZE_CASE
		default:
			fmt::print(stderr, "unhandled canonicalize for alphabet size {}, typeid {}\n",
					a.alphabet_size(), typeid(a).name());
			std::terminate();
	}
}
