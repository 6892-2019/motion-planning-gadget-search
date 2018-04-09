#include "precompiled.hpp"
#include "circular_deque.hpp"
#include <doctest.h>

using std::swap;

TEST_CASE("CircularDequeTest_CtorDtor") {
	circular_deque<unsigned int, 16> deque;
	CHECK_UNARY(deque.empty());
	CHECK_EQ(deque.size(), 0);
	CHECK_EQ(deque.capacity(), 16);
}

TEST_CASE("CircularDequeTest_DtorNonemptySmall") {
	circular_deque<unsigned int, 16> deque;
	deque.push_back(0);
}

TEST_CASE("CircularDequeTest_DtorNonemptyNonsmall") {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 32; ++i)
		deque.push_back(i);
}

TEST_CASE("CircularDequeTest_PushPopSmall") {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 16; ++i)
		deque.push_back(i);
	CHECK_UNARY_FALSE(deque.empty());
	CHECK_EQ(deque.size(), 16);
	CHECK_EQ(deque.capacity(), 16);
	for (int i = 15; i >= 0; --i)
		CHECK_EQ(deque.pop_back(), i);
	CHECK_UNARY(deque.empty());
	CHECK_EQ(deque.size(), 0);
	CHECK_EQ(deque.capacity(), 16);
}

TEST_CASE("CircularDequeTest_PushPopLarge") {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 64; ++i)
		deque.push_back(i);
	CHECK_UNARY_FALSE(deque.empty());
	CHECK_EQ(deque.size(), 64);
	CHECK_GE(deque.capacity(), deque.size());
	for (int i = 63; i >= 0; --i)
		CHECK_EQ(deque.pop_back(), i);
	CHECK_UNARY(deque.empty());
	CHECK_EQ(deque.size(), 0);
}

TEST_CASE("CircularDequeTest_CycleStayingSmall") {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 8; ++i)
		deque.push_back(i);
	for (int cycle = 0; cycle < 1000; ++cycle) {
		deque.push_back(cycle);
		deque.pop_front();
	}
}

TEST_CASE("CircularDequeTest_CycleNonsmall") {
	circular_deque<unsigned int, 16> deque;
	for (int cycle = 0; cycle < 1000; ++cycle) {
		deque.push_back(cycle);
		deque.push_back(cycle);
		deque.pop_front();
	}
}

TEST_CASE("CircularDequeTest_EmplacedInReservedVector") {
	std::vector<circular_deque<unsigned int, 16>> deques;
	deques.reserve(50);
	for (int i = 0; i < 50; ++i)
		deques.emplace_back();
}

TEST_CASE("CircularDequeTest_EmplacedInVector") {
	std::vector<circular_deque<unsigned int, 16>> deques;
	for (int i = 0; i < 50; ++i)
		deques.emplace_back();
}

TEST_CASE("CircularDequeTest_PushedInReservedVector") {
	std::vector<circular_deque<unsigned int, 16>> deques;
	deques.reserve(50);
	for (int i = 0; i < 50; ++i)
		deques.push_back({});
}

TEST_CASE("CircularDequeTest_PushedInVector") {
	std::vector<circular_deque<unsigned int, 16>> deques;
	for (int i = 0; i < 50; ++i)
		deques.push_back({});
}

TEST_CASE("CircularDequeTest_PushedInVectorSomeInflated") {
	std::vector<circular_deque<unsigned int, 16>> deques;
	for (int i = 0; i < 50; ++i) {
		deques.push_back({});
		for (int j = 0; j < i; ++j)
			deques.back().push_back(j);
	}
}

TEST_CASE("CircularDequeTest_FullSmallClearInflate") {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 16; ++i)
		deque.push_back(i);
	deque.clear();
	for (int i = 0; i < 32; ++i)
		deque.push_back(i);
}

TEST_CASE("CircularDequeTest_FullSmallClearInflateCopyAssign") {
	circular_deque<unsigned int, 16> deque;
	for (int i = 0; i < 16; ++i)
		deque.push_back(i);
	deque.clear();
	for (int i = 0; i < 32; ++i)
		deque.push_back(i);
	circular_deque<unsigned int, 16> deque2;
	deque2 = deque;
	CHECK_EQ(deque2.size(), 32);
}

TEST_CASE("CircularDequeTest_SwapSmallSmall") {
	circular_deque<unsigned int, 16> a, b;
	a.push_back(0);
	b.push_back(1);

	swap(a, b);
	CHECK_EQ(a.pop_back(), 1);
	CHECK_EQ(b.pop_back(), 0);
}

