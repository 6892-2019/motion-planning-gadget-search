#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "automaton.hpp"
#include "ioutils.hpp"
#include "automaton-io.hpp"

using namespace automaton;
//'clock' in the global namespace is already defined, sigh
using myclock = std::chrono::high_resolution_clock;

int main(int argc, const char* argv[]) { //genbuild entrypoint
	std::vector<std::unique_ptr<WorkingAutomaton>> automata;
	auto start = myclock::now();
	std::size_t states_before = 0, transitions_before = 0;
	for (auto& filename : processFilenameArgs(argv+1, argv+argc)) {
		automata.push_back(deserialize(std::move(filename)));
		states_before += automata.back()->state_size();
		transitions_before += automata.back()->transition_size();
	}
	auto end = myclock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	fmt::print("loaded {} automata in {}ms\n", automata.size(), elapsed);

	start = myclock::now();
	for (auto& pa : automata)
		pa->optimize();
	end = myclock::now();

	elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	std::size_t hash = 0;
	std::size_t states_after = 0, transitions_after = 0;
	//Print the hash a) to prevent the benchmark from being optimized out and
	//b) so we can tell if our optimizations changed the result or not.
	for (auto& pa : automata) {
		hash += pa->working_hash();
		states_after += pa->state_size();
		transitions_after += pa->transition_size();
	}
	auto states_saved = states_before - states_after,
			transitions_saved = transitions_before - transitions_after;
	auto states_mult = ((double)states_saved)/(double)states_before,
			transitions_mult = ((double)transitions_saved)/(double)transitions_after;
	fmt::print("{}ms, saved {} ({:.3f}) states and {} ({:.3f}) transitions, {}\n", elapsed,
			states_before - states_after, states_mult,
			transitions_before - transitions_after, transitions_mult, hash);

	return 0;
}