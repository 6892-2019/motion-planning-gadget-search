// SPDX-License-Identifier: MIT
// Copyright 2016 Massachusetts Institute of Technology
#include "precompiled.hpp"
#include "ioutils.hpp"
#include "alphabet.hpp"
#include "regex.hpp"
#include "stringutils.hpp"

using std::unique_ptr;
using std::vector;
using std::pair;
using std::string;
using std::string_view;
using std::optional;
using std::make_optional;

using Coord = std::pair<unsigned int, unsigned int>;

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
		vector<Coord> ts;
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
	vector<Coord> terminals;
};

using CoordSet = tsl::hopscotch_set<Coord, boost::hash<const Coord>>;
void findPathsRecurse(const CoordSet& vertices, Coord target, vector<Coord>& path,
		CoordSet& pathSet, vector<vector<Coord>>& results) {
	if (path.back() == target) {
		if (path.size() == vertices.size())
			results.push_back(path);
		//solution or not, we're done with this recursion branch
		return;
	}
	Coord cur = path.back();
	std::array<Coord, 4> neighbors = {{
		{cur.first-1U, cur.second}, {cur.first+1U, cur.second},
		{cur.first, cur.second-1U}, {cur.first, cur.second+1U}
	}};
	for (Coord n : neighbors) {
		if (vertices.count(n) && pathSet.insert(n).second) {
			path.push_back(n);
			findPathsRecurse(vertices, target, path, pathSet, results);
			assert(path.back() == n);
			path.pop_back();
			MAYBE_UNUSED bool erased = pathSet.erase(n);
			assert(erased);
		}
	}
}

vector<vector<Coord>> findPaths(const CoordSet& vertices, Coord source, Coord target) {
	vector<vector<Coord>> results;
	vector<Coord> path;
	path.reserve(vertices.size());
	path.push_back(source);
	CoordSet pathSet;
	pathSet.reserve(vertices.size());
	pathSet.insert(source);
	findPathsRecurse(vertices, target, path, pathSet, results);
	return results;
}

using R = automaton::Regex<ByteAlphabet<3>>;

/**
 * @return a regex that constrains the given positions to match the given regex,
 * and imposes no constraints on any other
 */
R require(R regex, std::initializer_list<unsigned int> positions) {
	//TODO: What if the given regex can match more than one character?  We don't
	//want to do an unnecessary intersection here... maybe add a "can match
	//length n" query to Regex and assert it here?
	//TODO: could be generalized to a position -> regex mapping
	vector<unsigned int> sp(positions);
	assert(!sp.empty());
	std::sort(sp.begin(), sp.end());
	assert(std::adjacent_find(sp.begin(), sp.end()) == sp.end());
	vector<unsigned int> differences;
	differences.resize(sp.size());
	std::adjacent_difference(sp.begin(), sp.end(), differences.begin());
	//each difference beyond the first gets -1 to account for the position itself
	std::transform(differences.begin()+1, differences.end(),
		differences.begin()+1, [](unsigned int i){return i-1;});

	vector<R> constraint;
	constraint.reserve(2*positions.size() + 1);
	for (unsigned int offset : differences) {
		constraint.push_back(R::repeat(R::any(), offset));
		constraint.push_back(regex);
	}
	constraint.push_back(R::star(R::any()));
	return R::cat(constraint);
}

/**
 * @returns a regex that constrains the given regex to match the given number of
 * times within the given total number of occurrences
 */
