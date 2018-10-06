/*
 * File:   n_copies_iterator.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on October 5, 2018, 10:52 PM
 */

#ifndef AUTOMATON_N_COPIES_ITERATOR_HPP_INCLUDED
#define AUTOMATON_N_COPIES_ITERATOR_HPP_INCLUDED

#include <iterator>
#include <cassert>

#ifndef NDEBUG
#define COMMA_BOUND_FIELD , bound_
#define COMMA_BOUND_ARG , size_type bound
#define COMMA_BOUND_MEMINIT_ZERO , bound_(0)
#define COMMA_BOUND_MEMINIT_ARG , bound_(bound)
#define COMMA_COPIES_FIELD , copies_
#else
#define COMMA_BOUND_FIELD
#define COMMA_BOUND_ARG
#define COMMA_BOUND_MEMINIT_ZERO
#define COMMA_BOUND_MEMINIT_ARG
#define COMMA_COPIES_FIELD
#endif

template<typename T>
class n_copies_range;

template<typename T>
class n_copies_iterator {
public:
	using size_type = unsigned int; //not a standard typedef
	using difference_type = int;
	using value_type = T;
	using pointer = const T*;
	using reference = const T&;
	using iterator_category = std::random_access_iterator_tag;

	n_copies_iterator() : thing_(nullptr), pos_(0) COMMA_BOUND_MEMINIT_ZERO {}
	//default copy, move, assignment
	reference operator*() const {
		check_nonsingular();
		check_dereferenceable();
		return *thing_;
	}
	pointer operator->() const {
		check_nonsingular();
		check_dereferenceable();
		return thing_;
	}
	reference operator[](size_type idx) const {
		check_nonsingular();
		check_offset_dereferenceable(idx);
		return *thing_;
	}
	reference operator[](difference_type idx) const {
		check_nonsingular();
		check_offset_dereferenceable(idx);
		return *thing_;
	}
	n_copies_iterator& operator++() {
		check_nonsingular();
		check_offset_inbounds(1);
		++pos_;
		return *this;
	}
	n_copies_iterator operator++(int) {
		auto copy = *this;
		operator++();
		return copy;
	}
	n_copies_iterator& operator--() {
		check_nonsingular();
		check_offset_inbounds(-1);
		--pos_;
		return *this;
	}
	n_copies_iterator operator--(int) {
		auto copy = *this;
		operator--();
		return copy;
	}
	n_copies_iterator& operator+=(size_type dist) {
		check_nonsingular();
		check_offset_inbounds(dist);
		pos_ += dist;
		return *this;
	}
	n_copies_iterator& operator+=(difference_type dist) {
		check_nonsingular();
		check_offset_inbounds(dist);
		//Get the correct behavior when dist is negative.
		difference_type pos = static_cast<difference_type>(pos_);
		pos += dist;
		pos_ = static_cast<size_type>(pos);
		return *this;
	}
	n_copies_iterator& operator-=(size_type dist) {
		check_nonsingular();
		check_offset_inbounds(dist);
		pos_ -= dist;
		return *this;
	}
	n_copies_iterator& operator-=(difference_type dist) {
		check_nonsingular();
		check_offset_inbounds(dist);
		//Get the correct behavior when dist is negative.
		difference_type pos = static_cast<difference_type>(pos_);
		pos -= dist;
		pos_ = static_cast<size_type>(pos);
		return *this;
	}
private:
	const T* thing_;
	size_type pos_ COMMA_BOUND_FIELD;

	friend class n_copies_range<T>;
	n_copies_iterator(const T* thing, size_type pos COMMA_BOUND_ARG) : thing_(thing), pos_(pos) COMMA_BOUND_MEMINIT_ARG {}

