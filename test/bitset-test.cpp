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

TEST(BitsetTest, FindFirst8Low) {
	bitset<8> a;
	a.set(0);
	EXPECT_EQ(a.find_first(), 0);
}
TEST(BitsetTest, FindFirst8Mid) {
	bitset<8> a;
	a.set(3);
	EXPECT_EQ(a.find_first(), 3);
}
TEST(BitsetTest, FindFirst8High) {
	bitset<8> a;
	a.set(7);
	EXPECT_EQ(a.find_first(), 7);
}
TEST(BitsetTest, FindFirst8None) {
	bitset<8> a;
	EXPECT_GT(a.find_first(), a.size());
}
TEST(BitsetTest, FindFirst12Low) {
	bitset<12> a;
	a.set(0);
	EXPECT_EQ(a.find_first(), 0);
}
TEST(BitsetTest, FindFirst12Mid) {
	bitset<12> a;
	a.set(3);
	EXPECT_EQ(a.find_first(), 3);
}
TEST(BitsetTest, FindFirst12High) {
	bitset<12> a;
	a.set(11);
	EXPECT_EQ(a.find_first(), 11);
}
TEST(BitsetTest, FindFirst12None) {
	bitset<12> a;
	EXPECT_GT(a.find_first(), a.size());
}
TEST(BitsetTest, FindFirst16Low) {
	bitset<16> a;
	a.set(0);
	EXPECT_EQ(a.find_first(), 0);
}
TEST(BitsetTest, FindFirst16Mid) {
	bitset<16> a;
	a.set(3);
	EXPECT_EQ(a.find_first(), 3);
}
TEST(BitsetTest, FindFirst16High) {
	bitset<16> a;
	a.set(15);
	EXPECT_EQ(a.find_first(), 15);
}
TEST(BitsetTest, FindFirst16None) {
	bitset<16> a;
	EXPECT_GT(a.find_first(), a.size());
}

TEST(BitsetTest, FindNext16) {
	bitset<16> a;
	a.set(3);
	a.set(4);
	EXPECT_EQ(a.find_next(3), 4);
	EXPECT_GT(a.find_next(4), a.size());
	a.set(15);
	EXPECT_EQ(a.find_next(4), 15);
	EXPECT_GT(a.find_next(15), a.size());
}