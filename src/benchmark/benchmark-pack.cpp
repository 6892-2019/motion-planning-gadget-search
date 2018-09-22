#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "automaton.hpp"
#include "automaton-io.hpp"
#include "pack.hpp"

using namespace automaton;
//'clock' in the global namespace is already defined, sigh
using myclock = std::chrono::high_resolution_clock;

int main(int argc, char* argv[]) { //genbuild entrypoint
	std::vector<std::unique_ptr<WorkingAutomaton>> automata;
	for (int i = 1; i < argc; ++i)
		automata.push_back(deserialize(argv[i]));

	dynarray<std::byte> data(1024*1024*1024);
	std::vector<const Pack*> packs;
	packs.reserve(automata.size());
	std::byte* cur = data.begin();
	auto start = myclock::now();
	for (auto& pa : automata) {
		packs.push_back(cur);
		cur = pack(*pa, cur, data.end());
	}
	auto end = myclock::now();

	std::cout << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << "\n";
	std::size_t hash = 0;
	//Print the hash a) to prevent the benchmark from being optimized out and
	//b) so we can tell if our optimizations changed the result or not.
	for (auto p : packs)
		hash += packed_hash(p);
	std::cout << hash << "\n";
	std::cout << (cur - data.begin()) << "\n";

	return 0;
}