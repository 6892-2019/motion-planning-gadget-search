#include "precompiled.hpp"
#include "puzzle.hpp"
#include "../regex.hpp"
#include "../alphabet.hpp"
#include "../automaton.hpp"

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

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True}
	std::unique_ptr<Puzzle> puzzle = Puzzle::fromNONFile(argv[1]);
	std::cout << puzzle->name() << std::endl;

	R zero = R::lit(false), one = R::lit(true), any = R::any();
	R rowWidth = R::repeat(any, static_cast<int>(puzzle->cols().size()));
	R sizeConstraint = R::repeat(any, static_cast<int>(puzzle->rows().size() * puzzle->cols().size()));

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

	std::stable_sort(puzzleConstraints.begin(), puzzleConstraints.end(), [](const Constraint& l, const Constraint& r) {
////		return std::tie(l.isCol, l.clues, l.solutions) < std::tie(r.isCol, r.clues, r.solutions);
//		std::size_t ls = std::numeric_limits<std::size_t>::max() - l.clues;
//		std::size_t rs = std::numeric_limits<std::size_t>::max() - r.clues;
//		return std::tie(l.isCol, ls, l.solutions) < std::tie(r.isCol, rs, r.solutions);
		return l.automaton.state_size() < r.automaton.state_size();
	});
//	std::mt19937 rng(3);
//	std::shuffle(puzzleConstraints.begin(), puzzleConstraints.end(), rng);

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