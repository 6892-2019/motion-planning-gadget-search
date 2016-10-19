#include "automaton.hpp"
#include <gtest/gtest.h>

using namespace automaton;
using namespace automaton::impl;

TEST(AutomatonTest, EmptyLanguage) {
	auto a = Automaton<2>::empty();
	EXPECT_TRUE(a->deterministic());
	EXPECT_FALSE(a->run({}));
	EXPECT_FALSE(a->run({0}));
	EXPECT_FALSE(a->run({1}));
	EXPECT_FALSE(a->run({0, 1}));
	EXPECT_FALSE(a->run({1, 0}));
}

TEST(AutomatonTest, AllLanguage) {
	auto a = Automaton<2>::all();
	EXPECT_TRUE(a->deterministic());
	EXPECT_TRUE(a->run({}));
	EXPECT_TRUE(a->run({0}));
	EXPECT_TRUE(a->run({1}));
	EXPECT_TRUE(a->run({0, 1}));
	EXPECT_TRUE(a->run({1, 0}));
}

TEST(AutomatonTest, Any) {
	auto a = Automaton<2>::any();
	EXPECT_TRUE(a->deterministic());
	EXPECT_FALSE(a->run({}));
	EXPECT_TRUE(a->run({0}));
	EXPECT_TRUE(a->run({1}));
	EXPECT_FALSE(a->run({0, 1}));
	EXPECT_FALSE(a->run({1, 0}));
}