#include "precompiled.hpp"
#include "regex.hpp"

using namespace automaton;

constexpr std::array<BooleanAlphabet::symbol_type, 2> BooleanAlphabet::symbols;

int main(int argc, char* argv[]) {
	Regex<BooleanAlphabet> t = Regex<BooleanAlphabet>::lit(true);
	auto f = Regex<BooleanAlphabet>::lit(false);
	std::vector<Regex<BooleanAlphabet>> v = {t, f};
//	auto c = Regex<BooleanAlphabet>::cat(v.begin(), v.end());
	auto c = Regex<BooleanAlphabet>::cat(v);
//	auto c = Regex<BooleanAlphabet>::cat({t, f});
	c.enumerate([](const std::vector<bool>& v) {std::cout << v[0] << v[1] << std::endl;});
	return 0;
}
