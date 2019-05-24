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

template<typename ForwardIterator,
		typename T = typename std::common_type<
				//should be using std::tuple_element here, I guess...
				typename std::iterator_traits<ForwardIterator>::value_type::first_type,
				typename std::iterator_traits<ForwardIterator>::value_type::second_type
		>::type>
std::vector<T> interval_inflate(ForwardIterator first, ForwardIterator last) {
	std::vector<T> ret;
	if (first == last)
		return ret;

	//could ret.reserve(interval_size(first, last)) at the cost of another iteration
	while (first != last) {
		for (auto i = first->first; i != first->second; ++i)
			ret.push_back(i);
		++first;
	}
	return ret;
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
	assert(std::is_sorted(left, left_end));
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



template<typename T>
class interval_accumulator {
public:
	interval_accumulator(std::size_t buffer_capacity) {
		if (!buffer_capacity)
			throw std::length_error("interval_accumulator must have buffer capacity");
		buf.reserve(buffer_capacity);
	}
	void operator()(T x) {
		//TODO: We're usually adding in sorted order, and some of those things
		//have duplicates, so we could return early if (!buf.empty() && x == buf.back()).
		//That would mean we drain less often at the cost of an unpredictable
		//branch in operator().
		if (buf.size() == buf.capacity())
			drain_buffer();
		buf.push_back(x);
	}
	//TODO: when this class was written inline, before inserting a batch of N
	//items, we'd check we had space (draining early if necessary), saving
	//having to check on each element.  Our elements aren't usually contiguous
	//(being extracted from structs), so it's not clear how to modularize that.
	//That approach also threw if any one batch was too big for the buffer, but
	//the new approach never has that problem, so there's some advantage here.

	//This is &&-qualified to preserve the chance to use a mutating
	//interval_coalesce if I ever write one.
	std::vector<std::pair<T, T>> finish() && {
		drain_buffer();
		std::sort(accum.begin(), accum.end());
		auto ret = interval_coalesce(accum.cbegin(), accum.cend());
		accum.clear();
		return ret;
	}
private:
	std::vector<std::pair<T, T>> accum;
	std::vector<T> buf;
	void drain_buffer() {
		std::sort(buf.begin(), buf.end());
		buf.erase(std::unique(buf.begin(), buf.end()), buf.end());
		//TODO: we could avoid this temporary with a maximal_intervals overload
		//using an output iterator (a back_inserter into accum);
		auto ints = maximal_intervals(buf.begin(), buf.end());
		accum.insert(accum.end(), ints.begin(), ints.end());
		buf.clear();
		//TODO: we might want to coalesce accum periodically to reduce peak
		//memory usage, though we'd also want to stop coalescing if we don't
		//get any size reduction.
	}
};


/**
 * Returns a list of interval lists each having interval_size equal to the given
 * chunk size (except the last chunk, which may be shorter).
 *
 * TODO: We'd like this to be a generator rather than allocating a bunch of
 * vectors.  That generator probably returns a const ref to a vector stored
 * inside itself.
 */
template<typename It1,
		typename T = typename std::common_type<
				//should be using std::tuple_element here, I guess...
				typename std::iterator_traits<It1>::value_type::first_type,
				typename std::iterator_traits<It1>::value_type::second_type
		>::type>
std::vector<std::vector<std::pair<T, T>>> interval_chunk(It1 left, It1 left_end, std::size_t chunk_size) {
	if (chunk_size == 0) throw std::logic_error("zero chunk size");
	std::vector<std::vector<std::pair<T, T>>> ret;
	std::vector<std::pair<T, T>> working;
	std::size_t working_interval_size = 0;
	while (left != left_end) {
		std::pair<T, T> cur = *left++;
		while (cur.first != cur.second) {
			std::size_t needed = chunk_size - working_interval_size;
			//TODO: numeric_cast silences the warning in the case we currently
			//care about, but makes this function less general
			T endpoint = std::min<T>(numeric_cast<T>(cur.first + needed), cur.second);
			working.emplace_back(cur.first, endpoint);
			working_interval_size += endpoint - cur.first;
			cur.first = endpoint;
			if (working_interval_size == chunk_size) {
				ret.push_back(std::move(working));
				working.clear();
				working_interval_size = 0;
			}
		}
	}
	if (!working.empty()) //straggling chunk
		ret.push_back(std::move(working));
	return ret;
}

template<typename It1>
std::size_t interval_size(It1 left, It1 left_end) {
	std::size_t size = 0;
	for (auto i = left; i != left_end; ++i)
		size += i->second - i-> first;
	return size;
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

