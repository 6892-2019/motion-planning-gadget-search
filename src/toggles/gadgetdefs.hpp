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

/**
 * Returns the names or regexes of known gadgets.  Does not include aliases.
 * This is primarily for testing, but might be useful for displaying a 'did you
 * mean?' message.
 * @return a vector of known gadget names (possibly regexes)
 */
std::vector<std::string_view> known_gadget_keys();

#endif /* GADGETDEFS_HPP */

