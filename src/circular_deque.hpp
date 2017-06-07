/*
 * File:   circular_deque.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 25, 2017, 1:00 AM
 */

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
	circular_deque_base(unsigned int capacity) noexcept : data_(reinterpret_cast<T*>(&first_)),
			capacity_(capacity), head_(0), size_(0) {}
	~circular_deque_base() {
		std::destroy(prefix_begin(), prefix_end());
		std::destroy(suffix_begin(), suffix_end());
		if (!small())
#ifdef __SANITIZE_ADDRESS__
			free(data_);
#else
			sdallocx(data_, capacity_*sizeof(T), MALLOCX_ALIGN(alignof(T)));
#endif
	}

	bool small() const noexcept {
//		return data_ == reinterpret_cast<const T*>(&first_);
		return static_cast<const void*>(data_) == static_cast<const void*>(&first_);
	}
public:
	circular_deque_base& operator=(const circular_deque_base& other) {
		if (this == &other) return *this;

		if (size() >= other.size()) {
			//TODO: work out how the segments work out so we can use memcpy for
			//trivially-copyable types
			auto endpoint = std::copy(other.begin(), other.end(), begin());
			std::destroy(endpoint, end());
			size_ = other.size();
		} else if (other.size() <= capacity()) {
			std::copy(other.begin(), other.begin()+size(), begin());
			if (!linear()) //our free space is contiguous starting at suffix_end()
				std::uninitialized_copy(other.begin()+size(), other.end(), suffix_end());
			else {
				size_type beforeWrapping = capacity_ - (head_+size_);
				std::uninitialized_copy_n(other.begin()+size(), beforeWrapping, prefix_end());
				std::uninitialized_copy(other.begin()+size()+beforeWrapping, other.end(), suffix_begin());
			}
			size_ = other.size();
		} else {
			//We clear the elements we'd otherwise copy-assign into so we don't
			//copy them when growing.
			clear();
			ensureCapacity(other.size());
			append(other.prefix_begin(), other.prefix_end());
			append(other.suffix_begin(), other.suffix_end());
		}
		return *this;
	}
	circular_deque_base& operator=(circular_deque_base&& other) {
		if (this == &other) return *this;
		if (!other.small()) {
			clear();
			if (!small())
#ifdef __SANITIZE_ADDRESS__
				free(data_);
#else
				sdallocx(data_, capacity_*sizeof(T), MALLOCX_ALIGN(alignof(T)));
#endif
			data_ = other.data_;
			head_ = other.head_;
			size_ = other.size_;
			//reset other to its at-construction state
			capacity_ = other.capacity_;
			other.data_ = reinterpret_cast<T*>(&other.first_);
			other.head_ = other.size_ = 0;
			//We don't know what the other vector's N was, but it's at least 1, so be safe.
			other.capacity_ = 1;
		} else if (other.size() <= capacity()) {
			std::move(other.begin(), other.begin()+size(), begin());
			if (!linear()) //our free space is contiguous starting at suffix_end()
				std::uninitialized_move(other.begin()+size(), other.end(), suffix_end());
			else {
				size_type beforeWrapping = capacity_ - (head_+size_);
				std::uninitialized_move_n(other.begin()+size(), beforeWrapping, prefix_end());
				std::uninitialized_move(other.begin()+size()+beforeWrapping, other.end(), suffix_begin());
			}
			size_ = other.size();
		} else {
			//We clear the elements we'd otherwise move-assign into so we don't
			//copy them when growing.
			clear();
			ensureCapacity(other.size());
			append(std::make_move_iterator(other.prefix_begin()), std::make_move_iterator(other.prefix_end()));
			append(std::make_move_iterator(other.suffix_begin()), std::make_move_iterator(other.suffix_end()));
		}
		return *this;
	}

	bool empty() const noexcept {
		return !size_;
	}
	size_type size() const noexcept {
		return size_;
	}
	size_type capacity() const noexcept {
		return capacity_;
	}

	auto begin() noexcept {
		//TODO: if (linear()) return prefix_begin();
		//but that isn't the same type as below
		return boost::range::join(boost::make_iterator_range(prefix_begin(), prefix_end()),
				boost::make_iterator_range(suffix_begin(), suffix_end())).begin();
	}
	auto begin() const noexcept {
		//TODO: if (linear()) return prefix_begin();
		//but that isn't the same type as below
		return boost::range::join(boost::make_iterator_range(prefix_begin(), prefix_end()),
				boost::make_iterator_range(suffix_begin(), suffix_end())).begin();
	}
	auto end() noexcept {
		//TODO: if (linear()) return prefix_end();
		//but that isn't the same type as below
		return boost::range::join(boost::make_iterator_range(prefix_begin(), prefix_end()),
				boost::make_iterator_range(suffix_begin(), suffix_end())).end();
	}
	auto end() const noexcept {
		//TODO: if (linear()) return prefix_end();
		//but that isn't the same type as below
		return boost::range::join(boost::make_iterator_range(prefix_begin(), prefix_end()),
				boost::make_iterator_range(suffix_begin(), suffix_end())).end();
	}

	size_type reserve(size_type minCapacity) {
		if (minCapacity > capacity())
			ensureCapacity(minCapacity);
		return capacity();
	}

	T& front() noexcept {
		assert(!empty());
		return data_[head_];
	}
	const T& front() const noexcept {
		assert(!empty());
		return data_[head_];
	}
	T& back() noexcept {
		assert(!empty());
		return data_[mask(head_+size_-1)];
	}
	const T& back() const noexcept {
		assert(!empty());
		return data_[mask(head_+size_-1)];
	}

	void push_front(const T& e) {
		if (full()) ensureCapacity();
		head_ = dec(head_);
		::new (data_+head_) T(e);
		++size_;
	}
	void push_front(T&& e) {
		if (full()) ensureCapacity();
		head_ = dec(head_);
		::new (data_+head_) T(std::move(e));
		++size_;
	}
	template<typename... Args>
	void emplace_front(Args... args) {
		if (full()) ensureCapacity();
		head_ = dec(head_);
		::new (data_+head_) T(std::forward<Args>(args)...);
		++size_;
	}
	T pop_front() {
		assert(!empty());
		T ret = std::move(front());
		(data_+head_)->~T();
		head_ = inc(head_);
		--size_;
		return ret;
	}

	void push_back(const T& e) {
		if (full()) ensureCapacity();
		::new(data_ + mask(head_+size_)) T(e);
		++size_;
	}
	void push_back(T&& e) {
		if (full()) ensureCapacity();
		::new(data_ + mask(head_+size_)) T(std::move(e));
		++size_;
	}
	template<typename... Args>
	void emplace_back(Args... args) {
		if (full()) ensureCapacity();
		::new(data_ + mask(head_+size_)) T(std::forward<Args>(args)...);
		++size_;
	}
	T pop_back() {
		assert(!empty());
		T ret = std::move(back());
		--size_;
		(data_ + mask(head_+size_))->~T();
		return ret;
	}

	void clear() {
		std::destroy(prefix_begin(), prefix_end());
		std::destroy(suffix_begin(), suffix_end());
		size_ = 0;
		head_ = 0;
		//We don't shrink, so no change to data_ or capacity_.
	}
	//TODO: shrink_to_fit? we're deliberately letting jemalloc round up, but at
	//least becoming small again might be worthwhile even if we don't otherwise shrink.

	template<typename Iter>
	void append(Iter first, Iter last) {
		size_type incoming = numeric_cast<size_type>(std::distance(first, last));
		if (size() + incoming > capacity_)
			ensureCapacity(size() + incoming);

		if (linear()) {
			assert(capacity_ >= head_+size_);
			size_type remaining = capacity_ - (head_+size_);
			size_type segment = std::min(incoming, remaining);
			std::uninitialized_copy_n(first, segment, data_+(head_+size_));
			std::uninitialized_copy(first+segment, last, data_); //maybe wrap around
		} else
			std::uninitialized_copy(first, last, data_+mask(head_+size_));
		size_ += incoming;
	}
	//TODO: prepend (is to append what push_front is to push_back)

	void swap(circular_deque_base& other) {
		if (this == &other) return;
		using std::swap;
		if (!small() && !other.small()) {
			swap(data_, other.data_);
			swap(capacity_, other.capacity_);
			swap(head_, other.head_);
			swap(size_, other.size_);
		} else {
			if (other.size() > capacity())
				ensureCapacity(other.size());
			if (size() > other.capacity())
				other.ensureCapacity(size());

			size_type commonPrefix = std::min(size(), other.size());
			size_type p = head_, q = other.head_;
			for (size_type i = 0; i < commonPrefix; ++i, p = inc(p), q = other.inc(q))
				swap(data_[p], other.data_[q]);

			if (size() < other.size()) {
				size_type oldsize = size();
				if (other.data_+q < other.prefix_end()) {
					append(std::make_move_iterator(other.data_+q), std::make_move_iterator(other.prefix_end()));
					append(std::make_move_iterator(other.suffix_begin()), std::make_move_iterator(other.suffix_end()));
					std::destroy(other.data_+q, other.prefix_end());
					std::destroy(other.suffix_begin(), other.suffix_end());
				} else {
					append(std::make_move_iterator(other.data_+q), std::make_move_iterator(other.suffix_end()));
					std::destroy(other.data_+q, other.suffix_end());
				}
				//append adjusted our size already
				other.size_ = oldsize;
			} else if (size() > other.size()) {
				size_type oldsize = other.size();
				if (data_+p < prefix_end()) {
					other.append(std::make_move_iterator(data_+p), std::make_move_iterator(prefix_end()));
					other.append(std::make_move_iterator(suffix_begin()), std::make_move_iterator(suffix_end()));
					std::destroy(data_+p, prefix_end());
					std::destroy(suffix_begin(), suffix_end());
				} else {
					other.append(std::make_move_iterator(data_+p), std::make_move_iterator(suffix_end()));
					std::destroy(data_+p, suffix_end());
				}
				//other.append adjusted other.size
				size_ = oldsize;
			}
		}
	}

