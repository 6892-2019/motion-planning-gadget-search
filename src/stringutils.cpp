#include "precompiled.hpp"
#include "stringutils.hpp"

std::vector<std::string> split(std::string_view haystack, char delimiter) {
	std::vector<std::string> result;
	split(result, haystack, delimiter);
	return result;
}
void split(std::vector<std::string>& out, std::string_view haystack, char delimiter) {
	//TODO: we'd prefer not to hit the heap here, but we'd also prefer not to
	//duplicate the actual splitting code (by using a template).
	std::vector<std::string_view> views = split_view(haystack, delimiter);
	out.clear();
	for (std::string_view view : views)
		out.push_back(std::string(view));
}

std::vector<std::string_view> split_view(const std::string& haystack, char delimiter) {
	return split_view(std::string_view(haystack), delimiter);
}
std::vector<std::string_view> split_view(const char* haystack, char delimiter) {
	return split_view(std::string_view(haystack), delimiter);
}
std::vector<std::string_view> split_view(std::string_view haystack, char delimiter) {
	std::vector<std::string_view> result;
	split_view(result, haystack, delimiter);
	return result;
}
void split_view(std::vector<std::string_view>& out, const std::string& haystack, char delimiter) {
	return split_view(out, std::string_view(haystack), delimiter);
}
void split_view(std::vector<std::string_view>& out, const char* haystack, char delimiter) {
	return split_view(out, std::string_view(haystack), delimiter);
}
void split_view(std::vector<std::string_view>& out, std::string_view haystack, char delimiter) {
	out.clear();
	using index = std::string_view::size_type;
	index start = 0, scan = 0;
	while (scan < haystack.length()) {
		if (haystack[scan] == delimiter) {
			out.push_back(haystack.substr(start, scan-start));
			start = scan+1; //skip the delimiter
			scan = start;
		} else
			++scan;
	}
	out.push_back(haystack.substr(start, scan-start));
}


std::string join(const std::vector<std::string_view>& inputs, std::string_view delimiter) {
	std::string result;
	std::string_view::size_type total_size = 0;
	for (auto v : inputs)
		total_size += v.size();
	result.reserve(total_size);

	auto i = inputs.begin(), end = inputs.end();
	if (i != end) {
		result += *i++;
		while (i != end) {
			result += delimiter;
			result += *i++;
		}
	}
	return result;
}
std::string join(const std::vector<std::string>& inputs, std::string_view delimiter) {
	//TODO: again, we'd like to not hit the heap here.
	std::vector<std::string_view> views;
	for (const auto& s : inputs)
		views.push_back(s);
	return join(views, delimiter);
}


template<typename T>
T from_string(std::string_view view) {
	T value;
	if (auto [ptr, ec] = std::from_chars(view.begin(), view.end(), value, 10); ec != std::errc() || ptr != view.end()) {
		//TODO: extract slowpath into [[gnu::cold]] function
		throw std::logic_error("from_string problem"); //TODO: appropriate exception and messages
	}
	return value;
}

#define FROM_STRING_CASE(TYPE,SHORTHAND) template TYPE from_string(std::string_view view); \
	TYPE SHORTHAND(std::string_view view) { \
	return from_string<TYPE>(view); \
	}
		FROM_STRING_CASE(int,to_int)
		FROM_STRING_CASE(unsigned int,to_uint)
#undef FROM_STRING_CASE

StringBuilder& operator<<(StringBuilder& out, std::string_view view) {
	out.data_.append(view.begin(), view.size());
	return out;
}
StringBuilder& operator<<(StringBuilder& out, const char* string) {
	return out << std::string_view(string);
}
StringBuilder& operator<<(StringBuilder& out, unsigned int string) {
	return out << std::to_string(string);
}
StringBuilder& operator<<(StringBuilder& out, unsigned long string) {
	return out << std::to_string(string);
}