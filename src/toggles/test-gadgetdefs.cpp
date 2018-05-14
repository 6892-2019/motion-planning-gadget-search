#include "precompiled.hpp"
#include "gadgetdefs.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN //genbuild entrypoint
#include <doctest.h>

#include "gadgetdefs.hpp"
#include "automaton.hpp"

using namespace automaton;
using std::unique_ptr;
using std::vector;
using std::pair;

namespace {
const char digits_group[] = "(\\d+)";
std::string replace_int_regex(std::string_view input, int replacement) {
	std::string ret(input);
	auto begin = ret.find(digits_group);
	ret.replace(begin, std::strlen(digits_group), std::to_string(replacement));
	return ret;
}
}

TEST_CASE("gadgetdefs_NoDuplicates") {
	auto names = known_gadget_keys();
	vector<pair<std::string, unique_ptr<WorkingAutomaton>>> instances;
	for (auto n : names) {
		std::string s = (n.find(digits_group) != decltype(n)::npos) ?
			replace_int_regex(n, 4) : std::string(n);
		auto a = known_gadget(s, 8);
		instances.emplace_back(std::move(s), std::move(a));
	}
	for (const auto& [iname, iauto] : instances)
		for (const auto& [jname, jauto] : instances)
			if (iname != jname) {
				INFO(iname << " " << jname);
				CHECK_NE(*iauto, *jauto);
			}
}