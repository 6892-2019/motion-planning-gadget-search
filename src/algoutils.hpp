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

#endif /* ALGOUTILS_HPP */

