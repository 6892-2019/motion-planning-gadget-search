/*
 * File:   fmtutils.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 2, 2018, 11:40 PM
 */

#ifndef AUTOMATON_FMTUTILS_HPP_INCLUDED
#define AUTOMATON_FMTUTILS_HPP_INCLUDED

#include <cstddef>
#include <fmt/format.h>

//format std::byte like unsigned int (not uint8_t, as that's a character type)
template <>
struct fmt::formatter<std::byte> : fmt::formatter<unsigned int> {
	//use inherited parse()
	template <typename FormatContext>
	auto format(std::byte b, FormatContext &ctx) {
		return formatter<unsigned int>::format(std::to_integer<unsigned int>(b), ctx);
	}
};

#endif /* AUTOMATON_FMTUTILS_HPP_INCLUDED */

