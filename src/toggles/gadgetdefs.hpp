/*
 * File:   gadgetdefs.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 6, 2018, 3:17 AM
 */

#ifndef GADGETDEFS_HPP
#define GADGETDEFS_HPP

#include <string_view>

namespace automaton {
class WorkingAutomaton;
}

std::unique_ptr<automaton::WorkingAutomaton> known_gadget(std::string_view name, unsigned int alphabet_size);

#endif /* GADGETDEFS_HPP */

