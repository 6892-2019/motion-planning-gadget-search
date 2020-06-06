#include "precompiled.hpp"
#include "regex.hpp"
#include "alphabet.hpp"
#include "ioutils.hpp"
#include "stringutils.hpp"
#include "hopscotch/hopscotch_map.h"
#include <boost/algorithm/string/trim.hpp>

using std::vector;
using namespace std::literals::string_view_literals;

using R = automaton::Regex<BooleanAlphabet>;

static std::size_t random_seed;

struct Problem {
	vector<vector<int>> clauses; //each clause sorted by absolute value
	unsigned int variables; //numbered 1 to n because negative zero isn't a thing
};

Problem parse_dimacs(const char* filename) {
	vector<std::string> lines = readAllLines(filename);
	Problem problem;
	problem.variables = 0;
	for (std::string& line : lines) {
		if (line[0] == 'c' || line[0] == 'p') continue;
		if (line[0] == '%') break; //some files end with this? it isn't an official part of the format
		boost::trim(line);
		std::vector<std::string_view> literals = split_view(line, ' ');
		problem.clauses.push_back({});
		for (std::string_view l : literals) {
			int x = to_int(l);
			if (x == 0) break; //assuming at most one clause per line
			problem.clauses.back().push_back(x);
			problem.variables = std::max<unsigned int>(problem.variables, std::abs(x));
		}
		std::sort(problem.clauses.back().begin(), problem.clauses.back().end(), [](int a, int b) {
			return std::abs(a) < std::abs(b);
		});
	}
	return problem;
}

void renumber(vector<vector<int>>& clauses, const vector<unsigned int>& renumbering) {
	for (vector<int>& c : clauses) {
		for (unsigned int i = 0; i < c.size(); ++i) {
			int v = std::abs(c[i]);
			int r = renumbering[v];
			if (c[i] < 0)
				r = -r;
			c[i] = r;
		}
		std::sort(c.begin(), c.end(), [](int a, int b) {
			return std::abs(a) < std::abs(b);
		});
	}
}

using Heuristic = void(*)(vector<vector<int>>& clauses);
void nop_heuristic(vector<vector<int>>& clauses) {}

void renumber_popularity(vector<vector<int>>& clauses, bool least) {
	vector<unsigned int> occurrences;
	for (vector<int>& c : clauses)
		for (int v : c) {
			v = std::abs(v);
			if ((unsigned)v >= occurrences.size())
				occurrences.resize(v+1);
			++occurrences[v];
		}

	vector<std::pair<unsigned int, unsigned int>> sort;
	for (unsigned int i = 0; i < occurrences.size(); ++i)
		sort.push_back({occurrences[i], i});
	std::sort(sort.begin(), sort.end());
	if (!least)
		std::reverse(sort.begin(), sort.end());
	else
		std::rotate(sort.begin(), sort.begin()+1, sort.end());
	fmt::print("{}\n", sort);

	vector<unsigned int> renumbering;
	renumbering.resize(occurrences.size());
	for (unsigned int i = 0; i < sort.size(); ++i)
		renumbering[sort[i].second] = i+1;
	fmt::print("{}\n", renumbering);

	renumber(clauses, renumbering);
}
void popular(vector<vector<int>>& clauses) {
	renumber_popularity(clauses, false);
}
void antipopular(vector<vector<int>>& clauses) {
	renumber_popularity(clauses, true);
}
void random_variable(vector<vector<int>>& clauses) {
	vector<unsigned int> renumbering;
	for (vector<int>& c : clauses)
		for (int v : c) {
			v = std::abs(v);
			if ((unsigned)v >= renumbering.size())
				renumbering.resize(v+1);
		}
	std::iota(renumbering.begin()+1, renumbering.end(), 1);
	std::mt19937 rng(random_seed);
	std::shuffle(renumbering.begin()+1, renumbering.end(), rng);
	renumber(clauses, renumbering);
}

static const std::pair<std::string_view, Heuristic> variable_heuristics[] = {
	{"popular"sv, &popular},
	{"antipopular"sv, &antipopular},
	{"random"sv, &random_variable},
};

void sorted_clauses(vector<vector<int>>& clauses) {
	std::stable_sort(clauses.begin(), clauses.end(), [](const vector<int>& a, const vector<int>& b) {
		vector<int> aa = a, bb = b;
		for (int& c : aa)
			c = std::abs(c);
		for (int& c : bb)
			c = std::abs(c);
		return aa < bb;
	});
}
void reverse_sorted_clauses(vector<vector<int>>& clauses) {
	sorted_clauses(clauses);
	std::reverse(clauses.begin(), clauses.end());
}
void random_clause(vector<vector<int>>& clauses) {
	std::mt19937 rng(random_seed);
	std::shuffle(clauses.begin(), clauses.end(), rng);
}

