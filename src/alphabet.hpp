// SPDX-License-Identifier: MIT
// Copyright 2016 Massachusetts Institute of Technology
#ifndef ALPHABET_HPP
#define ALPHABET_HPP

#include <cstdint>
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

template<unsigned int Size>
struct ByteAlphabet {
	using symbol_type = uint8_t;
	static constexpr unsigned int size = Size;
	static constexpr unsigned int find(symbol_type symbol) {
		return static_cast<unsigned int>(symbol);
	}
	static constexpr symbol_type at(unsigned int index) {
		assert(index < size);
		return static_cast<symbol_type>(index);
	}
};

#endif /* ALPHABET_HPP */

