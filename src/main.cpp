#include "precompiled.hpp"
#include "regex.hpp"

using namespace automaton;

int main(int argc, char* argv[]) {
	Regex<BooleanAlphabet> t = Regex<BooleanAlphabet>::lit(true);
	auto f = Regex<BooleanAlphabet>::lit(true);
	std::vector<Regex<BooleanAlphabet>> v = {t, f};
//	auto c = Regex<BooleanAlphabet>::cat(v.begin(), v.end());
	auto c = Regex<BooleanAlphabet>::cat(v);
//	auto c = Regex<BooleanAlphabet>::cat({t, f});
	return 0;
}
