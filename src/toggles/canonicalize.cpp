#include "precompiled.hpp"

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