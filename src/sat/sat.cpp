#include "precompiled.hpp"
#include "ioutils.hpp"
#include "stringutils.hpp"
#include <boost/algorithm/string/trim.hpp>

using std::vector;
using namespace std::literals::string_view_literals;

static std::size_t random_seed;

struct Problem {
	vector<vector<int>> clauses; //each clause sorted by absolute value
	unsigned int variables; //numbered 1 to n because negative zero isn't a thing
};

Problem parse_dimacs(const char* filename) {
	vector<std::string> lines = readAllLines(filename);
	Problem problem;
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

using Heuristic = void(*)(vector<vector<int>>& clauses);

void nop_heuristic(vector<vector<int>>& clauses) {}

void renumber_popularity(vector<vector<int>>& clauses, bool least) {
	vector<unsigned int> occurrences;
	for (vector<int>& c : clauses)
		for (int v : c) {
			v = std::abs(v);
			if ((unsigned)v > occurrences.size())
				occurrences.resize(v+1);
			++occurrences[v];
		}

	vector<std::pair<unsigned int, unsigned int>> sort;
	for (unsigned int i = 0; i < occurrences.size(); ++i)
		sort.push_back({occurrences[i], i});
	std::sort(sort.begin(), sort.end());
	if (!least)
		std::reverse(sort.begin(), sort.end());
	fmt::print("{}\n", sort);

	vector<unsigned int> renumbering;
	renumbering.resize(occurrences.size());
	for (unsigned int i = 0; i < sort.size(); ++i)
		renumbering[sort[i].second] = i+1;
	fmt::print("{}\n", renumbering);

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
void popular(vector<vector<int>>& clauses) {
	renumber_popularity(clauses, false);
}
void antipopular(vector<vector<int>>& clauses) {
	renumber_popularity(clauses, true);
}

static const std::pair<std::string_view, Heuristic> variable_heuristics[] = {
	{"popular"sv, &popular},
	{"antipopular"sv, &antipopular},
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

static const std::pair<std::string_view, Heuristic> clause_heuristics[] = {
	{"sorted"sv, &sorted_clauses},
	{"reverse-sorted"sv, &reverse_sorted_clauses},
};

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True}
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

	for (vector<int>& clause : prob.clauses)
		fmt::print("{}\n", clause);
	return 0;
}