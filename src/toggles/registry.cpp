#include "precompiled.hpp"
#include "registry.hpp"

std::ostream& operator<<(std::ostream& o, Provenance& p) {
	if (p.isInput())
		return (o << "input " << p.second);
	else if (p.isConnect())
		o << "connect " << p.first << (p.i & Provenance::TOPBIT ? "m" : "")
				<< " at " << (p.i & ~Provenance::TOPBIT) << " start " << p.j;
	else if (p.isCombine())
		o << "combine " << p.first << (p.i & Provenance::TOPBIT ? "m" : "") << " at " << (p.i & ~Provenance::TOPBIT)
				<< " with " << p.second << (p.j & Provenance::TOPBIT ? "m" : "") << " at " << (p.j & ~Provenance::TOPBIT);
	return o;
}