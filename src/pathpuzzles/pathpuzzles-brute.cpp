#include "precompiled.hpp"
#include "pathpuzzle.hpp"
#include "ioutils.hpp"
#include "alphabet.hpp"
#include "regex.hpp"
#include "stringutils.hpp"
#include "tsl/ordered_set.h"

using std::unique_ptr;
using std::vector;
using std::pair;
using std::string;
using std::string_view;
using std::optional;
using std::make_optional;

using Coord = std::pair<unsigned int, unsigned int>;
struct CoordHash {
	std::size_t operator()(pair<unsigned int, unsigned int> p) const {
		//splitmix64, from near bottom of https://nullprogram.com/blog/2018/07/31/
		std::size_t x = p.first << 6 + p.second;
		x ^= x >> 30;
		x *= 0xbf58476d1ce4e5b9UL;
		x ^= x >> 27;
		x *= 0x94d049bb133111ebUL;
		x ^= x >> 31;
		return x;
	}
};
using PathSet = tsl::ordered_set<Coord, CoordHash>;

const auto le_zero = [](int x){return x <= 0;};

void solve_recurse(PathSet& path, vector<int>& rows, vector<int>& cols,
		const vector<Coord>& terminals, vector<vector<Coord>>& results) {
	if (std::find(terminals.begin(), terminals.end(), path.back()) != terminals.end() &&
			std::all_of(rows.begin(), rows.end(), le_zero) &&
			std::all_of(cols.begin(), cols.end(), le_zero))
		results.push_back(vector<Coord>(path.begin(), path.end()));

	Coord cur = path.back();
	std::array<Coord, 4> neighbors = {{
		{cur.first-1U, cur.second}, {cur.first+1U, cur.second},
		{cur.first, cur.second-1U}, {cur.first, cur.second+1U}
	}};
	for (Coord n : neighbors) {
		//This also handles wrapping after subtracting from zero.
		if (!(n.first < rows.size()) || !(n.second < cols.size()))
			continue;
		if (rows[n.first] == 0 || cols[n.second] == 0)
			continue;
		if (!path.insert(n).second)
			continue;
		--rows[n.first];
		--cols[n.second];
		solve_recurse(path, rows, cols, terminals, results);
		++cols[n.second];
		++rows[n.first];
		path.pop_back();
	}
}

vector<vector<Coord>> solve(const Puzzle& p) {
	vector<vector<Coord>> results;
	vector<int> rows, cols;
	for (optional<unsigned int> r : p.rows)
		if (r)
			rows.push_back((int)*r);
		else
			rows.push_back(-1);
	for (optional<unsigned int> r : p.cols)
		if (r)
			cols.push_back((int)*r);
		else
			cols.push_back(-1);
	PathSet path;
	for (Coord c : p.terminals) {
		path.clear();
		path.insert(c);
		--rows[c.first];
		--cols[c.second];
		solve_recurse(path, rows, cols, p.terminals, results);
		++cols[c.second];
		++rows[c.first];
	}
	return results;
}

int main(int argc, char* argv[]) { //genbuild {'entrypoint': True}
	Puzzle p = Puzzle::parse(argv[1]);
	vector<vector<Coord>> solutions = solve(p);
	for (vector<Coord>& s : solutions)
		fmt::print("{}\n", s);
}

//tsl::ordered_set to store the current path
//when subtracting one from row/col, just let it wrap, then check >= height/width

