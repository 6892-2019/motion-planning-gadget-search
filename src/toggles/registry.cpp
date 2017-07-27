#include "precompiled.hpp"
#include "registry.hpp"

automaton_type mirror(const automaton_type& a, unsigned int locations) {
	//TODO: precompute and reuse for 2..automaton_type::alphabet_size_v
	//TODO: can we replace this with canonicalize-renumber using mirrored perms only?
	std::vector<AutomatonBase::symbol_type> symbols(
			boost::make_counting_iterator<AutomatonBase::symbol_type>(0),
			boost::make_counting_iterator<AutomatonBase::symbol_type>(automaton_type::alphabet_size_v));
	std::reverse(symbols.begin(), symbols.begin()+locations);
	automaton_type b = a;
	b.renumberAlphabet(symbols);
	canonicalize(b, locations, false);
	return b;
}

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