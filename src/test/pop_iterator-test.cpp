#include "precompiled.hpp"
#include "pop_iterator.hpp"
#include "algoutils.hpp"
#include "doctest.h"

TEST_CASE("PopFrontIterator_DequeMerge") {
	std::deque<int> left = {1, 2, 5, 6, 10};
	std::deque<int> right = {3, 4, 7, 8, 9};
	std::deque<int> actual;
	std::merge(pop_front_begin(left), pop_front_end(left), pop_front_begin(right), pop_front_end(right), std::back_inserter(actual));
	CHECK_UNARY(left.empty());
	CHECK_UNARY(right.empty());
	std::initializer_list<int> expected = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("PopFrontIterator_DequeMergeUnique") {
	std::deque<int> left = {1, 2, 3, 4, 5, 6, 10};
	std::deque<int> right = {3, 4, 7, 8, 9, 10};
	std::deque<int> actual;
	merge_unique(pop_front_begin(left), pop_front_end(left), pop_front_begin(right), pop_front_end(right), std::back_inserter(actual));
	CHECK_UNARY(left.empty());
	CHECK_UNARY(right.empty());
	std::initializer_list<int> expected = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("PopFrontIterator_RangeFor") {
	std::deque<int> left = {1, 2, 5, 6, 10};
	std::deque<int> actual;
	for (int x : pop_front_range(left))
		actual.push_back(x);
	CHECK_UNARY(left.empty());
	std::initializer_list<int> expected = {1, 2, 5, 6, 10};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("PopFrontIterator_Distance") {
	std::deque<int> left = {1, 2, 5, 6, 10};
	using std::distance;
	CHECK_EQ(distance(pop_front_begin(left), pop_front_end(left)), left.size());
}


TEST_CASE("PopBackIterator_VectorMerge") {
	std::vector<int> left = {1, 2, 5, 6, 10};
	std::vector<int> right = {3, 4, 7, 8, 9};
	std::vector<int> actual;
	std::merge(pop_back_begin(left), pop_back_end(left), pop_back_begin(right), pop_back_end(right), std::back_inserter(actual), std::greater<>());
	CHECK_UNARY(left.empty());
	CHECK_UNARY(right.empty());
	std::initializer_list<int> expected = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

//merge_unqiue not tested because we don't have an overload taking a comparator object

TEST_CASE("PopBackIterator_RangeFor") {
	std::vector<int> left = {1, 2, 5, 6, 10};
	std::vector<int> actual;
	for (int x : pop_back_range(left))
		actual.push_back(x);
	CHECK_UNARY(left.empty());
	std::initializer_list<int> expected = {10, 6, 5, 2, 1};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("PopBackIterator_Distance") {
	std::vector<int> left = {1, 2, 5, 6, 10};
	using std::distance;
	CHECK_EQ(distance(pop_back_begin(left), pop_back_end(left)), left.size());
}