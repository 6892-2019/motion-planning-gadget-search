#include "precompiled.hpp"
#include "puzzle.hpp"
#include "../regex.hpp"
#include "../alphabet.hpp"

//TODO: support of multi-color puzzles will require more flexible alphabet selection
using R = automaton::Regex<BooleanAlphabet>;

/**
 * Counts solutions to a row.
 * @return the number of solutions to a row with the given clues and width
 */
static std::size_t countSolutions(const std::vector<Puzzle::Clue>& row, std::size_t width) {
	//TODO: empty rows?
	std::size_t spaces = width, bins = static_cast<unsigned int>(row.size() + 1);
	for (decltype(row.size()) i = 0; i < row.size(); ++i) {
		const Puzzle::Clue& clue = row[i];
		if (clue.length)
			spaces -= clue.length.get();
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
	return ways;
}

int main(int argc, char* argv[]) {
	std::unique_ptr<Puzzle> puzzle = Puzzle::fromNONFile(argv[1]);
	std::cout << puzzle->name() << std::endl;

	R zero = R::lit(false), one = R::lit(true), any = R::any();
	R zeroStar = R::star(zero), zeroPlus = R::plus(zero);
	R rowWidth = R::repeat(any, static_cast<int>(puzzle->cols().size()));
	R sizeConstraint = R::repeat(any, static_cast<int>(puzzle->rows().size() * puzzle->cols().size()));

	struct Constraint {
		R regex;
		std::size_t solutions;
		bool isCol;
	};
	std::vector<Constraint> puzzleConstraints;

	//Logically, the row constraints could all be concatenated together, but
	//somehow treating them separately is faster.
	for (decltype(puzzle->cols().size()) r = 0; r < puzzle->rows().size(); ++r) {
		const std::vector<Puzzle::Clue>& row = puzzle->rows()[r];
		R prefix = R::repeat(any, static_cast<int>(r * puzzle->cols().size())), suffix = R::repeat(any, static_cast<int>((puzzle->rows().size() - 1 - r) * puzzle->cols().size()));
		std::vector<R> clueConstraints;
		clueConstraints.reserve(2 * row.size() + 1);
		clueConstraints.push_back(zeroStar);
		for (decltype(row.size()) i = 0; i < row.size(); ++i) {
			const Puzzle::Clue& clue = row[i];
			if (clue.length)
				clueConstraints.push_back(R::repeat(R::lit(clue.color ? true : false), clue.length.get()));
			else
				clueConstraints.push_back(R::plus(R::lit(clue.color ? true : false)));
			if ((i+1) < row.size())
				if (clue.color == row[i+1].color)
					clueConstraints.push_back(zeroPlus);
				else
					clueConstraints.push_back(zeroStar);
		}
		clueConstraints.push_back(zeroStar);
		R thisRow = R::conj({rowWidth, R::cat(clueConstraints)});
		clueConstraints.clear();
		clueConstraints.push_back(prefix);
		clueConstraints.push_back(thisRow);
		clueConstraints.push_back(suffix);
		puzzleConstraints.push_back(Constraint{R::cat(clueConstraints),
				countSolutions(row, puzzle->cols().size()), false});
	}

	for (decltype(puzzle->cols().size()) c = 0; c < puzzle->cols().size(); ++c) {
		const std::vector<Puzzle::Clue>& col = puzzle->cols()[c];
		//prefix and suffix consume the parts of the row not in this column
		R prefix = R::repeat(any, static_cast<int>(c)), suffix = R::repeat(any, static_cast<int>(puzzle->cols().size() - 1 - c));
		R colZero = R::cat({prefix, zero, suffix}), colZeroStar = R::star(colZero), colZeroPlus = R::plus(colZero);
		std::vector<R> clueConstraints;
		clueConstraints.reserve(2 * col.size() + 1);
		clueConstraints.push_back(colZeroStar);
		for (decltype(col.size()) i = 0; i < col.size(); ++i) {
			const Puzzle::Clue& clue = col[i];
			if (clue.length)
				clueConstraints.push_back(R::repeat(R::cat({prefix, R::lit(clue.color ? true : false), suffix}), clue.length.get()));
			else
				clueConstraints.push_back(R::plus(R::cat({prefix, R::lit(clue.color ? true : false), suffix})));
			if ((i+1) < col.size())
				if (clue.color == col[i+1].color)
					clueConstraints.push_back(colZeroPlus);
				else
					clueConstraints.push_back(colZeroStar);
		}
		clueConstraints.push_back(colZeroStar);
		puzzleConstraints.push_back(Constraint{R::conj({R::cat(clueConstraints), sizeConstraint}),
				countSolutions(col, puzzle->rows().size()), true});
	}

	std::stable_sort(puzzleConstraints.begin(), puzzleConstraints.end(), [](const Constraint& l, const Constraint& r) {
		return std::tie(l.isCol, l.solutions) < std::tie(r.isCol, r.solutions);
	});

	std::vector<R> regexes;
	for (const Constraint& c : puzzleConstraints)
		regexes.push_back(c.regex);
	R puzzleConstraint = R::conj(regexes);
	std::vector<std::vector<bool>> solutions;
	try {
		puzzleConstraint.enumerate([&](auto& v) {solutions.push_back(v);});
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