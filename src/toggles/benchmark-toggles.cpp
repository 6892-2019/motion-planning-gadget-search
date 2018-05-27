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

void connect_once(const PackedAutomaton* source, index_type sourceIndex, Finisher& finishAction) {
	automaton_type inflated(*source);
	automaton_type::symbol_type locations = inflated.active_alphabet_size();
	connect(inflated, sourceIndex, false, locations, finishAction);
}

vector<unique_ptr<WorkingAutomaton>> load_automata(char** first, char** last) {
	vector<string> queue(first, last);
	std::reverse(queue.begin(), queue.end());
	vector<unique_ptr<WorkingAutomaton>> result;
	while (!queue.empty()) {
		string filename = std::move(queue.back());
		queue.pop_back();
		if (filename[0] == '@') { //response file
			//TODO: enough filename smarts to resolve paths relative to the
			//directory containing the response file
			filename.erase(0, 1);
			vector<string> lines = readAllLines(filename);
			std::reverse(lines.begin(), lines.end());
			queue.insert(queue.end(), std::move_iterator(lines.begin()), std::move_iterator(lines.end()));
		} else
			result.push_back(deserialize(filename));
	}
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

int benchmark_connect(int argc, char* argv[]) {
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

int benchmark_mirror(int argc, char* argv[]) {
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

int main(int argc, char* argv[]) { //genbuild entrypoint
	if (argc < 2) {
		std::cout << "need more args\n";
		return 1;
	}

	if (argv[1] == "connect"sv)
		return benchmark_connect(argc, argv);
	else if (argv[1] == "mirror"sv)
		return benchmark_mirror(argc, argv);
	else {
		std::cout << "bad mode " << argv[1] << std::endl;
		return 1;
	}
}