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

TEST_CASE("BitsetTest_DefaultConstruct64") {
	bitset<64> a;
	for (int i = 0; i < 64; ++i)
		CHECK_UNARY_FALSE(a[i]);
	CHECK_UNARY(a.none());
	CHECK_UNARY_FALSE(a.any());
	CHECK_UNARY_FALSE(a.all());
	CHECK_EQ(a.count(), 0);
}

TEST_CASE("BitsetTest_Equality64") {
	bitset<64> a, b;
	CHECK_EQ(a, b);
	for (int i = 0; i < 64; ++i) {
		a.reset();
		a.set(i);
		for (int j = 0; j < 64; ++j) {
			b.reset();
			b.set(j);
			CHECK_EQ(a == b, i == j);
			CHECK_EQ(a != b, i != j);
		}
	}
}

TEST_CASE("BitsetTest_SetResetOne64") {
	for (int i = 0; i < 64; ++i) {
		bitset<64> a;
		a.set(i);
		CHECK_UNARY(a[i]);
	}
	for (int i = 0; i < 64; ++i) {
		bitset<64> a;
		a.reset(i);
		CHECK_UNARY_FALSE(a[i]);
	}
}

TEST_CASE("BitsetTest_SetResetAll64") {
	bitset<64> a;
	for (int i = 0; i < 64; ++i)
		a.set(i);
	for (int i = 0; i < 64; ++i)
		CHECK_UNARY(a[i]);
	for (int i = 0; i < 64; ++i)
		a.reset(i);
	for (int i = 0; i < 64; ++i)
		CHECK_UNARY_FALSE(a[i]);
}

TEST_CASE("BitsetTest_FindFirst64") {
	for (int i = 0; i < 64; ++i) {
		bitset<64> a;
		a.set(i);
		CHECK_MESSAGE(a.find_first() == i, i);
	}
}

TEST_CASE("BitsetTest_FindNext64_00") {
	for (int i = 1; i < 64; ++i) {
		bitset<64> a;
		a.set(i);
		for (int j = 0; j < i; ++j)
			CHECK_EQ(a.find_next(j), i);
		for (int j = i; j < 64; ++j)
			CHECK_GE(a.find_next(j), a.size());
	}
}

TEST_CASE("BitsetTest_FindNext64_01") {
	for (int i = 1; i < 63; ++i) {
		bitset<64> a;
		a.set(i);
		a.set(i+1);
		CHECK_EQ(a.find_next(i), i+1);
	}
}