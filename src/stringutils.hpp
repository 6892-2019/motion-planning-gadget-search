/*
 * File:   stringutils.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 25, 2018, 6:53 PM
 */

#ifndef STRINGUTILS_HPP
#define STRINGUTILS_HPP

#include <string>
#include <string_view>
#include <vector>

/**
 * If the given string starts with the given prefix, erases that prefix.
 * @return true iff the prefix was present (and erased)
 */
bool removePrefix(std::string& str, std::string_view prefix);

/**
 * If the given string ends with the given suffix, erases that suffix.
 * @return true iff the suffix was present (and erased)
 */
bool removeSuffix(std::string& str, std::string_view suffix);

std::vector<std::string> split(std::string_view haystack, char delimiter);
void split(std::vector<std::string>& out, std::string_view haystack, char delimiter);

std::vector<std::string_view> split_view(const std::string& haystack, char delimiter);
std::vector<std::string_view> split_view(const char* haystack, char delimiter);
std::vector<std::string_view> split_view(std::string_view haystack, char delimiter);
void split_view(std::vector<std::string_view>& out, const std::string& haystack, char delimiter);
void split_view(std::vector<std::string_view>& out, const char* haystack, char delimiter);
void split_view(std::vector<std::string_view>& out, std::string_view haystack, char delimiter);

//These overloads are deleted because they result in dangling references.
std::vector<std::string_view> split_view(const std::string&& temp, char delimiter) = delete;
void split_view(std::vector<std::string_view>& out, const std::string&& temp, char delimiter) = delete;


std::string join(const std::vector<std::string_view>& inputs, std::string_view delimiter);
std::string join(const std::vector<std::string>& inputs, std::string_view delimiter);


template<typename T>
T from_string(std::string_view view);
#define FROM_STRING_CASE(TYPE,SHORTHAND) extern template TYPE from_string(std::string_view view); TYPE SHORTHAND(std::string_view view);
		FROM_STRING_CASE(int,to_int)
		FROM_STRING_CASE(unsigned int,to_uint)
#undef FROM_STRING_CASE


class StringBuilder {
public:
	std::string data() const & {return data_;}
	std::string data() && {return std::move(data_);}
private:
	std::string data_;
	friend StringBuilder& operator<<(StringBuilder& out, std::string_view view);
};
StringBuilder& operator<<(StringBuilder& out, const char* string);
StringBuilder& operator<<(StringBuilder& out, unsigned int string);
StringBuilder& operator<<(StringBuilder& out, unsigned long string);

#endif /* STRINGUTILS_HPP */

