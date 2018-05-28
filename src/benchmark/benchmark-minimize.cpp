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
	for (auto& filename : processFilenameArgs(argv+1, argv+argc))
		automata.push_back(deserialize(std::move(filename)));
	std::cout << "loaded " << automata.size() << " automata\n";

	auto start = myclock::now();
	for (auto& pa : automata)
		pa->minimize();
	auto end = myclock::now();

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	std::size_t hash = 0;
	//Print the hash a) to prevent the benchmark from being optimized out and
	//b) so we can tell if our optimizations changed the result or not.
	for (auto& pa : automata)
		hash += pa->working_hash();
	std::cout << elapsed << "ms, " << hash << "\n";

	return 0;
}