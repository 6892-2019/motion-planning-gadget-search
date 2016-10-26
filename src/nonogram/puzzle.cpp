#include "precompiled.hpp"
#include "puzzle.hpp"

//TODO: candidate for reuse
static std::vector<std::string> readAllLines(std::string filename) {
	std::vector<std::string> ret;
	std::ifstream file(filename);
	for (std::string line; std::getline(file, line);)
		ret.push_back(std::move(line));
	return ret;
}

/**
 * If the given string starts with the given prefix, erases that prefix.
 * @return true iff the prefix was present (and erased)
 */
template<unsigned int N>
static bool removePrefix(std::string& str, const char (&prefix)[N]) {
	//don't count the null terminator
	unsigned int end = prefix[N-1] ? N : N-1;
	if (str.size() < end) return false;
	for (unsigned int i = 0; i < end; ++i)
		if (str[i] != prefix[i])
			return false;
	str.erase(0, end);
	return true;
}

std::unique_ptr<Puzzle> Puzzle::fromNONFile(std::string filename) {
	std::vector<std::string> lines = readAllLines(filename);
	unsigned int width = 0, height = 0;
	std::string title, solution;
	std::vector<std::vector<Clue>> rows, cols;

	for (decltype(lines)::size_type i = 0; i < lines.size(); ++i) {
		std::string& line = lines[i];
		if (line.empty()) continue;
		if (removePrefix(line, "title "))
			title = std::move(line);
		else if (removePrefix(line, "height "))
			height = std::stoi(line);
		else if (removePrefix(line, "width "))
			width = std::stoi(line);
		else if (removePrefix(line, "goal ")) {
			//remove ""
			line.erase(0, 1);
			line.pop_back();
			solution = std::move(line);
		} else if (line == "rows" || line == "columns") {
			std::vector<std::vector<Clue>>& target = line == "rows" ? rows : cols;
			++i;
			std::vector<std::string> tokens;
			for (; !lines[i].empty(); ++i)
				if (lines[i] == "0")
					target.push_back({});
				else {
					tokens.clear();
					boost::algorithm::split(tokens, lines[i], boost::algorithm::is_any_of(","));
					std::vector<Clue> clues;
					for (const std::string& t : tokens)
						clues.push_back(Clue(std::stoi(t)));
					target.push_back(std::move(clues));
				}
		}
	}

	if (rows.size() != height || cols.size() != width)
		throw std::logic_error("error");

	//std::make_unique can't access private constructors, and there's no
	//exception-safety issue here.
	return std::unique_ptr<Puzzle>(new Puzzle(std::move(title), std::move(rows), std::move(cols), std::move(solution)));
}