R subset(R regex, unsigned int required, unsigned int total, unsigned int stride = 1) {
	//There's a straightforward automata representation for this that we might
	//prefer to have as a primitive.
	vector<R> constraint;
	constraint.reserve(2*required + 1);
	R anyStar = R::star(R::repeat(R::any(), stride));
	for (unsigned int i = 0; i < required; ++i) {
		constraint.push_back(anyStar);
		constraint.push_back(regex);
	}
	constraint.push_back(anyStar);
	return R::conj({R::cat(constraint), R::repeat(R::any(), total * stride)});
}

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True}
	Puzzle p = Puzzle::parse(argv[1]);
	unsigned int width = static_cast<unsigned int>(p.cols.size()), height = static_cast<unsigned int>(p.rows.size());

	vector<R> constraints;

	vector<R> sizeConstraint;
	R normalCell = R::alt({R::lit(0), R::lit(1)});
	for (unsigned int r = 0; r < height; ++r)
		for (unsigned int c = 0; c < width; ++c)
			if (std::find(p.terminals.begin(), p.terminals.end(), std::make_pair(r, c)) != p.terminals.end())
				sizeConstraint.push_back(R::any());
			else
				sizeConstraint.push_back(normalCell);
	constraints.push_back(R::cat(sizeConstraint));

	vector<R> terminalConstraints;
	for (std::size_t i = 0; i < p.terminals.size(); ++i)
		for (std::size_t j = i+1; j < p.terminals.size(); ++j) {
			unsigned int pi = p.terminals[i].first * width + p.terminals[i].second;
			unsigned int pj = p.terminals[j].first * width + p.terminals[j].second;
			terminalConstraints.push_back(require(R::lit(2), {pi, pj}));
		}
	constraints.push_back(R::alt(terminalConstraints));

	R present = R::alt({R::lit(1), R::lit(2)});
	for (unsigned int row = 0; row < p.rows.size(); ++row) {
		if (p.rows[row])
			constraints.push_back(R::cat({
				R::repeat(R::any(), row * width),
				R::conj({
					subset(present, *(p.rows[row]), width),
					subset(R::lit(0), width - *(p.rows[row]), width)
				}),
				R::star(R::any()),
			}));
	}
	for (unsigned int col = 0; col < p.cols.size(); ++col) {
		if (p.cols[col]) {
			R colSelect = R::cat({R::repeat(R::any(), col), present, R::repeat(R::any(), width - col - 1)});
			R colUnselect = R::cat({R::repeat(R::any(), col), R::lit(0), R::repeat(R::any(), width - col - 1)});
			R colConstraint = R::conj({
				subset(colSelect, *(p.cols[col]), height, width),
				subset(colUnselect, height - *(p.cols[col]), height, width),
			});
			constraints.push_back(colConstraint);
		}
	}

	if (width > 2) {
		//A present cell (1 or 2) cannot be surrounded by 0s on all sides.
		//TODO: border should count as a 0 for 1s (not for 2s)
		R nonisolation = R::comp(R::cat({
			R::star(R::any()),
			R::lit(0),
			R::repeat(R::any(), width - 2),
			R::lit(0),
			R::lit(1),
			R::lit(0),
			R::repeat(R::any(), width - 2),
			R::lit(0),
			R::star(R::any())
		}));
		constraints.push_back(nonisolation);
	}

	R overall = R::conj(constraints);
	std::size_t count = 0;
	overall.enumerate([&](const vector<uint8_t>& vec) {
		CoordSet vertices;
		vector<Coord> terminals;
		for (unsigned int r = 0; r < height; ++r) {
			for (unsigned int c = 0; c < width; ++c) {
				if (vec[r * width + c] != 0)
					vertices.insert({r, c});
				if (vec[r * width + c] == 2)
					terminals.push_back({r, c});
			}
		}

		vector<vector<Coord>> paths;
		for (unsigned int i = 0; i < terminals.size(); ++i)
			for (unsigned int j = i+1; j < terminals.size(); ++j) {
				const auto& these = findPaths(vertices, terminals[i], terminals[j]);
				paths.insert(paths.end(), these.begin(), these.end());
			}

		if (!paths.empty()) {
			++count;
			for (unsigned int r = 0; r < height; ++r) {
				for (unsigned int c = 0; c < width; ++c)
					std::cout << static_cast<unsigned int>(vec[r * width + c]);
				std::cout << '\n';
			}
			for (const auto& path : paths) {
				for (Coord c : path)
					std::cout << '(' << c.first << "," << c.second << "), ";
				std::cout << '\n';
			}
			std::cout << '\n';
		}
	});
}