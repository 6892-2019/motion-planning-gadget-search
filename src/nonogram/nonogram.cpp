#include "precompiled.hpp"
#include "puzzle.hpp"
#include "../regex.hpp"
#include "../alphabet.hpp"
#include "../automaton.hpp"
#include "stringutils.hpp"

using namespace std::literals::string_view_literals;

//TODO: support of multi-color puzzles will require more flexible alphabet selection
using R = automaton::Regex<BooleanAlphabet>;

/**
 * Counts solutions to a row.  As part of that computation, also computes the
 * number of "floating 0s" that can appear between any of the clues in that row.
 * If there are variable-length clues in the row, the returned number of
 * floating 0s is a maximum (some may instead be part of the variable-length
 * clues).
 * @return the number of solutions to a row with the given clues and width, and
 * the maximum number of floating 0s in the row
 */
static std::pair<std::size_t, std::size_t> countSolutions(const std::vector<Puzzle::Clue>& row, std::size_t width) {
	//TODO: empty rows?
	std::size_t spaces = width, bins = static_cast<unsigned int>(row.size() + 1);
	for (decltype(row.size()) i = 0; i < row.size(); ++i) {
		const Puzzle::Clue& clue = row[i];
		if (clue.length)
			spaces -= *clue.length;
		else {
			//must consume at least one space
			--spaces;
			//plus some number of additional spaces
			++bins;
		}
		if ((i+1) < row.size() && clue.color == row[i+1].color)
			//at least one space spent in the bin separating these clues
			--spaces;
	}
	//compute the ways to distribute spaces into bins (including allowing 0 in a bin)
	//(n + k - 1) choose (k - 1)
	//TODO: smarter overflow-conscious code, possibly involving factorization
	std::size_t numerator = 1, denominator = 1;
	for (std::size_t x = spaces + bins - 1; x >= spaces + 1; --x)
		numerator *= x;
	for (std::size_t x = bins - 1; x > 0; --x)
		denominator *= x;
	std::size_t ways = numerator/denominator;
	return {ways, spaces};
}

struct Constraint {
	Constraint(R r, std::size_t clue, std::size_t solution, std::size_t floatingZero, int rown, int coln)
			: regex(r), automaton(r.compile()), clues(clue), solutions(solution), floatingZeroes(floatingZero), row(rown), col(coln) {
		automaton.minimize();
	}
	R regex;
	automaton::Automaton<2> automaton;
	std::size_t clues;
	std::size_t solutions;
	std::size_t floatingZeroes;
	int row, col; //one of these is -1
};

using Heuristic = void(*)(std::vector<Constraint>&);

void rows_first(std::vector<Constraint>& constraints) {}
void cols_first(std::vector<Constraint>& constraints) {
	std::stable_sort(constraints.begin(), constraints.end(), [](const Constraint& l, const Constraint& r) {
		//i.e., sort rows to the back, cols in their existing order
		auto ls = l.col == -1 ? std::numeric_limits<int>::max() : l.col;
		auto rs = r.col == -1 ? std::numeric_limits<int>::max() : r.col;
		return ls < rs;
	});
}
void interleaved(std::vector<Constraint>& constraints) {
	std::vector<Constraint> rows, cols;
	for (Constraint& c : constraints)
		if (c.row != -1)
			rows.push_back(std::move(c));
		else
			cols.push_back(std::move(c));
	std::reverse(rows.begin(), rows.end());
	std::reverse(cols.begin(), cols.end());
	constraints.clear();
	while (!rows.empty() && !cols.empty()) {
		constraints.push_back(std::move(rows.back()));
		constraints.push_back(std::move(cols.back()));
		rows.pop_back();
		cols.pop_back();
	}
	while (!rows.empty()) {
		constraints.push_back(std::move(rows.back()));
		rows.pop_back();
	}
	while (!cols.empty()) {
		constraints.push_back(std::move(cols.back()));
		cols.pop_back();
	}
}