private:
	size_type mask(size_type i) const noexcept {
		assert(i < 2*capacity_);
		return i >= capacity_ ? i - capacity_ : i;
	}
	size_type inc(size_type i) const noexcept {
		assert(i < capacity_);
		return i+1 >= capacity_ ? 0 : i+1;
	}
	size_type dec(size_type i) const noexcept {
		assert(i < capacity_); //comparing vs 0 doesn't make sense for unsigned
		//deliberate underflow
		return i-1 >= capacity_ ? capacity_-1 : i-1;
	}

	//these methods are the prefix and suffix part of the *present* elements, not the unused space
	T* prefix_begin() noexcept {
		return data_ + head_;
	}
	const T* prefix_begin() const noexcept {
		return data_ + head_;
	}
	T* prefix_end() noexcept {
		return data_ + std::min(head_ + size_, capacity_);
	}
	const T* prefix_end() const noexcept {
		return data_ + std::min(head_ + size_, capacity_);
	}
	T* suffix_begin() noexcept {
		return data_;
	}
	const T* suffix_begin() const noexcept {
		return data_;
	}
	T* suffix_end() noexcept {
		//underflow-aware code
		return head_ + size_ > capacity_ ? data_ + head_ + size_ - capacity_ : data_;
	}
	const T* suffix_end() const noexcept {
		//underflow-aware code
		return head_ + size_ > capacity_ ? data_ + head_ + size_ - capacity_ : data_;
	}
	//contiguous() would have been a better name, I guess, though by one interpretation
	//all full circular_deques are contiguous, even the ones that wrap around
	bool linear() const noexcept {
		return suffix_begin() == suffix_end();
	}

	/**
	 * Grows this deque, ensuring space for at least one more element or for the
	 * given capacity.
	 */
	void ensureCapacity(size_type minCapacity = 0) {
		size_type targetCapacity = 2 * capacity_; //TODO: tune growth factor (see Folly's FBVector docs)
		targetCapacity = std::max(targetCapacity, minCapacity);
		std::size_t targetBytes = targetCapacity * sizeof(T);
#ifdef __SANITIZE_ADDRESS__
		std::size_t actualBytes = targetBytes;
		void* allocation = malloc(actualBytes);
#else
		std::size_t actualBytes = nallocx(targetBytes, MALLOCX_ALIGN(alignof(T)));
		void* allocation = mallocx(actualBytes, MALLOCX_ALIGN(alignof(T)));
#endif
		//we may lose some bytes due to truncation, but that's okay, sdallocx copes
		size_type actualCapacity = numeric_cast<size_type>(actualBytes / sizeof(T));

		T* newData = reinterpret_cast<T*>(allocation);
		T* p = std::uninitialized_move(prefix_begin(), prefix_end(), newData);
		p = std::uninitialized_move(suffix_begin(), suffix_end(), p);
		std::destroy(prefix_begin(), prefix_end());
		std::destroy(suffix_begin(), suffix_end());
		if (!small())
#ifdef __SANITIZE_ADDRESS__
			free(data_);
#else
			//not sure if I'm supposed to pass flags here...
			sdallocx(data_, capacity_*sizeof(T), MALLOCX_ALIGN(alignof(T)));
#endif

		data_ = newData;
		capacity_ = actualCapacity;
		head_ = 0;
		size_ = numeric_cast<size_type>(p - newData);
	}
	bool full() const noexcept {
		return size_ == capacity_;
	}
};

