#include "precompiled.hpp"
#include "circular_deque.hpp"
#include <gtest/gtest.h>

using std::swap;

TEST(CircularDequeTest, CtorDtor) {
	circular_deque<unsigned int, 16> deque;
	EXPECT_TRUE(deque.empty());
	EXPECT_EQ(deque.size(), 0);
	EXPECT_EQ(deque.capacity(), 16);
}

TEST(CircularDequeTest, DtorNonemptySmall) {
	circular_deque<unsigned int, 16> deque;
	deque.push_back(0);
}

TEST(CircularDequeTest, DtorNonemptyNonsmall) {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 32; ++i)
		deque.push_back(i);
}

TEST(CircularDequeTest, PushPopSmall) {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 16; ++i)
		deque.push_back(i);
	EXPECT_FALSE(deque.empty());
	EXPECT_EQ(deque.size(), 16);
	EXPECT_EQ(deque.capacity(), 16);
	for (int i = 15; i >= 0; --i)
		EXPECT_EQ(deque.pop_back(), i);
	EXPECT_TRUE(deque.empty());
	EXPECT_EQ(deque.size(), 0);
	EXPECT_EQ(deque.capacity(), 16);
}

TEST(CircularDequeTest, PushPopLarge) {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 64; ++i)
		deque.push_back(i);
	EXPECT_FALSE(deque.empty());
	EXPECT_EQ(deque.size(), 64);
	EXPECT_GE(deque.capacity(), deque.size());
	for (int i = 63; i >= 0; --i)
		EXPECT_EQ(deque.pop_back(), i);
	EXPECT_TRUE(deque.empty());
	EXPECT_EQ(deque.size(), 0);
}

TEST(CircularDequeTest, CycleStayingSmall) {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 8; ++i)
		deque.push_back(i);
	for (int cycle = 0; cycle < 1000; ++cycle) {
		deque.push_back(cycle);
		deque.pop_front();
	}
}

TEST(CircularDequeTest, CycleNonsmall) {
	circular_deque<unsigned int, 16> deque;
	for (int cycle = 0; cycle < 1000; ++cycle) {
		deque.push_back(cycle);
		deque.push_back(cycle);
		deque.pop_front();
	}
}

TEST(CircularDequeTest, EmplacedInReservedVector) {
	std::vector<circular_deque<unsigned int, 16>> deques;
	deques.reserve(50);
	for (int i = 0; i < 50; ++i)
		deques.emplace_back();
}

TEST(CircularDequeTest, EmplacedInVector) {
	std::vector<circular_deque<unsigned int, 16>> deques;
	for (int i = 0; i < 50; ++i)
		deques.emplace_back();
}

TEST(CircularDequeTest, PushedInReservedVector) {
	std::vector<circular_deque<unsigned int, 16>> deques;
	deques.reserve(50);
	for (int i = 0; i < 50; ++i)
		deques.push_back({});
}

TEST(CircularDequeTest, PushedInVector) {
	std::vector<circular_deque<unsigned int, 16>> deques;
	for (int i = 0; i < 50; ++i)
		deques.push_back({});
}

TEST(CircularDequeTest, PushedInVectorSomeInflated) {
	std::vector<circular_deque<unsigned int, 16>> deques;
	for (int i = 0; i < 50; ++i) {
		deques.push_back({});
		for (int j = 0; j < i; ++j)
			deques.back().push_back(j);
	}
}

TEST(CircularDequeTest, FullSmallClearInflate) {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 16; ++i)
		deque.push_back(i);
	deque.clear();
	for (int i = 0; i < 32; ++i)
		deque.push_back(i);
}

TEST(CircularDequeTest, FullSmallClearInflateCopyAssign) {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 16; ++i)
		deque.push_back(i);
	deque.clear();
	for (int i = 0; i < 32; ++i)
		deque.push_back(i);
	circular_deque<unsigned int, 16> deque2;
	deque2 = deque;
	EXPECT_EQ(deque2.size(), 32);
}

TEST(CircularDequeTest, SwapSmallSmall) {
	circular_deque<unsigned int, 16> a, b;
	a.push_back(0);
	b.push_back(1);

	swap(a, b);
	EXPECT_EQ(a.pop_back(), 1);
	EXPECT_EQ(b.pop_back(), 0);
}

