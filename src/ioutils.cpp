#include "ioutils.hpp"
#include "precompiled.hpp"

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

bool removePrefix(std::string& str, std::string_view prefix) {
	if (str.size() < prefix.size() ||
			!std::equal(str.begin(), str.begin()+prefix.size(), prefix.begin(), prefix.end()))
		return false;
	str.erase(0, prefix.size());
	return true;
}