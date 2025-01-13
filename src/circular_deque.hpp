// SPDX-License-Identifier: MIT
// Copyright 2017 Massachusetts Institute of Technology
#ifndef CIRCULAR_DEQUE_HPP
#define CIRCULAR_DEQUE_HPP

#include <memory>
#include <cassert>
#include <algorithm>
#include <boost/range/join.hpp>
#ifndef __SANITIZE_ADDRESS__
#include <jemalloc/jemalloc.h>
#endif
#include "numutils.hpp"

template<typename T>
class circular_deque_base {
public:
	using size_type = std::uint32_t;
private:
	T* data_;
	size_type capacity_, head_, size_;
protected:
	std::aligned_storage_t<sizeof(T), alignof(T)> first_;
	//no data members past this point -- further small-buffer elements follow
	circular_deque_base(unsigned int capacity) noexcept;
	~circular_deque_base();

	bool small() const noexcept;
public:
	circular_deque_base& operator=(const circular_deque_base& other);
	circular_deque_base& operator=(circular_deque_base&& other);

	bool empty() const noexcept;
	size_type size() const noexcept;
	size_type capacity() const noexcept;

	auto begin() noexcept;
	auto begin() const noexcept;
	auto end() noexcept;
	auto end() const noexcept;

	size_type reserve(size_type minCapacity);

	T& front() noexcept;
	const T& front() const noexcept;
	T& back() noexcept;
	const T& back() const noexcept;

	void push_front(const T& e);
	void push_front(T&& e);
	template<typename... Args>
	void emplace_front(Args... args);
	T pop_front();

	void push_back(const T& e);
	void push_back(T&& e);
	template<typename... Args>
	void emplace_back(Args... args);
	T pop_back();

	void clear();
	//TODO: shrink_to_fit? we're deliberately letting jemalloc round up, but at
	//least becoming small again might be worthwhile even if we don't otherwise shrink.

	template<typename Iter>
	void append(Iter first, Iter last);
	//TODO: prepend (is to append what push_front is to push_back)

	void swap(circular_deque_base& other);

private:
	size_type mask(size_type i) const noexcept;
	size_type inc(size_type i) const noexcept;
	size_type dec(size_type i) const noexcept;

	//these methods are the prefix and suffix part of the *present* elements, not the unused space
	T* prefix_begin() noexcept;
	const T* prefix_begin() const noexcept;
	T* prefix_end() noexcept;
	const T* prefix_end() const noexcept;
	T* suffix_begin() noexcept;
	const T* suffix_begin() const noexcept;
	T* suffix_end() noexcept;
	const T* suffix_end() const noexcept;
	//contiguous() would have been a better name, I guess, though by one interpretation
	//all full circular_deques are contiguous, even the ones that wrap around
	bool linear() const noexcept;

	/**
	 * Grows this deque, ensuring space for at least one more element or for the
	 * given capacity.
	 */
	void ensureCapacity(size_type minCapacity = 0);
	bool full() const noexcept;
};

template<typename T, unsigned int N>
class circular_deque : public circular_deque_base<T> {
private:
	//inherit one from the base, store the rest
	std::aligned_storage_t<sizeof(T), alignof(T)> rest_[N-1];
public:
	circular_deque() : circular_deque_base<T>(N) {}
	~circular_deque() = default;
	circular_deque(const circular_deque& o);
	circular_deque(circular_deque&& o);
	circular_deque& operator=(const circular_deque& o);
	circular_deque& operator=(circular_deque&& o);

	circular_deque(const circular_deque_base<T>& o);
	circular_deque(circular_deque_base<T>&& o);
	circular_deque& operator=(const circular_deque_base<T>& o);
	circular_deque& operator=(circular_deque_base<T>&& o);
};

namespace std {
template<typename T>
void swap(circular_deque_base<T>& l, circular_deque_base<T>& r) {
	l.swap(r);
}
} //namespace std

#include "circular_deque.tcc"

#endif /* CIRCULAR_DEQUE_HPP */

