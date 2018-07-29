/*
 * File:   maybe_owning_ptr.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on July 25, 2018, 5:08 PM
 */

#ifndef MAYBE_OWNING_PTR_HPP
#define MAYBE_OWNING_PTR_HPP

#include <utility>

template<class T>
class maybe_owning_ptr {
	T* p_;
	bool owning_;
public:
	using element_type = T;
	using pointer = T*;
	maybe_owning_ptr() : p_(nullptr), owning_(false) {}
	explicit maybe_owning_ptr(T* p, bool owning = false) : p_(p), owning_(owning) {}
	//TODO: could make copyable (producing a non-owning copy)
	maybe_owning_ptr(maybe_owning_ptr&& o) : p_(std::exchange(o.p_, nullptr)), owning_(std::exchange(o.owning_, false)) {}
	maybe_owning_ptr& operator=(maybe_owning_ptr&& o) {
		p_ = std::exchange(o.p_, nullptr);
		owning_ = std::exchange(o.owning_, false);
	}
	~maybe_owning_ptr() {
		if (owning_) delete p_; //deleting nullptr is safe
	}

	explicit operator bool() const {
		return p_;
	}
	//If *this is an rvalue and is owning, **this should also be an rvalue, but
	//we can't decide that at compile time.
	element_type& operator*() const {
		return *p_;
	}
	pointer operator->() const {
		return p_;
	}
	pointer get() const {
		return p_;
	}
};

#endif /* MAYBE_OWNING_PTR_HPP */

