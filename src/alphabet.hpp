/*
 * File:   alphabet.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 25, 2016, 11:30 PM
 */

#ifndef ALPHABET_HPP
#define ALPHABET_HPP

#include "precompiled.hpp"

struct BooleanAlphabet {
	using symbol_type = bool;
	static constexpr std::array<symbol_type, 2> symbols = {false, true};
	static constexpr unsigned int find(symbol_type symbol) {
		return symbol ? 1 : 0;
	}
	static constexpr symbol_type at(unsigned int index) {
		return symbols.at(index);
	}
};

#endif /* ALPHABET_HPP */

