#include "precompiled.hpp"
#include "registry.hpp"

constexpr Registry::index_type Registry::ABSENT;

automaton_type mirror(const automaton_type& a, unsigned int locations) {
	//TODO: precompute and reuse for 2..automaton_type::alphabet_size_v
	std::vector<AutomatonBase::symbol_type> symbols(
			boost::make_counting_iterator<AutomatonBase::symbol_type>(0),
			boost::make_counting_iterator<AutomatonBase::symbol_type>(automaton_type::alphabet_size_v));
	std::reverse(symbols.begin(), symbols.begin()+locations);
	automaton_type b = a;
	b.renumberAlphabet(symbols);
	canonicalize(b, locations, false);
	return b;
}