void fewest_solutions(std::vector<Constraint>& constraints) {
	std::stable_sort(constraints.begin(), constraints.end(), [](const Constraint& l, const Constraint& r) {
		return l.solutions < r.solutions;
	});
}
void most_solutions(std::vector<Constraint>& constraints) {
	std::stable_sort(constraints.begin(), constraints.end(), [](const Constraint& l, const Constraint& r) {
		return l.solutions > r.solutions;
	});
}

void fewest_states(std::vector<Constraint>& constraints) {
	std::stable_sort(constraints.begin(), constraints.end(), [](const Constraint& l, const Constraint& r) {
		return l.automaton.state_size() < r.automaton.state_size();
	});
}
void most_states(std::vector<Constraint>& constraints) {
	std::stable_sort(constraints.begin(), constraints.end(), [](const Constraint& l, const Constraint& r) {
		return l.automaton.state_size() > r.automaton.state_size();
	});
}

static std::size_t random_seed = 0;
void random_heuristic(std::vector<Constraint>& constraints) {
	std::mt19937 rng(random_seed);
	std::shuffle(constraints.begin(), constraints.end(), rng);
}
void random_cols_heuristic(std::vector<Constraint>& constraints) {
	std::mt19937 rng(random_seed);
	auto col_begin = std::partition_point(constraints.begin(), constraints.end(), [](const Constraint& c){return c.row != -1;});
	std::shuffle(col_begin, constraints.end(), rng);
}

