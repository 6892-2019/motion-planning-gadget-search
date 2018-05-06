/*
 * File:   polyvariant.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on May 5, 2018, 7:35 PM
 */

#ifndef POLYVARIANT_HPP
#define POLYVARIANT_HPP

#include <type_traits>
#include <memory>
#include <utility>
#include <vta/algorithms.hpp>

/**
 * A variant holding instances of Base and providing a pointer-like interface to
 * call virtual methods on the contained derived class instance.
 */
template<class Base, class... Derived>
class polyvariant {
	static_assert((... && std::is_convertible<Derived*, Base*>::value), "bad derived class (pointer)");
	static_assert((... && std::is_convertible<Derived&, Base&>::value), "bad derived class (reference)");
	static_assert(vta::are_unique<Derived...>::value, "repeated derived class");
	typename std::aligned_union<1, Derived...>::type buf_;
public:
//	polyvariant() {
//		::new (&buf_)
//	}
//	template<class D, typename = std::enable_if_t<(... && std::is_same<D, Derived>::value)>>
//	polyvariant(const D& thing) {
//		::new (&buf_) D(thing);
//	}
	template<class Variant, class... Args, typename = std::enable_if_t<(... || std::is_same<Variant, Derived>::value)>>
	polyvariant(std::in_place_type_t<Variant>, Args&&... args) {
		::new (&buf_) Variant(std::forward<Args>(args)...);
	}
	~polyvariant() {
		std::destroy_at(std::addressof(**this));
	}

	//TODO: do I need ref-qualifiers to prevent handing out a ref to a temporary?
	Base& operator*() {
		return *this->operator->();
	}
	const Base& operator*() const {
		return *this->operator->();
	}
	Base* operator->() {
		return reinterpret_cast<Base*>(&buf_);
	}
	const Base* operator->() const {
		return reinterpret_cast<const Base*>(&buf_);
	}
};

#endif /* POLYVARIANT_HPP */

