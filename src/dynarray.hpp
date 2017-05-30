/*
 * File:   dynarray.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on November 18, 2016, 6:46 PM
 */

#ifndef DYNARRAY_HPP
#define DYNARRAY_HPP

#include <memory>
#include <iterator>
#include <cassert>

/**
 * An array of fixed, dynamic size.  All elements are default-initialized upon
 * construction, as with a built-in array; elements of built-in type are thus
 * left uninitialized.
 */
template<class T>
class dynarray {
public:
	using value_type = T;
	using reference = value_type&;
	using const_reference = const value_type&;
	using iterator = T*;
	using const_iterator = const T*;
	using reverse_iterator = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

	dynarray() : data_(nullptr), size_(0) {}
	explicit dynarray(size_type size) : data_(new T[size]), size_(size) {}

	size_type size() const {
		return size_;
	}

	iterator begin() {
		return data_.get();
	}
	const_iterator begin() const {
		return data_.get();
	}
	iterator end() {
		return data_.get() + size();
	}
	const_iterator end() const {
		return data_.get() + size();
	}
	const_iterator cbegin() const {
		return begin();
	}
	const_iterator cend() const {
		return end();
	}
	reverse_iterator rbegin() {
		return reverse_iterator(end());
	}
	const_reverse_iterator rbegin() const {
		return const_reverse_iterator(end());
	}
	reverse_iterator rend() {
		return reverse_iterator(begin());
	}
	const_reverse_iterator rend() const {
		return const_reverse_iterator(begin());
	}
	const_reverse_iterator crbegin() const {
		return rbegin();
	}
	const_reverse_iterator crend() const {
		return rend();
	}

	reference operator[](size_type i) {
		assert(i < size());
		return data_[i];
	}
	const_reference operator[](size_type i) const {
		assert(i < size());
		return data_[i];
	}

	/**
	 * Clears this dynarray and releases memory.  Because dynarray does not
	 * support insertion, no further operations can be performed on it.
	 */
	void clear() {
		data_.reset(nullptr);
		size_ = 0;
	}

	void swap(dynarray& other) noexcept {
		data_.swap(other.data_);
		std::swap(size_, other.size_);
	}
private:
	std::unique_ptr<value_type[]> data_;
	size_type size_;
};

namespace std {
template<typename T>
void swap(dynarray<T>& left, dynarray<T>& right) noexcept {
	left.swap(right);
}
}

#endif /* DYNARRAY_HPP */

