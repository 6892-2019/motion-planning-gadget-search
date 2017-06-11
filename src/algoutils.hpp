/*
 * File:   algoutils.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on March 30, 2017, 12:17 AM
 */

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

template<typename Iter>
bool is_possibly_mirrored_rotation_permutation(Iter first, Iter last) {
	auto dist = std::distance(first, last);
	assert(dist > 0);
	auto size = static_cast<std::make_unsigned_t<decltype(dist)>>(dist);
	using T = typename std::iterator_traits<Iter>::value_type;
	auto zeroit = std::find(first, last, 0);
	if (zeroit == last)
		return false;
	auto theirzero = std::distance(first, zeroit);

	dynarray<T> exemplar(size);
	std::copy(boost::counting_iterator<T>(0), boost::counting_iterator<T>(static_cast<T>(size)), exemplar.begin());
	std::rotate(exemplar.rbegin(), exemplar.rbegin()+theirzero, exemplar.rend());
	if (std::equal(first, last, exemplar.begin(), exemplar.end()))
		return true;
	std::copy(boost::counting_iterator<T>(0), boost::counting_iterator<T>(static_cast<T>(size)), exemplar.begin());
	std::reverse(exemplar.begin(), exemplar.end());
	std::rotate(exemplar.begin(), exemplar.begin()+(size-theirzero-1), exemplar.end());
	if (std::equal(first, last, exemplar.begin(), exemplar.end()))
		return true;
	return false;
}

template<typename T>
bool is_possibly_mirrored_rotation_permutation(std::initializer_list<T> list) {
	return is_possibly_mirrored_rotation_permutation(list.begin(), list.end());
}


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


template<typename T, typename Compare = std::less<T>>
int sgncmp(const T& left, const T& right, Compare comp = Compare()) {
	if (comp(left, right)) return -1;
	if (comp(right, left)) return 1;
	return 0;
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

struct indirect_equal {
	template<typename L, typename R>
	bool operator()(const L* l, const R* r) const noexcept(noexcept(*l == *r)) {
		//TODO: also test pointer equality (and return true)?
		//maybe as identical_or_indirect_equal?
		return *l == *r;
	}
	template<typename L, typename R>
	bool operator()(const std::unique_ptr<L>& l, const std::unique_ptr<R>& r) const noexcept(noexcept(*l == *r)) {
		return *l == *r;
	}
	template<typename T>
	std::size_t operator()(const std::unique_ptr<const T>& p) const noexcept(noexcept(std::hash<T>()(*p))) {
		return std::hash<T>()(*p);
	}
};
struct indirect_hash {
	template<typename T>
	std::size_t operator()(const T* p) const noexcept(noexcept(std::hash<T>()(*p))) {
		return std::hash<T>()(*p);
	}
	template<typename T>
	std::size_t operator()(const std::unique_ptr<T>& p) const noexcept(noexcept(std::hash<T>()(*p))) {
		return std::hash<T>()(*p);
	}
	template<typename T>
	std::size_t operator()(const std::unique_ptr<const T>& p) const noexcept(noexcept(std::hash<T>()(*p))) {
		return std::hash<T>()(*p);
	}
};


struct free_deleter {
	constexpr free_deleter() noexcept = default;
	template<typename T>
	void operator()(T* ptr) noexcept {
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
	if (present) return std::nullopt;
	return std::make_optional(std::forward<T>(value));
}
#endif /* ALGOUTILS_HPP */

