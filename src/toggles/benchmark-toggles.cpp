#include "precompiled.hpp"
#include "automaton.hpp"
#include "packedautomaton.hpp"
#include "ops.hpp"
#include "ioutils.hpp"
#include "automaton-io.hpp"
#include "hopscotch/hopscotch_set.h"
#include <string_view>

using namespace automaton;
using namespace std::literals::string_view_literals;
using std::vector;
using std::unique_ptr;
using std::string;
using std::string_view;
//'clock' in the global namespace is already defined, sigh
using myclock = std::chrono::high_resolution_clock;

using automaton_type = Automaton<8>; //TODO: would prefer this as a command-line arg...
using index_type = unsigned int;
template<class PackPointer>
using ClosedSet = tsl::hopscotch_set<PackPointer,
		indirect_hash, indirect_equal, std::allocator<PackPointer>,
		30, true /* store the hash */>;
using NonowningClosedSet = ClosedSet<const PackedAutomaton*>;

namespace {
vector<unique_ptr<WorkingAutomaton>> load_automata(const char** first, const char** last) {
	vector<string> filenames = processFilenameArgs(first, last);
	vector<unique_ptr<WorkingAutomaton>> result;
	for (auto& filename : filenames)
		result.push_back(deserialize(std::move(filename)));
	return result;
}

vector<unique_ptr<const PackedAutomaton>> pack_all(const std::vector<unique_ptr<WorkingAutomaton>>& working) {
	vector<unique_ptr<const PackedAutomaton>> p;
	for (const auto& w : working) {
		automaton_type trash(*w);
		canonicalize(trash, trash.active_alphabet_size());
		p.push_back(pack(trash));
	}
	return p;
}

struct Finisher {
	Finisher() : pruned(0) {}
	vector<unique_ptr<const PackedAutomaton>> nextgen;
	NonowningClosedSet closed;
	unsigned int pruned;
	void operator()(automaton_type&& a, Provenance p) {
		canonicalize(a, a.active_alphabet_size());
		auto packed = pack(a);
		auto hash = packed->packed_hash();
		if (closed.insert(packed.get()).second) { //TODO: insert overload taking the hash
			nextgen.emplace_back(std::move(packed));
		} else
			++pruned;
	}
};

void combine_once(const PackedAutomaton* left, const PackedAutomaton* rightA, Finisher& finishAction) {
	automaton_type unpacked(*left);
	//TODO: we don't usually have to unpack or mirror the inputs, so this isn't
	//a perfectly accurate benchmark.
	automaton_type right(*rightA);
	automaton_type::symbol_type leftLocations = unpacked.active_alphabet_size();
	automaton_type::symbol_type rightLocations = right.active_alphabet_size();
	if (leftLocations + rightLocations > automaton_type::alphabet_size_v) return;
	//TODO: should only mirror(unpacked) if !rightMir
	automaton_type mirrored = mirror(unpacked), rightMir = mirror(right);
	bool mirrorRight = right != rightMir;
	bool mirrorLeft = !mirrorRight && unpacked != mirrored;
	combine(unpacked, 0, false, leftLocations, right, 0, false, rightLocations, finishAction);
	if (mirrorRight)
		combine(unpacked, 0, false, leftLocations, rightMir, 0, true, rightLocations, finishAction);
	if (mirrorLeft)
		combine(mirrored, 0, true, leftLocations, right, 0, false, rightLocations, finishAction);
}

int benchmark_combine(int argc, const char* argv[]) {
	if (argc < 3) {
		std::cout << "specify at least one automaton file or response file\n";
		return 1;
	}
	auto packed = pack_all(load_automata(&argv[2], &argv[argc]));
	std::cout << "loaded " << packed.size() << " automata\n";
	Finisher finisher;

	auto start = myclock::now();
	for (const auto& p : packed)
		for (const auto& q : packed)
			combine_once(p.get(), q.get(), finisher);
	auto end = myclock::now();

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	std::size_t hash = 0;
	//Print the hash a) to prevent the benchmark from being optimized out and
	//b) so we can tell if our optimizations changed the result or not.
	for (auto& pa : finisher.nextgen)
		hash += pa->packed_hash();
	std::cout << elapsed << " microseconds, "
			<< finisher.nextgen.size() << " results, "
			<< finisher.pruned << " pruned, "
			<< hash << std::endl;
	return 0;
}

void connect_once(const PackedAutomaton* source, index_type sourceIndex, Finisher& finishAction) {
	automaton_type inflated(*source);
	automaton_type::symbol_type locations = inflated.active_alphabet_size();
	connect(inflated, sourceIndex, false, locations, finishAction);
}

int benchmark_connect(int argc, const char* argv[]) {
	if (argc < 3) {
		std::cout << "specify at least one automaton file or response file\n";
		return 1;
	}
	auto packed = pack_all(load_automata(&argv[2], &argv[argc]));
	std::cout << "loaded " << packed.size() << " automata\n";
	Finisher finisher;

	auto start = myclock::now();
	for (const auto& p : packed)
		connect_once(p.get(), 0, finisher);
	auto end = myclock::now();

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();;
	std::size_t hash = 0;
	//Print the hash a) to prevent the benchmark from being optimized out and
	//b) so we can tell if our optimizations changed the result or not.
	for (auto& pa : finisher.nextgen)
		hash += pa->packed_hash();
	std::cout << elapsed << " microseconds, "
			<< finisher.nextgen.size() << " results, "
			<< finisher.pruned << " pruned, "
			<< hash << std::endl;
	return 0;
}

int benchmark_mirror(int argc, const char* argv[]) {
	if (argc < 3) {
		std::cout << "specify at least one automaton file or response file\n";
		return 1;
	}
	auto working_ptrs = load_automata(&argv[2], &argv[argc]);
	vector<automaton_type> working;
	for (auto& p : working_ptrs) {
		working.emplace_back(*p);
		canonicalize(working.back(), working.back().active_alphabet_size());
	}
	std::cout << "loaded " << working.size() << " automata\n";
	Finisher finisher;

	vector<automaton_type> result;
	result.reserve(working.size());
	auto start = myclock::now();
	for (const auto& a : working) {
		result.push_back(mirror(a));
	}
	auto end = myclock::now();

	unsigned int chiral = 0;
	for (unsigned int i = 0; i < working.size(); ++i) {
		if (result[i] != working[i])
			++chiral;
		finisher(std::move(result[i]), Provenance(0)); //just for hashing/stats
	}

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();;
	std::size_t hash = 0;
	//Print the hash a) to prevent the benchmark from being optimized out and
	//b) so we can tell if our optimizations changed the result or not.
	for (auto& pa : finisher.nextgen)
		hash += pa->packed_hash();
	std::cout << elapsed << " microseconds, " << chiral << " chiral "
			<< finisher.nextgen.size() << " results, "
			<< finisher.pruned << " pruned, "
			<< hash << std::endl;
	return 0;
}
}

int main(int argc, const char* argv[]) { //genbuild entrypoint
	if (argc < 2) {
		std::cout << "need more args\n";
		return 1;
	}

	if (argv[1] == "combine"sv)
		return benchmark_combine(argc, argv);
	else if (argv[1] == "connect"sv)
		return benchmark_connect(argc, argv);
	else if (argv[1] == "mirror"sv)
		return benchmark_mirror(argc, argv);
	else {
		std::cout << "bad mode " << argv[1] << std::endl;
		return 1;
	}
}