static const std::pair<std::string_view, Heuristic> clause_heuristics[] = {
	{"sorted"sv, &sorted_clauses},
	{"reverse-sorted"sv, &reverse_sorted_clauses},
	{"random"sv, &random_clause},
};

unsigned long language_size_recurse(automaton::Automaton<2>& a, automaton::Automaton<2>::state_type state,
		tsl::hopscotch_map<automaton::Automaton<2>::state_type, unsigned long>& memo) {
	auto x = memo.find(state);
	if (x != memo.end())
		return x->second;

	unsigned long sum = 0;
	for (automaton::Automaton<2>::symbol_type symbol = 0; symbol < 2; ++symbol)
		if (auto next = a.stepDeterministic(state, symbol))
			sum += language_size_recurse(a, *next, memo);
	if (a.accept(state))
		sum += 1;
	memo[state] = sum;
	return sum;
}
unsigned long language_size(automaton::Automaton<2>& a) {
	tsl::hopscotch_map<automaton::Automaton<2>::state_type, unsigned long> memo;
	return language_size_recurse(a, 0, memo);
}

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True}
	setvbuf(stdout, nullptr, _IOLBF, 0); //line buffering

	Heuristic variable_heuristic = nop_heuristic, clause_heuristic = nop_heuristic;
	const char* dimacs_file = nullptr;
	for (int i = 1; i < argc; ++i)
		if (argv[i] == "--variable-order"sv) {
			variable_heuristic = nullptr;
			std::string_view name = argv[++i];
			for (const std::pair<std::string_view, Heuristic>& h : variable_heuristics)
				if (name == h.first)
					variable_heuristic = h.second;
			if (!variable_heuristic) {
				fmt::print(stderr, "unrecognized variable heuristic name: {}\n", name);
				std::exit(2);
			}
		} else if (argv[i] == "--clause-order"sv) {
			clause_heuristic = nullptr;
			std::string_view name = argv[++i];
			for (const std::pair<std::string_view, Heuristic>& h : clause_heuristics)
				if (name == h.first)
					clause_heuristic = h.second;
			if (!clause_heuristic) {
				fmt::print(stderr, "unrecognized clause heuristic name: {}\n", name);
				std::exit(2);
			}
		} else if (argv[i] == "--seed"sv)
			random_seed = to_uint64(argv[++i]);
		else
			dimacs_file = argv[i];

	Problem prob = parse_dimacs(dimacs_file);

	variable_heuristic(prob.clauses);
	clause_heuristic(prob.clauses);

	vector<automaton::Automaton<2>> automata;
	for (vector<int>& clause : prob.clauses) {
		vector<int> flat_clause = clause;
		for (int& v : flat_clause)
			v = std::abs(v) - 1;
		vector<int> differences;
		differences.resize(flat_clause.size());
		std::adjacent_difference(flat_clause.begin(), flat_clause.end(), differences.begin());
		//each difference beyond the first gets -1 to account for the position itself
		std::transform(differences.begin()+1, differences.end(),
				differences.begin()+1, [](int i){return i-1;});

		vector<R> components;
		for (unsigned int i = 0; i < differences.size(); ++i) {
			components.push_back(R::repeat(R::any(), differences[i]));
			//the value that doesn't satisfy the clause (we complement at the end)
			components.push_back(clause[i] > 0 ? R::lit(0) : R::lit(1));
		}
		components.push_back(R::repeat(R::any(), prob.variables - std::abs(clause.back()))); //TODO may be too long?
		R regex = R::conj({R::comp(R::cat(components)), R::repeat(R::any(), prob.variables)});
		automata.push_back(regex.compile());
		automata.back().minimize();
		std::cout << fmt::format("{}", clause) << " " << regex << " " << automata.back().state_size() << "\n";
	}

	automaton::Automaton<2> accumulator = std::move(automata.front());
	for (std::size_t i = 1; i < automata.size(); ++i) {
		accumulator = automaton::conj(accumulator, automata[i]);
		accumulator.minimize();
		auto free_memory = std::move(automata[i]);
	}
	accumulator.minimize();
	fmt::print("{} solutions\n", language_size(accumulator));
	return 0;
}