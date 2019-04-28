#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "automaton.hpp"
#include "automaton-io.hpp"
#include "pack.hpp"
#include "ioutils.hpp"

using namespace automaton;
//'clock' in the global namespace is already defined, sigh
using myclock = std::chrono::high_resolution_clock;

int main(int argc, const char* argv[]) { //genbuild {'entrypoint': True}
	std::vector<std::string> filenames = processFilenameArgs(argv+1, argv+argc);
	std::vector<std::unique_ptr<WorkingAutomaton>> automata;
	for (auto& filename : filenames) {
		automata.push_back(deserialize(filename));
		automata.back()->canonicalize();
	}

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
	for (auto i : xrange(packs.size())) {
		const Pack* p = packs[i];
		hash += packed_hash(p);
		auto unpacked = make_working(automata[i]->alphabet_size());
		auto unpack_result = unpack(*unpacked, p, i+1 < packs.size() ? packs[i+1] : cur);
		if (!unpack_result || *unpacked != *automata[i])
			std::cout << "roundtrip problem: " << filenames[i] << std::endl;
	}
	std::cout << hash << "\n";
	std::cout << (cur - data.begin()) << "\n";

	return 0;
}