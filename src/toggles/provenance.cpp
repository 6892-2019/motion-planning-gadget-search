#include "precompiled.hpp"
#include "provenance.hpp"

std::ostream& operator<<(std::ostream& o, Provenance& p) {
	return o << fmt::format("{}", p);
}