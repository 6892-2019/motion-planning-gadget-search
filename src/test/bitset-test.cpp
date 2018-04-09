#include "precompiled.hpp"
#include "bitset.hpp"
#include <doctest.h>

using automaton::bitset;

TEST_CASE("BitsetTest_ComplementSanity") {
	bitset<2> a, b;
	a.set(1);
	b.set(0);
	b = ~b;
	CHECK_EQ(a, b);
}

TEST_CASE("BitsetTest_FindFirst8Low") {
	bitset<8> a;
	a.set(0);
	CHECK_EQ(a.find_first(), 0);
}
TEST_CASE("BitsetTest_FindFirst8Mid") {
	bitset<8> a;
	a.set(3);
	CHECK_EQ(a.find_first(), 3);
}
TEST_CASE("BitsetTest_FindFirst8High") {
	bitset<8> a;
	a.set(7);
	CHECK_EQ(a.find_first(), 7);
}
TEST_CASE("BitsetTest_FindFirst8None") {
	bitset<8> a;
	CHECK_GT(a.find_first(), a.size());
}
TEST_CASE("BitsetTest_FindFirst12Low") {
	bitset<12> a;
	a.set(0);
	CHECK_EQ(a.find_first(), 0);
}
TEST_CASE("BitsetTest_FindFirst12Mid") {
	bitset<12> a;
	a.set(3);
	CHECK_EQ(a.find_first(), 3);
}
TEST_CASE("BitsetTest_FindFirst12High") {
	bitset<12> a;
	a.set(11);
	CHECK_EQ(a.find_first(), 11);
}
TEST_CASE("BitsetTest_FindFirst12None") {
	bitset<12> a;
	CHECK_GT(a.find_first(), a.size());
}
TEST_CASE("BitsetTest_FindFirst16Low") {
	bitset<16> a;
	a.set(0);
	CHECK_EQ(a.find_first(), 0);
}
TEST_CASE("BitsetTest_FindFirst16Mid") {
	bitset<16> a;
	a.set(3);
	CHECK_EQ(a.find_first(), 3);
}
TEST_CASE("BitsetTest_FindFirst16High") {
	bitset<16> a;
	a.set(15);
	CHECK_EQ(a.find_first(), 15);
}
TEST_CASE("BitsetTest_FindFirst16None") {
	bitset<16> a;
	CHECK_GT(a.find_first(), a.size());
}

TEST_CASE("BitsetTest_FindNext16") {
	bitset<16> a;
	a.set(3);
	a.set(4);
	CHECK_EQ(a.find_next(3), 4);
	CHECK_GT(a.find_next(4), a.size());
	a.set(15);
	CHECK_EQ(a.find_next(4), 15);
	CHECK_GT(a.find_next(15), a.size());
}