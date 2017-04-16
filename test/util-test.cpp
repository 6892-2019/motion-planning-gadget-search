#include "precompiled.hpp"
#include "automaton.hpp"
#include "util.hpp"
#include <gtest/gtest.h>

using namespace automaton;

TEST(AutomatonTest, AllStringsUtilFn) {
	auto s = allStrings(2, 0);
	EXPECT_EQ(s.size(), 1);
	EXPECT_TRUE(s[0].empty());

	s = allStrings(2, 1);
	EXPECT_EQ(s.size(), 3);
	auto sizeOne = [](const auto& v){return v.size() == 1;};
	EXPECT_EQ(std::count_if(s.begin(), s.end(), sizeOne), 2);

	s = allStrings(2, 2);
	EXPECT_EQ(s.size(), 7);
	EXPECT_EQ(std::count_if(s.begin(), s.end(), sizeOne), 2);
	auto sizeTwo = [](const auto& v){return v.size() == 2;};
	EXPECT_EQ(std::count_if(s.begin(), s.end(), sizeTwo), 4);

	s = allStrings(2, 3);
	EXPECT_EQ(s.size(), 15);
	auto sizeThree = [](const auto& v){return v.size() == 3;};
	EXPECT_EQ(std::count_if(s.begin(), s.end(), sizeThree), 8);
}

TEST(AutomatonTest, EquivOnAllStringsUtilFn) {
	equivalentOnAllStrings<2>(empty<2>(), empty<2>(), 8);
	equivalentOnAllStrings<2>(all<2>(), all<2>(), 8);
	equivalentOnAllStrings<2>(lit<2>(1), lit<2>(1), 8);
}