template<typename T, unsigned int N>
class circular_deque : public circular_deque_base<T> {
private:
	//inherit one from the base, store the rest
	std::aligned_storage_t<sizeof(T), alignof(T)> rest_[N-1];
public:
	circular_deque() : circular_deque_base<T>(N) {}
	~circular_deque() = default;
	circular_deque(const circular_deque& o) : circular_deque_base<T>(N) {
		if (!o.empty())
			circular_deque_base<T>::operator=(o);
	}
	circular_deque(circular_deque&& o) : circular_deque_base<T>(N) {
		if (!o.empty())
			circular_deque_base<T>::operator=(std::move(o));
	}
	circular_deque& operator=(const circular_deque& o) {
		circular_deque_base<T>::operator=(o);
		return *this;
	}
	circular_deque& operator=(circular_deque&& o) {
		circular_deque_base<T>::operator=(std::move(o));
		return *this;
	}

	circular_deque(const circular_deque_base<T>& o) : circular_deque_base<T>(N) {
		if (!o.empty())
			circular_deque_base<T>::operator=(o);
	}
	circular_deque(circular_deque_base<T>&& o) : circular_deque_base<T>(N) {
		if (!o.empty())
			circular_deque_base<T>::operator=(std::move(o));
	}
	circular_deque& operator=(const circular_deque_base<T>& o) {
		circular_deque_base<T>::operator=(o);
		return *this;
	}
	circular_deque& operator=(circular_deque_base<T>&& o) {
		circular_deque_base<T>::operator=(std::move(o));
		return *this;
	}
};

namespace std {
template<typename T>
void swap(circular_deque_base<T>& l, circular_deque_base<T>& r) {
	l.swap(r);
}
} //namespace std

#endif /* CIRCULAR_DEQUE_HPP */

