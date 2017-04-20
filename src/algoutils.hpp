/*
 * File:   algoutils.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on March 30, 2017, 12:17 AM
 */

#ifndef ALGOUTILS_HPP
#define ALGOUTILS_HPP

#include <iterator>

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

#endif /* ALGOUTILS_HPP */

