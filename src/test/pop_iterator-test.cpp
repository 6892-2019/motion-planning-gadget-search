#include "precompiled.hpp"
#include "pop_iterator.hpp"
#include "doctest.h"

//TEST_CASE("PopFrontIterator_DequeMerge") {
//	std::deque<int> left = {1, 2, 5, 6, 10};
//	std::deque<int> right = {3, 4, 7, 8, 9};
//	std::deque<int> actual;
//	std::merge(pop_front_begin(left), pop_front_end(left), pop_front_begin(right), pop_front_end(right), std::back_inserter(actual));
//	CHECK_UNARY(left.empty());
//	CHECK_UNARY(right.empty());
//	std::initializer_list<int> expected = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
//	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
//}

TEST_CASE("PopFront_asdfd") {
	std::deque<int> foo = {1, 2, 3};
	pop_front_iterator i(foo);
	*i;
}