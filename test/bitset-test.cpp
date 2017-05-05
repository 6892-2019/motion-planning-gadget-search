#include "precompiled.hpp"
#include "bitset.hpp"
#include <gtest/gtest.h>

using automaton::bitset;

TEST(BitsetTest, ComplementSanity) {
	bitset<2> a, b;
	a.set(1);
	b.set(0);
	b = ~b;
	EXPECT_EQ(a, b);
}