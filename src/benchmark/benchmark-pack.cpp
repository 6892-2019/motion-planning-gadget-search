#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "automaton.hpp"
#include "automaton-io.hpp"
#include "packedautomaton.hpp"

using namespace automaton;
//'clock' in the global namespace is already defined, sigh
using myclock = std::chrono::high_resolution_clock;

int main(int argc, char* argv[]) { //genbuild entrypoint
	std::vector<std::unique_ptr<WorkingAutomaton>> automata;
	for (int i = 1; i < argc; ++i)
		automata.push_back(deserialize(argv[i]));

	std::vector<std::unique_ptr<const PackedAutomaton>> packs;
	packs.reserve(automata.size());
	auto start = myclock::now();
	for (auto& pa : automata)
		packs.push_back(pack(*pa));
	auto end = myclock::now();

	std::cout << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << "\n";
	std::size_t hash = 0;
	//Print the hash a) to prevent the benchmark from being optimized out and
	//b) so we can tell if our optimizations changed the result or not.
	for (auto& pa : packs)
		hash += pa->packed_hash();
	std::cout << hash << "\n";

	return 0;
}