	//Moving these out-of-line (as template functions) would require forward-
	//declaring them because a friend declaration is assumed to name a
	//non-template function.
	friend bool operator==(const n_copies_iterator& left, const n_copies_iterator& right) {
		check_comparable(left, right);
		return left.pos_ == right.pos_;
	}
	friend bool operator!=(const n_copies_iterator& left, const n_copies_iterator& right) {
		check_comparable(left, right);
		return left.pos_ != right.pos_;
	}
	friend bool operator<(const n_copies_iterator& left, const n_copies_iterator& right) {
		check_comparable(left, right);
		return left.pos_ < right.pos_;
	}
	friend bool operator>(const n_copies_iterator& left, const n_copies_iterator& right) {
		check_comparable(left, right);
		return left.pos_ > right.pos_;
	}
	friend bool operator<=(const n_copies_iterator& left, const n_copies_iterator& right) {
		check_comparable(left, right);
		return left.pos_ <= right.pos_;
	}
	friend bool operator>=(const n_copies_iterator& left, const n_copies_iterator& right) {
		check_comparable(left, right);
		return left.pos_ >= right.pos_;
	}

	void check_nonsingular() const {
		assert(thing_);
	}
	static void check_comparable(const n_copies_iterator& left, const n_copies_iterator& right) {
		//singular iterators compare equal to other singular iterators only (C++14)
		//so no special handling is required
		assert(left.thing_ == right.thing_);
		//If NDEBUG is defined, assert ignores its args so no special bound_
		//handling is required here.
		assert(left.bound_ == right.bound_);
	}
	void check_dereferenceable() const {
		assert(pos_ < bound_);
	}

	//Because we only bounds-check in debug builds, instead of writing
	//overflow-aware bounds checks, we'll just promote to a larger signed type.
	using bounds_check_type = std::int64_t;
	void check_offset_dereferenceable(bounds_check_type offset) const {
		bounds_check_type actual = pos_ + offset;
		assert(0 <= actual && actual < bound_);
	}
	void check_offset_inbounds(bounds_check_type offset) const {
		bounds_check_type actual = pos_ + offset;
		assert(0 <= actual && actual <= bound_);
	}
};

template<typename T>
auto operator+(n_copies_iterator<T> i, typename n_copies_iterator<T>::size_type dist) {
	i += dist;
	return i;
}
template<typename T>
auto operator+(typename n_copies_iterator<T>::size_type dist, const n_copies_iterator<T> i) {
	i += dist;
	return i;
}
template<typename T>
auto operator+(n_copies_iterator<T> i, typename n_copies_iterator<T>::difference_type dist) {
	i += dist;
	return i;
}
template<typename T>
auto operator+(typename n_copies_iterator<T>::difference_type dist, const n_copies_iterator<T> i) {
	i += dist;
	return i;
}
template<typename T>
auto operator-(n_copies_iterator<T> i, typename n_copies_iterator<T>::size_type dist) {
	i -= dist;
	return i;
}
template<typename T>
auto operator-(typename n_copies_iterator<T>::size_type dist, const n_copies_iterator<T> i) {
	i -= dist;
	return i;
}
template<typename T>
auto operator-(n_copies_iterator<T> i, typename n_copies_iterator<T>::difference_type dist) {
	i -= dist;
	return i;
}
template<typename T>
auto operator-(typename n_copies_iterator<T>::difference_type dist, const n_copies_iterator<T> i) {
	i -= dist;
	return i;
}



template<typename T>
class n_copies_range {
public:
	using size_type = typename n_copies_iterator<T>::size_type;
	n_copies_range(const T& thing, size_type copies) : thing_(&thing), copies_(copies) {}
	n_copies_range(T&& thing, size_type copies) = delete; //thing_ would dangle
	auto begin() const {
		return n_copies_iterator<T>(thing_, 0 COMMA_COPIES_FIELD);
	}
	auto end() const {
		return n_copies_iterator<T>(thing_, copies_ COMMA_COPIES_FIELD);
	}
private:
	const T* thing_;
	size_type copies_;
};

#endif /* AUTOMATON_N_COPIES_ITERATOR_HPP_INCLUDED */