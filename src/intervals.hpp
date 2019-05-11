/*
 * File:   intervals.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 11, 2019, 12:02 AM
 */

#ifndef INTERVALS_HPP
#define INTERVALS_HPP

#include <vector>
#include <utility>
#include <cassert>
#include <iterator>

//having T as a second template argument lets the caller override the type
//(e.g., wider or narrower) at the cost of also having to spell out the iterator type
//The fully-algorithm thing to is to write to an output iterator...
template<typename ForwardIterator, typename T = typename std::iterator_traits<ForwardIterator>::value_type>
auto maximal_intervals(ForwardIterator first, ForwardIterator last) -> std::vector<std::pair<T, T>> {
	assert(std::is_sorted(first, last));
	std::vector<std::pair<T, T>> intervals;
	if (first == last)
		return intervals;

	auto left = first, right = first;
	//Build maximal ranges, first inclusive and last exclusive.
	while (true) {
		if (right+1 == last) {
			intervals.emplace_back(*left, *right + 1);
			break;
		} else if (*(right+1) - *right != 1) {
			intervals.emplace_back(*left, *right + 1);
			left = right = right+1;
		} else
			++right;
	}
	assert(std::is_sorted(intervals.begin(), intervals.end()));
	return intervals;
}



template<typename It1, typename It2,
		typename T = typename std::common_type<
				//should be using std::tuple_element here, I guess...
				typename std::iterator_traits<It1>::value_type::first_type,
				typename std::iterator_traits<It1>::value_type::second_type,
				typename std::iterator_traits<It2>::value_type::first_type,
				typename std::iterator_traits<It2>::value_type::second_type
		>::type>
std::vector<std::pair<T, T>> interval_intersection(It1 left, It1 left_end, It2 right, It2 right_end) {
	std::vector<std::pair<T, T>> ret;
	while (left != left_end && right != right_end) {
		if (left->second <= right->first)
			++left;
		else if (right->second <= left->first)
			++right;
		else {
			auto first = std::max(left->first, right->first),
					second = std::min(left->second, right->second);
			assert(first < second);
			ret.emplace_back(std::move(first), std::move(second));
			if (left->second < right->second)
				++left;
			else if (right->second < left->second)
				++right;
			else {
				++left;
				++right;
			}
		}
	}
	return ret;
}

//template<typename T, typename It1>
//std::vector<std::pair<T, T>> interval_intersection(It1 first1, It1 last1, std::pair<T, T> interval) {
//	std::array<std::pair<T, T>, 1> right = {std::move(interval)};
//	return interval_intersection(first1, last1, right.cbegin(), right.cend());
//}
//template<typename T, typename It1>
//std::vector<std::pair<T, T>> interval_intersection(It1 first1, It1 last1, T beginInclusive, T endExclusive) {
//	return interval_intersection(first1, last1, std::make_pair(std::move(beginInclusive), std::move(endExclusive)));
//}

//template<typename It1, typename It2,
//		typename T = typename std::common_type<
//				//should be using std::tuple_element here, I guess...
//				typename std::iterator_traits<It1>::value_type::first_type,
//				typename std::iterator_traits<It1>::value_type::second_type,
//				typename std::iterator_traits<It2>::value_type::first_type,
//				typename std::iterator_traits<It2>::value_type::second_type
//		>::type>
//std::vector<std::pair<T, T>> interval_union(It1 left, It1 left_end, It2 right, It2 right_end) {
//	std::vector<std::pair<T, T>> ret;
//	while (left != left_end && right != right_end) {
//
//	}
//	while (left != left_end)
//		ret.push_back(*left++);
//	while (right != right_end)
//		ret.push_back(*right++);
//	return ret;
//}

//template<typename T, typename It1>
//std::vector<std::pair<T, T>> interval_union(It1 first1, It1 last1, std::pair<T, T> interval) {
//	std::array<std::pair<T, T>, 1> right = {std::move(interval)};
//	return interval_union(first1, last1, right.cbegin(), right.cend());
//}
//template<typename T, typename It1>
//std::vector<std::pair<T, T>> interval_union(It1 first1, It1 last1, T beginInclusive, T endExclusive) {
//	return interval_union(first1, last1, std::make_pair(std::move(beginInclusive), std::move(endExclusive)));
//}

#endif /* INTERVALS_HPP */

