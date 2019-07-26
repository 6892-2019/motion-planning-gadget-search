#ifndef PROJ_COMPARE_CPP
#define PROJ_COMPARE_CPP

#include <tuple>

template<int... I>
struct proj_less {
	//No point in noexcept here because tuple::operator< is not (conditionally) noexcept.
	template<typename T>
	bool operator()(const T& l, const T& r) const {
		using std::get;
		if constexpr(sizeof...(I) == 1)
			return get<I...>(l) < get<I...>(r);
		else
			return std::tie(get<I>(l)...) < std::tie(get<I>(r)...);
	}
};

template<int... I>
struct proj_equal {
	template<typename T>
	bool operator()(const T& l, const T& r) const {
		using std::get;
		if constexpr(sizeof...(I) == 1)
			return get<I...>(l) == get<I...>(r);
		else
			return std::tie(get<I>(l)...) == std::tie(get<I>(r)...);
	}
};

template<int I>
struct coord_less_left {
	template<typename T, typename U>
	bool operator()(const T& l, const U& r) const {
		using std::get;
		return get<I>(l) < r;
	}
};

template<int I>
struct coord_less_right {
	template<typename T, typename U>
	bool operator()(const U& l, const T& r) const {
		using std::get;
		return l < get<I>(r);
	}
};

//We could add variadic proj_compare_left/right that expects U to be an appropriate tuple.

#endif /* COMPARE_PROJ_HPP */
