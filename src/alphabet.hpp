/*
 * File:   alphabet.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 25, 2016, 11:30 PM
 */

#ifndef ALPHABET_HPP
#define ALPHABET_HPP

#include <cassert>

struct BooleanAlphabet {
	using symbol_type = bool;
	static constexpr unsigned int size = 2;
	static constexpr unsigned int find(symbol_type symbol) {
		return static_cast<unsigned int>(symbol);
	}
	static constexpr symbol_type at(unsigned int index) {
		assert(index < size);
		return static_cast<bool>(index);
	}
};

#endif /* ALPHABET_HPP */

