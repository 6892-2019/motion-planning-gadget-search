/*
 * File:   gadgetdefs.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 6, 2018, 3:17 AM
 */

#ifndef GADGETDEFS_HPP
#define GADGETDEFS_HPP

#include <string_view>
#include <stdexcept>

namespace automaton {
class WorkingAutomaton;
}

class unknown_gadget : public std::runtime_error {
public:
	unknown_gadget(std::string_view gadget, unsigned int size) : unknown_gadget(std::string(gadget), size) {}
	unknown_gadget(std::string gadget, unsigned int size)
			: std::runtime_error(format(gadget, size)), gadget_(std::move(gadget)), requested_(size) {}
	const std::string& gadget() const {return gadget_;}
	unsigned int requested() const {return requested_;}
private:
	std::string gadget_;
	unsigned int requested_;
	static std::string format(const std::string& thing, unsigned int requested);
};

class bad_alphabet_size : public std::runtime_error {
public:
	bad_alphabet_size(std::string_view gadget, unsigned int requested, unsigned int required)
			: bad_alphabet_size(std::string(gadget), requested, required) {}
	bad_alphabet_size(std::string gadget, unsigned int requested, unsigned int required)
			: std::runtime_error(format(gadget, requested, required)),
			gadget_(std::move(gadget)), requested_(requested), required_(required) {}
	const std::string& gadget() const {return gadget_;}
	unsigned int requested() const {return requested_;}
	unsigned int required() const {return required_;}
private:
	std::string gadget_;
	unsigned int requested_, required_;
	static std::string format(const std::string& gadget, unsigned int requested, unsigned int required);
};

std::unique_ptr<automaton::WorkingAutomaton> known_gadget(std::string_view name, unsigned int alphabet_size);

/**
 * Returns the names or regexes of known gadgets.  Does not include aliases.
 * This is primarily for testing, but might be useful for displaying a 'did you
 * mean?' message.
 * @return a vector of known gadget names (possibly regexes)
 */
std::vector<std::string_view> known_gadget_keys();

#endif /* GADGETDEFS_HPP */

