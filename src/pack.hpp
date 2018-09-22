/*
 * File:   pack.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on September 20, 2018, 8:31 PM
 */

#ifndef AUTOMATON_PACK_HPP_INCLUDED
#define AUTOMATON_PACK_HPP_INCLUDED

#include <cstddef>
#include "automatonbase.hpp"

namespace automaton {

using Pack = std::byte;

Pack* pack(const AutomatonBase& a, Pack* first, Pack* last);
const Pack* unpack(WorkingAutomaton& a, const Pack* first, const Pack* last = nullptr);
unsigned int packed_size(const Pack* pack);
std::size_t packed_hash(const Pack* pack);
bool packed_equal(const Pack* left, const Pack* right);
//could provide memcmp if useful

} //namespace automaton

#endif /* AUTOMATON_PACK_HPP_INCLUDED */

