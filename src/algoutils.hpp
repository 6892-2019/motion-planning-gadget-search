// SPDX-License-Identifier: MIT
// Copyright 2017 Massachusetts Institute of Technology
// Copyright 2025 Jeffrey Bosboom
#ifndef ALGOUTILS_HPP
#define ALGOUTILS_HPP

#include <iterator>
#include "dynarray.hpp"

//by Raymond Chen: https://blogs.msdn.microsoft.com/oldnewthing/20170104-00/?p=95115
//typos corrected, reformatted
//thinking about it, maybe just "permute" would be a better name, should I
//actually write a standards paper for this
//in light of the function below, maybe "permute_from" and "permute_to"
//indices indicate where an element moves from
template<typename Iter1, typename Iter2>
void apply_permutation(Iter1 first, Iter1 last, Iter2 indices) {
	using T = typename std::iterator_traits<Iter1>::value_type;
	using Diff = typename std::iterator_traits<Iter2>::difference_type;
	Diff length = last - first;
	for (Diff i = 0; i < length; i++) {
		Diff current = i;
		if (i != indices[current]) {
			T t{std::move(first[i])};
			while (i != indices[current]) {
				Diff next = indices[current];
				first[current] = std::move(first[next]);
				indices[current] = current;
				current = next;
			}
			first[current] = std::move(t);
			indices[current] = current;
		}
	}
}

//by Raymond Chen: https://blogs.msdn.microsoft.com/oldnewthing/20170111-00/?p=95165
//reformated, throws changed to asserts
//indices indicate where an element should move to
template<typename Iter1, typename Iter2>
void apply_reverse_permutation(Iter1 first, Iter1 last, Iter2 indices) {
	using Diff = typename std::iterator_traits<Iter2>::difference_type;
	using std::swap;
	Diff length = std::distance(first, last);
	for (Diff i = 0; i < length; i++) {
		while (i != indices[i]) {
			Diff next = indices[i];
			assert(0 <= next && next <= length && "invalid index in permutation");
			assert(next != indices[next] && "not a permutation");
			swap(first[i], first[next]);
			swap(indices[i], indices[next]);
		}
	}
}

struct identity_permutation {
	template<typename T>
	T operator[](const T& t) const {return t;}
	template<typename T>
	T operator[](T&& t) const {return t;}
};


template<typename LeftIter, typename RightIter>
bool unordered_equal(LeftIter first1, LeftIter end1, RightIter first2, RightIter end2) {
	if (std::distance(first1, end1) != std::distance(first2, end2))
		return false;
	for (LeftIter i = first1; i != end1; ++i)
		if (std::count(first1, end1, *i) != std::count(first2, end2, *i))
			return false;
	return true;
}
template<typename T, typename RightIter>
bool unordered_equal(std::initializer_list<T> left, RightIter first2, RightIter end2) {
	return unordered_equal(left.begin(), left.end(), first2, end2);
}
template<typename T, typename LeftIter>
bool unordered_equal(LeftIter first1, LeftIter end1, std::initializer_list<T> right) {
	return unordered_equal(first1, end1, right.begin(), right.end());
}
template<typename T, typename U>
bool unordered_equal(std::initializer_list<T> left, std::initializer_list<U> right) {
	return unordered_equal(left.begin(), left.end(), right.begin(), right.end());
}



template<class Front, class Sentinel>
class range_for_pair {
public:
	range_for_pair(Front front, Sentinel sentinel) : front_(std::move(front)), sentinel_(std::move(sentinel)) {}
	range_for_pair(std::pair<Front, Sentinel> pair) : front_(std::move(pair.first)), sentinel_(std::move(pair.second)) {}
	Front begin() const {
		return front_;
	}
	Sentinel end() const {
		return sentinel_;
	}
private:
	Front front_;
	Sentinel sentinel_;
};

template<class Front, class Sentinel>
range_for_pair<Front, Sentinel> make_range_for_pair(Front front, Sentinel sentinel) {
	return {front, sentinel};
}
template<class Front, class Sentinel>
range_for_pair<Front, Sentinel> as_range_for_pair(std::pair<Front, Sentinel> pair) {
	return {pair};
}


template<typename Integer>
auto xrange(Integer last) {
	return boost::irange(static_cast<Integer>(0), last);
}
template<typename Integer>
auto xrange(Integer first, Integer last) {
	return boost::irange(first, last);
}


struct free_deleter {
	constexpr free_deleter() noexcept = default;
	template<typename T>
	void operator()(T* ptr) const noexcept {
		std::free(ptr);
	}
};


template<class Target, class Source>
[[nodiscard]] std::unique_ptr<Target> unique_cast(std::unique_ptr<Source>& p) {
	return std::unique_ptr<Target>(static_cast<Target*>(p.release()));
}
template<class Target, class Source>
[[nodiscard]] std::unique_ptr<Target> unique_cast(std::unique_ptr<Source>&& p) {
	return std::unique_ptr<Target>(static_cast<Target*>(p.release()));
}


//std::optional's ctors always or never create an empty optional, so we need this
template<typename T>
[[nodiscard]] std::optional<std::decay_t<T>> maybe_opt(bool present, T&& value) {
	if (!present) return std::nullopt;
	return std::make_optional(std::forward<T>(value));
}


template<typename InputIter1, typename InputIter2, typename OutputIter>
OutputIter merge_unique(InputIter1 first1, InputIter1 last1, InputIter2 first2, InputIter2 last2, OutputIter output) {
	while (first1 != last1 && first2 != last2) {
		if (*first1 < *first2)
			*output++ = *first1++;
		else if (*first2 < *first1)
			*output++ = *first2++;
		else {
			*output++ = *first1++;
			first2++;
		}
	}
	output = std::copy(first1, last1, output);
	return std::copy(first2, last2, output);
}

#endif /* ALGOUTILS_HPP */