static const std::pair<std::string_view, Heuristic> heuristics[] = {
	{"rows"sv, &rows_first},
	{"cols"sv, &cols_first},
	{"interleave"sv, &interleaved},
	{"fewest-solutions"sv, &fewest_solutions},
	{"most-solutions"sv, &most_solutions},
	{"fewest-states"sv, &fewest_states},
	{"most-states"sv, &most_states},
	{"random"sv, &random_heuristic},
	{"random-cols"sv, &random_cols_heuristic},
};

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True}
	setvbuf(stdout, nullptr, _IOLBF, 0); //line buffering

	char* puzzle_file = nullptr;
	Heuristic heuristic = &rows_first;
	for (int i = 1; i < argc; ++i)
		if (argv[i] == "--heuristic"sv) {
			heuristic = nullptr;
			std::string_view name = argv[++i];
			for (const std::pair<std::string_view, Heuristic>& h : heuristics)
				if (name == h.first)
					heuristic = h.second;
			if (!heuristic) {
				fmt::print(stderr, "unrecognized heuristic name: {}\n", name);
				std::exit(2);
			}
		} else if (argv[i] == "--seed"sv)
			random_seed = to_uint64(argv[++i]);
		else
			puzzle_file = argv[i];

	std::unique_ptr<Puzzle> puzzle = Puzzle::fromNONFile(puzzle_file);
	std::cout << puzzle->name() << std::endl;

	R zero = R::lit(false), one = R::lit(true), any = R::any();
	R rowWidth = R::repeat(any, static_cast<int>(puzzle->cols().size()));
	R sizeConstraint = R::repeat(any, static_cast<int>(puzzle->rows().size() * puzzle->cols().size()));
	std::vector<Constraint> puzzleConstraints;

	//Logically, the row constraints could all be concatenated together, but
	//somehow treating them separately is faster.
	for (decltype(puzzle->cols().size()) r = 0; r < puzzle->rows().size(); ++r) {
		const std::vector<Puzzle::Clue>& row = puzzle->rows()[r];
		if (row.empty()) continue;
		std::size_t solutions, floatingZeroes;
		std::tie(solutions, floatingZeroes) = countSolutions(row, puzzle->cols().size());
		R prefix = R::repeat(any, static_cast<int>(r * puzzle->cols().size())), suffix = R::repeat(any, static_cast<int>((puzzle->rows().size() - 1 - r) * puzzle->cols().size()));
		R paddingZero = R::range(R::lit(0), 0, static_cast<int>(floatingZeroes));
		R separatorZero = R::range(R::lit(0), 1, static_cast<int>(floatingZeroes + 1));

		std::vector<R> clueConstraints;
		clueConstraints.reserve(2 * row.size() + 1);
		clueConstraints.push_back(paddingZero);
		for (decltype(row.size()) i = 0; i < row.size(); ++i) {
			const Puzzle::Clue& clue = row[i];
			if (clue.length)
				clueConstraints.push_back(R::repeat(R::lit(clue.color ? true : false), *clue.length));
			else
				clueConstraints.push_back(R::plus(R::lit(clue.color ? true : false)));
			if ((i+1) < row.size())
				if (clue.color == row[i+1].color)
					clueConstraints.push_back(separatorZero);
				else
					clueConstraints.push_back(paddingZero);
		}
		clueConstraints.push_back(paddingZero);
		R thisRow = R::conj({rowWidth, R::cat(clueConstraints)});
		clueConstraints.clear();
		clueConstraints.push_back(prefix);
		clueConstraints.push_back(thisRow);
		clueConstraints.push_back(suffix);
		puzzleConstraints.push_back(Constraint{R::cat(clueConstraints),
				row.size(), solutions, floatingZeroes, r, -1});
	}

	for (decltype(puzzle->cols().size()) c = 0; c < puzzle->cols().size(); ++c) {
		const std::vector<Puzzle::Clue>& col = puzzle->cols()[c];
		if (col.empty()) continue;
		std::size_t solutions, floatingZeroes;
		std::tie(solutions, floatingZeroes) = countSolutions(col, puzzle->rows().size());
		//prefix and suffix consume the parts of the row not in this column
		R prefix = R::repeat(any, static_cast<int>(c)), suffix = R::repeat(any, static_cast<int>(puzzle->cols().size() - 1 - c));
		R colZero = R::cat({prefix, zero, suffix});
		R paddingZero = R::range(colZero, 0, static_cast<int>(floatingZeroes));
		R separatorZero = R::range(colZero, 1, static_cast<int>(floatingZeroes + 1));
		std::vector<R> clueConstraints;
		clueConstraints.reserve(2 * col.size() + 1);
		clueConstraints.push_back(paddingZero);
		for (decltype(col.size()) i = 0; i < col.size(); ++i) {
			const Puzzle::Clue& clue = col[i];
			if (clue.length)
				clueConstraints.push_back(R::repeat(R::cat({prefix, R::lit(clue.color ? true : false), suffix}), *clue.length));
			else
				clueConstraints.push_back(R::plus(R::cat({prefix, R::lit(clue.color ? true : false), suffix})));
			if ((i+1) < col.size())
				if (clue.color == col[i+1].color)
					clueConstraints.push_back(separatorZero);
				else
					clueConstraints.push_back(paddingZero);
		}
		clueConstraints.push_back(paddingZero);
		puzzleConstraints.push_back(Constraint{R::conj({R::cat(clueConstraints), sizeConstraint}),
				col.size(), solutions, floatingZeroes, -1, c});
	}

	std::cout << reinterpret_cast<void*>(heuristic) << std::endl;
	heuristic(puzzleConstraints);

	for (const Constraint& c : puzzleConstraints)
		std::cout << c.clues << " " << c.floatingZeroes << " " << c.solutions << " " << c.regex << std::endl;

	automaton::Automaton<2> accumulator = std::move(puzzleConstraints.front().automaton);
	for (std::size_t i = 1; i < puzzleConstraints.size(); ++i) {
		accumulator = automaton::conj(accumulator, puzzleConstraints[i].automaton);
		accumulator.minimize();
		auto free_memory = std::move(puzzleConstraints[i].automaton);
	}

	std::vector<std::vector<bool>> solutions;
	try {
		accumulator.template enumerate<BooleanAlphabet>([&](auto& v) {solutions.push_back(v);});
	} catch (std::bad_alloc& ex) {
		std::cout << ex.what() << std::endl;
		return 1;
	}
	std::cout << solutions.size() << " solutions" << std::endl;
	for (auto& v : solutions) {
		for (auto s : v)
			std::cout << s;
		std::cout << '\n';
	}
	std::cout << std::endl;
	std::cout << puzzle->solution() << std::endl;
	return 0;
}