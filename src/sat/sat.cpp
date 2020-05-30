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

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True}
	const char* dimacs_file = nullptr;
	for (int i = 1; i < argc; ++i)
//		if (argv[i] == "--heuristic"sv) {
//			heuristic = nullptr;
//			std::string_view name = argv[++i];
//			for (const std::pair<std::string_view, Heuristic>& h : heuristics)
//				if (name == h.first)
//					heuristic = h.second;
//			if (!heuristic) {
//				fmt::print(stderr, "unrecognized heuristic name: {}\n", name);
//				std::exit(2);
//			}
//		} else
		if (argv[i] == "--seed"sv)
			random_seed = to_uint64(argv[++i]);
		else
			dimacs_file = argv[i];

	Problem prob = parse_dimacs(dimacs_file);

	for (vector<int>& clause : prob.clauses)
		fmt::print("{}\n", clause);
	return 0;
}