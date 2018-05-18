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
bool is_regex(const std::string_view input) {
	return input.find(digits_group) != std::string_view::npos;
}
std::string replace_int_regex(std::string_view input, int replacement) {
	std::string ret(input);
	auto begin = ret.find(digits_group);
	ret.replace(begin, std::strlen(digits_group), std::to_string(replacement));
	return ret;
}

void check_distinct(const vector<pair<std::string, unique_ptr<WorkingAutomaton>>>& instances) {
	for (const auto& [iname, iauto] : instances)
		for (const auto& [jname, jauto] : instances)
			if (iname != jname) {
				INFO(iname << " " << jname);
				CHECK_NE(*iauto, *jauto);
			}
}
}

TEST_CASE("gadgetdefs_NoDuplicates") {
	auto names = known_gadget_keys();
	vector<pair<std::string, unique_ptr<WorkingAutomaton>>> instances;
	for (auto n : names) {
		std::string s = is_regex(n) ? replace_int_regex(n, 4) : std::string(n);
		auto a = known_gadget(s, 8);
		instances.emplace_back(std::move(s), std::move(a));
	}
	check_distinct(instances);
}

TEST_CASE("gadgetdefs_unknown") {
	try {
		auto p = known_gadget("fgsfds", 42);
		FAIL("didn't throw");
	} catch (const unknown_gadget& e) {
		CHECK_EQ(e.gadget(), "fgsfds");
		CHECK_EQ(e.requested(), 42);
	}
}

TEST_CASE("gadgetdefs_toobig") {
	try {
		auto p = known_gadget("42-split", 4);
		FAIL("didn't throw");
	} catch (const bad_alphabet_size& e) {
		CHECK_EQ(e.gadget(), "42-split");
		CHECK_EQ(e.requested(), 4);
		CHECK_EQ(e.required(), 42);
	}
}

TEST_CASE("gadgetdefs_RegexDistinctness") {
	auto names = known_gadget_keys();
	vector<pair<std::string, unique_ptr<WorkingAutomaton>>> instances;
	for (const auto n : names) {
		if (!is_regex(n)) continue;
		instances.clear();
		for (unsigned int i = 1; i <= 16; ++i) {
			try {
				std::string s = replace_int_regex(n, i);
				auto a = known_gadget(s, 16);
				instances.emplace_back(std::move(s), std::move(a));
			} catch (const bad_alphabet_size&) {
				break;
			}
		}
		check_distinct(instances);
	}
}