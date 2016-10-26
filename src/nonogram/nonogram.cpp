#include "precompiled.hpp"
#include "puzzle.hpp"
#include "../regex.hpp"
#include "../alphabet.hpp"

int main(int argc, char* argv[]) {
	std::unique_ptr<Puzzle> puzzle = Puzzle::fromNONFile(argv[1]);
	std::cout << puzzle->name() << std::endl;

	using R = automaton::Regex<BooleanAlphabet>;
	R zero = R::lit(false), one = R::lit(true), any = R::any();
	R zeroStar = R::star(zero), zeroPlus = R::plus(zero);
	R rowWidth = R::repeat(any, static_cast<int>(puzzle->cols().size()));

	std::vector<R> puzzleConstraints;
	puzzleConstraints.push_back(R::repeat(any, static_cast<int>(puzzle->rows().size() * puzzle->cols().size())));

	//row constraints are concatenated (row-major order)
	std::vector<R> rowConstraints;
	for (const std::vector<Puzzle::Clue>& row : puzzle->rows()) {
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
		rowConstraints.push_back(R::conj({rowWidth, R::cat(clueConstraints)}));
	}
	puzzleConstraints.push_back(R::cat(rowConstraints));

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
		puzzleConstraints.push_back(R::cat(clueConstraints));
	}

	int solutions = 0;
	R puzzleConstraint = R::cat(puzzleConstraints);
	puzzleConstraint.enumerate([&](auto& v) {++solutions;});
	std::cout << solutions << std::endl;
	return 0;
}