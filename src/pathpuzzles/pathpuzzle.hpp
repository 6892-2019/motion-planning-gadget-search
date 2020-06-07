#ifndef PATHPUZZLE_HPP
#define PATHPUZZLE_HPP

#include <vector>
#include <optional>
#include <string>
#include <utility>
#include "ioutils.hpp"
#include "stringutils.hpp"

using std::unique_ptr;
using std::vector;
using std::pair;
using std::string;
using std::string_view;
using std::optional;
using std::make_optional;

struct Puzzle {
	static vector<optional<unsigned int>> parseRowCol(string rowcol) {
		vector<optional<unsigned int>> ret;
		vector<string_view> tokens = split_view(rowcol, ' ');
		for (string_view s : tokens)
			if (s == "-")
				ret.push_back(optional<unsigned int>(std::nullopt));
			else
				ret.push_back(optional<unsigned int>(to_uint(s)));
		return ret;
	}
	static Puzzle parse(string filename) {
		vector<string> lines = readAllLines(filename);
		vector<optional<unsigned int>> rs, cs;
		vector<pair<unsigned int, unsigned int>> ts;
		for (auto& line : lines) {
			if (removePrefix(line, "rows ")) {
				if (!rs.empty())
					throw std::runtime_error("rows repeated");
				rs = parseRowCol(line);
			} else if (removePrefix(line, "cols ")) {
				if (!cs.empty())
					throw std::runtime_error("cols repeated");
				cs = parseRowCol(line);
			} else if (removePrefix(line, "terminal ")) {
				vector<string_view> tokens = split_view(line, ' ');
				if (tokens.size() != 2)
					throw std::runtime_error("overlarge terminal: " + std::to_string(tokens.size()));
				ts.push_back({to_uint(tokens[0]), to_uint(tokens[1])});
			} else
				throw std::runtime_error(line);
		}
		return {rs, cs, ts};
	}
	vector<optional<unsigned int>> rows, cols;
	vector<pair<unsigned int, unsigned int>> terminals;
};

#endif /* PATHPUZZLE_HPP */

