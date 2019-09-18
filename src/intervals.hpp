/*
 * File:   intervals.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 11, 2019, 12:02 AM
 */

#ifndef INTERVALS_HPP
#define INTERVALS_HPP

#include "farmhash-util.hpp"
#include "tsl/ordered_map.h"
#include <vector>
#include <utility>
#include <cassert>
#include <iterator>
#include <algorithm>

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
template<typename Iterable>
auto interval_size(const Iterable& iterable)
		//This expression SFINAE doesn't check the value_type is tuple-ish, but this is good enough for now.
		-> decltype(std::begin(std::declval<Iterable>()), void(), static_cast<std::size_t>(0)) {
	using std::begin, std::end;
	return interval_size(begin(iterable), end(iterable));
}

template<typename It1, typename T>
bool interval_contains(It1 left, It1 left_end, const T& element) {
	if (left == left_end) return false;
	//Branchless binary search based on Listing 2 from
	//Array Layouts for Comparison-Based Searching by Khuong and Morin
	//which is itself based on Knuth.  We did take a branch just to check for
	//the empty range (hopefully that's predictable).  This also assumes
	//random-access iterators, but should be adaptable.
	It1 base = left;
	std::size_t n = std::distance(left, left_end);
	while (n > 1) {
		std::size_t half = n / 2;
		base = element < base[half].first ? base : base+half;
		n -= half;
	}
	return ((base->first <= element) & (element < base->second));
}
template<typename Iterable, typename T>
bool interval_contains(const Iterable& iterable, const T& element) {
	using std::begin, std::end;
	return interval_contains(begin(iterable), end(iterable), element);
}

//This would be an enum class if they could have conversion operators.
struct overlap_result {
	static const overlap_result overlap, adjacent, separate;
	explicit operator bool() const noexcept {check(v); return v == 0;}
	bool operator==(overlap_result o) const noexcept {check(v); check(o.v); return v == o.v;}
	bool operator!=(overlap_result o) const noexcept {return !(*this == o);}
	unsigned int v; //can't be private, or no longer an aggregate/trivial
private:
	static void check(unsigned int v) {
#ifndef NDEBUG
		if (v >= 3) throw std::logic_error(fmt::format("bad overlap_result: {}", v));
#endif //NDEBUG
	}
};
constexpr const overlap_result overlap_result::overlap = overlap_result{0};
constexpr const overlap_result overlap_result::adjacent = overlap_result{1};
constexpr const overlap_result overlap_result::separate = overlap_result{2};

template<typename It1, typename It2>
overlap_result interval_overlap(It1 left, It1 left_end, It2 right, It2 right_end) {
	bool adjacent = false;
	while (left != left_end && right != right_end) {
		if ((right->first <= left->first && left->first < right->second) ||
				(left->first <= right->first && right->first < left->second))
			return overlap_result::overlap;
		if (left->second == right->first || right->second == left->first)
			adjacent = true;
		if (left->first < right->first)
			++left;
		else
			++right;
	}
	return adjacent ? overlap_result::adjacent : overlap_result::separate;
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

template<typename T, typename K>
std::vector<std::pair<std::vector<K>, std::vector<std::pair<T, T>>>> interval_aggregate(
		const std::vector<std::pair<K, std::vector<std::pair<T, T>>>>& intervals) {
	//This is a sweep-line-based multiway group intersection to group intervals
	//having the same set of combine rights.  Each combine right is "active" or
	//"inactive", changing state at interval endpoints.  At each event point,
	//the current interval is committed with the current active set, then the
	//active set is updated.
	//TODO: assert the K are distinct
	std::vector<K> active;
	active.reserve(intervals.size());
	//There will usually be far fewer groups than intervals, so pre-sizing is cheap enough to make sense.
	std::size_t event_count = 0;
	for (const std::pair<K, std::vector<std::pair<T, T>>>& i : intervals)
		event_count += 2*i.second.size(); //not interval_size
	//(event point, true = becoming active, false = becoming inactive, the combine right)
	std::vector<std::tuple<T, bool, K>> events;
	events.reserve(event_count);
	for (const std::pair<K, std::vector<std::pair<T, T>>>& i : intervals)
		for (const std::pair<T, T>& j : i.second) {
			events.emplace_back(j.first, true, i.first);
			events.emplace_back(j.second, false, i.first);
		}
	std::sort(events.begin(), events.end(), std::greater<>()); //reversed sort for pop_back()
	//The previous event point.  Initializing to 0 is safe because the active
	//set starts empty, so we won't emit a spurious interval.  Similarly, a loop
	//epilogue is unnecessary because the active set is empty at the end.
	T cur = 0;
	//vector_ordered_map
	tsl::ordered_map<std::vector<K>, std::vector<std::pair<T, T>>, farmhash_hash,
			std::equal_to<std::vector<K>>, std::allocator<std::pair<std::vector<K>, std::vector<std::pair<T, T>>>>,
			std::vector<std::pair<std::vector<K>, std::vector<std::pair<T, T>>>>> result;
	while (!events.empty()) {
		T event_point = std::get<0>(events.back());
		if (!active.empty()) {
			//We don't retain sorted order during insertions and removals, so we
			//need to sort here.  (If most event points only occur for one list
			//of intervals, maintaining order might be faster.)
			std::sort(active.begin(), active.end());
			result[active].emplace_back(cur, event_point);
		}
		cur = event_point;

		//Process all events at this point.
		while (!events.empty() && std::get<0>(events.back()) == event_point) {
			std::tuple<T, bool, K> e = events.back();
			events.pop_back();
			if (std::get<1>(e)) {
				assert(std::find(active.begin(), active.end(), std::get<2>(e)) == active.end());
				active.push_back(std::get<2>(e));
			} else {
				auto it = std::find(active.begin(), active.end(), std::get<2>(e));
				assert(it != active.end());
				std::iter_swap(it, active.end()-1);
				active.pop_back();
			}
		}
	}
	return std::move(result).values_container();
}

#endif /* INTERVALS_HPP */

