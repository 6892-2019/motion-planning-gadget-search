#include "precompiled.hpp"
#include "randaut.hpp"
#include "../toggles/canonicalize.hpp"

using namespace automaton;

int main(int argc, char* argv[]) {
	std::mt19937 rng(std::atoi(argv[1]));

	for (int trials = 0; trials < 1; ++trials) {
		auto randaut = generate<4>(rng);
//		auto randaut = generate_diagonal<4>();

		std::cout << randaut << randaut.repr() << std::endl;
		randaut.minimize();
//		canonicalize(randaut, randaut.alphabet_size());
		std::cout << randaut << std::endl;
		std::vector<AutomatonBase::state_type> states, states2;
		std::vector<AutomatonBase::symbol_type> symbols;

		std::vector<Automaton<4>> rotations;
		rotations.reserve(randaut.alphabet_size());
		for (AutomatonBase::symbol_type rotDist = 0; rotDist < 1; ++rotDist) {
			auto copy = randaut;
			states.assign(boost::make_counting_iterator<AutomatonBase::state_type>(0),
					boost::make_counting_iterator<AutomatonBase::state_type>(randaut.state_size()));
			std::shuffle(states.begin(), states.end(), rng);
			std::iter_swap(states.begin(), std::find(states.begin(), states.end(), 0));
			states2 = states;
			symbols.assign(boost::make_counting_iterator<AutomatonBase::symbol_type>(0),
					boost::make_counting_iterator<AutomatonBase::symbol_type>(randaut.alphabet_size()));
			std::rotate(symbols.begin(), symbols.begin()+rotDist, symbols.end());
			copy.renumber(states.begin(), symbols.begin());
			std::cout << copy << std::endl;
			for (auto x : symbols)
				std::cout << x << " ";
			std::cout << std::endl;
//			copy.minimize();
			canonicalize(copy, copy.alphabet_size());
			std::cout << copy << std::endl;
//			rotations.push_back(std::move(copy));

//			auto cmp = compare_languages(randaut, copy);
//			//TODO: we actually need == (structural equality), but same-language is a good start!
//			if (!cmp.equal()) {
//				std::cout << "trial " << rotDist << " found mismatch:\n" << cmp.leftButNotRight << "\n" << cmp.rightButNotLeft << "\n" << std::endl;
//			}
		}

		std::cout << "end rotations" << std::endl;
		symbols.assign(boost::make_counting_iterator<AutomatonBase::symbol_type>(0),
				boost::make_counting_iterator<AutomatonBase::symbol_type>(randaut.alphabet_size()));
		int count = 0;
		do {
			symbols.assign(boost::make_counting_iterator<AutomatonBase::symbol_type>(0),
					boost::make_counting_iterator<AutomatonBase::symbol_type>(randaut.alphabet_size()));
			do {
				if (is_possibly_mirrored_rotation_permutation(symbols.begin(), symbols.end()))
					continue;
//				std::cout << "states: ";
//				for (auto x : states)
//					std::cout << x << " ";
//				std::cout << "\nsymbols: ";
//				for (auto x : symbols)
//					std::cout << x << " ";
//				std::cout << std::endl;
				auto copy2 = randaut;
				states = states2;
				copy2.renumber(states.begin(), symbols.begin());
				canonicalize(copy2, copy2.alphabet_size());
				++count;
				std::cout << copy2 << std::endl;
				std::cout << "hash: " << std::hash<decltype(copy2)>()(copy2) << std::endl;
			} while (std::next_permutation(symbols.begin(), symbols.end()));
//			} while (false);
		} while (std::next_permutation(states.begin()+1, states.end()));
		std::cout << count << " total" << std::endl;
	}

	return 0;
}