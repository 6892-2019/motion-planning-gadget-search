/*
 * File:   ioutils.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on November 27, 2016, 12:08 AM
 */

#ifndef IOUTILS_HPP
#define IOUTILS_HPP

#include <vector>
#include <string>
#include <experimental/string_view>

/**
 * Reads all lines from the file named by the given filename.
 * @return the lines in the file
 */
std::vector<std::string> readAllLines(std::string filename);

/**
 * Writes all lines in the given vector into a file at the given filename.
 */
void writeAllLines(std::string filename, const std::vector<std::string>& lines);

/**
 * If the given string starts with the given prefix, erases that prefix.
 * @return true iff the prefix was present (and erased)
 */
bool removePrefix(std::string& str, std::experimental::string_view prefix);

#endif /* IOUTILS_HPP */

