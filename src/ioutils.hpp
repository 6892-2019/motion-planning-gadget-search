// SPDX-License-Identifier: MIT
// Copyright 2016 Massachusetts Institute of Technology
#ifndef IOUTILS_HPP
#define IOUTILS_HPP

#include <vector>
#include <string>
#include <string_view>

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
 * Process response files among the given range of arguments.  Strings beginning
 * with '@' are interpreted as paths to files containing further arguments, one
 * per line, which are resolved relative to the response file.  No validation is
 * done to check if the rest of the arguments actually denote files.
 */
std::vector<std::string> processFilenameArgs(const char** first, const char** last);
std::vector<std::string> processFilenameArgs(std::vector<std::string> args); //passing by value to merge const&/&& overloads

std::string make_temp_filename(std::string_view base, std::string_view ext = "txt");

#endif /* IOUTILS_HPP */

