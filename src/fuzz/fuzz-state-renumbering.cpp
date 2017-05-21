#include "precompiled.hpp"
#include "randaut.hpp"

using namespace automaton;

int main(int argc, char* argv[]) {
	std::mt19937 rng(std::atoi(argv[1]));

	for (int trials = 0; trials < 1000; ++trials) {
		auto randaut = generate<4>(rng);
		std::vector<AutomatonBase::state_type> states(randaut.state_size());
		std::iota(states.begin(), states.end(), 0);
		do {
			auto copy = randaut;
			auto states2 = states;
			copy.renumberStates(states2.begin());
			if (!same_language(randaut, copy)) {
				std::cout << randaut.repr() << std::endl;
				for (auto x : states)
					std::cout << x << ", ";
				std::cout << std::endl;
				exit(1);
			}
		} while (std::next_permutation(states.begin()+1, states.end()));
		std::cout << "survived " << trials << std::endl;
	}
	return 0;
}