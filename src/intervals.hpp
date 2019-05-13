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


//TODO: should be easy enough to make this modify in-place (like std::unique),
//though we could still have interval_coalesce_copy if useful
template<typename It1,
		typename T = typename std::common_type<
				//should be using std::tuple_element here, I guess...
				typename std::iterator_traits<It1>::value_type::first_type,
				typename std::iterator_traits<It1>::value_type::second_type
		>::type>
std::vector<std::pair<T, T>> interval_coalesce(It1 left, It1 left_end) {
	if (left == left_end)
		return {};
	std::vector<std::pair<T, T>> ret;
	ret.push_back(*left++);
	while (left != left_end) {
		if (left->first <= ret.back().second)
			ret.back().second = std::max(ret.back().second, left++->second);
		else
			ret.push_back(*left++);
	}
	return ret;
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

template<typename It1, typename It2,
		typename T = typename std::common_type<
				//should be using std::tuple_element here, I guess...
				typename std::iterator_traits<It1>::value_type::first_type,
				typename std::iterator_traits<It1>::value_type::second_type,
				typename std::iterator_traits<It2>::value_type::first_type,
				typename std::iterator_traits<It2>::value_type::second_type
		>::type>
std::vector<std::pair<T, T>> interval_union(It1 left, It1 left_end, It2 right, It2 right_end) {
	if (left == left_end && right == right_end)
		return {};
	if (left == left_end)
		return {right, right_end};
	if (right == right_end)
		return {left, left_end};

	std::vector<std::pair<T, T>> ret;
	while (left != left_end && right != right_end) {
		if (ret.empty())
			ret.push_back(left->first < right-> first ? *left++ : *right++);
		else if (left->first < right->first)
			if (left->first <= ret.back().second)
				ret.back().second = std::max(ret.back().second, left++->second);
			else
				ret.push_back(*left++);
		else
			if (right->first <= ret.back().second)
				ret.back().second = std::max(ret.back().second, right++->second);
			else
				ret.push_back(*right++);
	}
	//We could do slightly better here if we assume the inputs are disjoint and non-adjacent.
	while (left != left_end)
		if (left->first <= ret.back().second)
			ret.back().second = std::max(ret.back().second, left++->second);
		else
			ret.push_back(*left++);
	while (right != right_end)
		if (right->first <= ret.back().second)
			ret.back().second = std::max(ret.back().second, right++->second);
		else
			ret.push_back(*right++);
	return ret;
}

template<typename It1, typename It2,
		typename T = typename std::common_type<
				//should be using std::tuple_element here, I guess...
				typename std::iterator_traits<It1>::value_type::first_type,
				typename std::iterator_traits<It1>::value_type::second_type,
				typename std::iterator_traits<It2>::value_type::first_type,
				typename std::iterator_traits<It2>::value_type::second_type
		>::type>
std::vector<std::pair<T, T>> interval_difference(It1 left, It1 left_end, It2 right, It2 right_end) {
	if (left == left_end)
		return {};
	if (right == right_end)
		return {left, left_end};

	//Based on https://stackoverflow.com/a/11891418/3614835.  It's an unclear
	//description of the algorithm; the core idea is to pretend we're setting
	//the first endpoint of one or the other interval.  The effect is like a
	//1-dimensional sweep line algorithm.
	std::vector<std::pair<T, T>> ret;
	T pos = std::min(left->first, right->first);
	while (left != left_end && right != right_end) {
		//pos is the "effective" first endpoint of one or both of the intervals.
		T elf = std::max(pos, left->first), erf = std::max(pos, right->first);
		if (elf < erf)
			if (left->second <= right->first) {
				ret.emplace_back(elf, left->second);
				pos = left->second;
				++left;
			} else {
				ret.emplace_back(elf, right->first);
				pos = right->first;
			}
		else if (erf < elf) {
			//we don't emit anything in this case because we're finding the
			//(asymmetric) difference.
			pos = left->first;
			if (right->second <= pos)
				++right;
		} else {
			if (left->second < right->second)
				pos = left++->second;
			else if (right->second < left->second)
				pos = right++->second;
			else {
				pos = left->second;
				++left;
				++right;
			}
		}
	}

	if (left != left_end) {
		//We have to handle the first left interval specially in case it was
		//interrupted by a right interval.  Then just copy any remaining.
		if (pos > left->first)
			ret.emplace_back(pos, left++->second);
		ret.insert(ret.end(), left, left_end);
	}
	return ret;
}

#endif /* INTERVALS_HPP */

