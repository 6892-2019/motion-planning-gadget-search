#include "precompiled.hpp"
#include "automatonbase.hpp"
#include "automaton.hpp"
#include "ioutils.hpp"
#include "automaton-io.hpp"
#include "stopwatch.hpp"

using namespace automaton;
using std::vector;
using std::unique_ptr;
using std::pair;
using std::string_view;
using namespace std::literals::string_view_literals;

using AutomatonContainer = vector<unique_ptr<WorkingAutomaton>>;

template<void(WorkingAutomaton::*func)()>
void nullary_memfun_loop(AutomatonContainer& automata) {
	for (auto& pa : automata)
		((*pa).*func)();
}

const pair<string_view, string_view> alias_table[] = {
	{"rds"sv, "removedeadstates"},
};
string_view translate_alias(string_view name) {
	for (auto [from, to] : alias_table)
		if (name == from)
			return to;
	return name;
}

using loop_ptr = void(*)(AutomatonContainer&);
const pair<string_view, loop_ptr> operations[] = {
	{"removedeadstates"sv, &nullary_memfun_loop<&WorkingAutomaton::removeDeadStates>},
	{"totalize"sv, &nullary_memfun_loop<&WorkingAutomaton::totalize>},
	{"determinize"sv, &nullary_memfun_loop<&WorkingAutomaton::determinize>},
	{"minimize"sv, &nullary_memfun_loop<&WorkingAutomaton::minimize>},
	{"canonicalize"sv, &nullary_memfun_loop<&WorkingAutomaton::canonicalize>},
	{"optimize"sv, &nullary_memfun_loop<&WorkingAutomaton::optimize>},
};
loop_ptr find_operation(string_view name) {
	name = translate_alias(name);
	for (auto [n, p] : operations)
		if (name == n)
			return p;
	fmt::print("unrecognized operation {}\n", name);
	std::exit(1);
}

int main(int argc, const char* argv[]) { //genbuild entrypoint
	loop_ptr operation = find_operation(argv[1]);

	vector<unique_ptr<WorkingAutomaton>> automata;
	auto stopwatch = Stopwatch::process();
	std::size_t states_before = 0, transitions_before = 0;
	for (auto& filename : processFilenameArgs(argv+2, argv+argc)) {
		automata.push_back(deserialize(std::move(filename)));
		states_before += automata.back()->state_size();
		transitions_before += automata.back()->transition_size();
	}
	auto elapsed = stopwatch.elapsed();
	fmt::print("loaded {} automata in {}ms, {:.3f} GB\n", automata.size(), elapsed.millis(), elapsed.highwaterGibibytes());

	stopwatch.reset();
	operation(automata);
	elapsed = stopwatch.elapsed();

	std::size_t hash = 0;
	std::size_t states_after = 0, transitions_after = 0;
	//Print the hash a) to prevent the benchmark from being optimized out and
	//b) so we can tell if our optimizations changed the result or not.
	for (auto& pa : automata) {
		hash += pa->working_hash();
		states_after += pa->state_size();
		transitions_after += pa->transition_size();
	}
	std::ptrdiff_t states_delta = ((std::ptrdiff_t)states_after) - states_before,
			transitions_delta = ((std::ptrdiff_t)transitions_after) - transitions_before;
	auto states_mult = ((double)states_after)/(double)states_before,
			transitions_mult = ((double)transitions_after)/(double)transitions_after;
	fmt::print("{}ms, {:.3f} GB, {:+} ({:.3f}x) states, {:+} ({:.3f}x) transitions, {}\n",
			elapsed.millis(), elapsed.highwaterGibibytes(),
			states_delta, states_mult, transitions_delta, transitions_mult,
			hash);
	return 0;
}