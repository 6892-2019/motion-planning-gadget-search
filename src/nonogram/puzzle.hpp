/*
 * File:   puzzle.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 25, 2016, 8:14 PM
 */

#ifndef NONOGRAM_PUZZLE_HPP
#define NONOGRAM_PUZZLE_HPP

#include "algoutils.hpp"

class Puzzle {
public:
	struct Clue {
		std::optional<unsigned int> length;
		unsigned int color;
		explicit Clue(int length_, unsigned int color_ = 1) : length(maybe_opt(length_ != -1, length_)), color(color_) {}
	};

	static std::unique_ptr<Puzzle> fromNONFile(std::string filename);

	std::string name() const {return name_;}
	const std::vector<std::vector<Clue>>& rows() const {return rows_;}
	const std::vector<std::vector<Clue>>& cols() const {return cols_;}
	const std::string& solution() const {return solution_;}

	unsigned int colors() const {
		unsigned int c = 1;
		for (const auto& r : rows())
			for (const auto& clue : r)
				c = std::max(c, clue.color);
		for (const auto& r : cols())
			for (const auto& clue : r)
				c = std::max(c, clue.color);
		return c;
	}
private:
	Puzzle(std::string&& name, std::vector<std::vector<Clue>>&& rows,
			std::vector<std::vector<Clue>>&& cols, std::string&& solution)
			: name_(std::move(name)), rows_(std::move(rows)), cols_(std::move(cols)), solution_(std::move(solution)) {}
	std::string name_;
	std::vector<std::vector<Clue>> rows_, cols_;
	std::string solution_;
};

#endif /* NONOGRAM_PUZZLE_HPP */

