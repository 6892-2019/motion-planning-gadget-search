#include "precompiled.hpp"
#include "randaut.hpp"
#include "../toggles/canonicalize.hpp"

using namespace automaton;

int main(int argc, char* argv[]) {
	std::mt19937 rng(std::atoi(argv[1]));

	for (int trials = 0; trials < 1; ++trials) {
		auto randaut = generate<4>(rng);

		std::cout << randaut << randaut.repr() << std::endl;
		randaut.minimize();
//		canonicalize(randaut, randaut.alphabet_size());
		std::cout << randaut << std::endl;
		std::vector<AutomatonBase::state_type> states;
		states.assign(boost::make_counting_iterator<AutomatonBase::state_type>(0),
				boost::make_counting_iterator<AutomatonBase::state_type>(randaut.state_size()));
		std::vector<AutomatonBase::symbol_type> symbols;

		std::vector<Automaton<4>> rotations;
		rotations.reserve(randaut.alphabet_size());
		for (AutomatonBase::symbol_type rotDist = 0; rotDist < randaut.alphabet_size(); ++rotDist) {
			auto copy = randaut;
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
			rotations.push_back(std::move(copy));

//			auto cmp = compare_languages(randaut, copy);
//			//TODO: we actually need == (structural equality), but same-language is a good start!
//			if (!cmp.equal()) {
//				std::cout << "trial " << rotDist << " found mismatch:\n" << cmp.leftButNotRight << "\n" << cmp.rightButNotLeft << "\n" << std::endl;
//			}
		}

		std::unordered_set<std::size_t> hashes;
		for (const auto& r : rotations)
			hashes.insert(std::hash<decltype(randaut)>()(r));
		std::cout << hashes.size() << std::endl;
	}

	return 0;
}