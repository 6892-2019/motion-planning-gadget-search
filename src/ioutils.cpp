#include "ioutils.hpp"
#include "precompiled.hpp"

using std::vector;
using std::string;

std::vector<std::string> readAllLines(std::string filename) {
	std::vector<std::string> ret;
	std::ifstream file(filename);
	for (std::string line; std::getline(file, line);)
		ret.push_back(std::move(line));
	return ret;
}

void writeAllLines(std::string filename, const std::vector<std::string>& lines) {
	std::ofstream file(filename);
	for (const std::string& l : lines)
		file << l << "\n";
}

std::vector<std::string> processFilenameArgs(const char** first, const char** last) {
	vector<string> queue(first, last);
	std::reverse(queue.begin(), queue.end());
	vector<string> result;
	while (!queue.empty()) {
		string filename = std::move(queue.back());
		queue.pop_back();
		if (filename[0] == '@') { //response file
			filename.erase(0, 1);
			vector<string> lines = readAllLines(filename);
			std::reverse(lines.begin(), lines.end());
			//TODO: path resolution, somehow.
			queue.insert(queue.end(), std::move_iterator(lines.begin()), std::move_iterator(lines.end()));
		} else
			result.push_back(std::move(filename));
	}
	return result;
}