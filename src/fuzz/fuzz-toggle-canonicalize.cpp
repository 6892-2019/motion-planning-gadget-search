#include "precompiled.hpp"
#include "randaut.hpp"
#include "../toggles/canonicalize.hpp"

using namespace automaton;

int main(int argc, char* argv[]) {
	std::mt19937 rng(10);

	Automaton<8> a = generate<8>(rng);
	a.minimize();
	canonicalize(a, a.alphabet_size());
	std::vector<AutomatonBase::state_type> states;
	std::vector<AutomatonBase::symbol_type> symbols;
	std::uniform_int_distribution<> symbolRotateDistance(0, a.alphabet_size()-1);

	int failures = 0;
	for (int i = 0; i < 1000; ++i) {
		states.assign(boost::make_counting_iterator<AutomatonBase::state_type>(0),
			boost::make_counting_iterator<AutomatonBase::state_type>(a.state_size()));
		symbols.assign(boost::make_counting_iterator<AutomatonBase::symbol_type>(0),
			boost::make_counting_iterator<AutomatonBase::symbol_type>(a.alphabet_size()));
		auto copy = a;
		std::shuffle(states.begin(), states.end(), rng);
		std::iter_swap(states.begin(), std::find(states.begin(), states.end(), 0));
		std::rotate(symbols.begin(), symbols.begin()+symbolRotateDistance(rng), symbols.end());
		copy.renumber(states.begin(), symbols.begin());
		copy.minimize();
		canonicalize(copy, copy.alphabet_size());

		auto cmp = compare_languages(a, copy);
		//TODO: we actually need == (structural equality), but same-language is a good start!
		if (!cmp.equal()) {
			std::cout << "trial " << i << " found mismatch:\n" << cmp.leftButNotRight << "\n" << cmp.rightButNotLeft << std::endl;
			if ((++failures) >= 5)
				std::exit(0);
		}
	}

	return 0;
}