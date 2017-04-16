#include "precompiled.hpp"
#include "util.hpp"

std::string to_string(const std::vector<unsigned int>& v) {
	std::string str;
    str += '(';
    for (auto i = v.cbegin(); i != v.cend(); ++i)
        str += std::to_string(*i);
    str += ')';
    return str;
}

std::vector<std::vector<unsigned int>> allStrings(unsigned int alphabetSize, unsigned int length) {
	assert(alphabetSize > 0);
	std::vector<std::vector<unsigned int>> ret;

	//Starting from the empty string, take all strings from the last generation
	//and append each possible symbol to them.
	auto start = ret.size();
	ret.emplace_back();
	for (unsigned int n = 1; n <= length; ++n) {
		auto end = ret.size();
		for (auto i = start; i < end; ++i) {
			for (unsigned int s = 0; s < alphabetSize; ++s) {
				//We can't just ret.push_back(ret[i]) because reallocation will
				//move ret[i] out from under us.
				auto copy = ret[i];
				copy.push_back(s);
				ret.push_back(std::move(copy));
			}
		}
		start = end;
	}
	return ret;
}