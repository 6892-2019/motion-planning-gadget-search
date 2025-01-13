// SPDX-License-Identifier: MIT
// Copyright 2017 Massachusetts Institute of Technology
#ifndef AUTOMATON_IO_HPP
#define AUTOMATON_IO_HPP

#include "automaton.hpp"
#include "ioutils.hpp"
#include "algoutils.hpp"

namespace automaton {

std::string defaultFilename(const AutomatonBase& a);

void serialize(const AutomatonBase& a, std::string filename);

std::unique_ptr<WorkingAutomaton> deserialize(std::string filename);
template<unsigned int N>
inline std::unique_ptr<Automaton<N>> deserialize(std::string filename) {
	auto p = deserialize(filename);
	return unique_cast<Automaton<N>>(p);
}
}

#endif /* AUTOMATON_IO_HPP */

