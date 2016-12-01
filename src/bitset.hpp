/*
 * File:   bitset.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on November 2, 2016, 7:54 PM
 */

#ifndef BITSET_HPP
#define BITSET_HPP

#include <iostream>
#include <exception>
#include <cassert>
#include <boost/integer.hpp>

namespace automaton {
namespace impl {

template<typename storage_type, unsigned int N>
class bitset {
public:
	//I guess in some cases loops using std::size_t may be faster due to x86 quirks?
	using size_type = unsigned int;
	class reference {
	public:
		reference(const reference&) = default;

		operator bool() const {
			//select the overload returning bool to avoid infinite recursion
			return static_cast<const bitset&>(bitset_)[i_];
		}

		reference& operator=(bool value) {
			bitset_.set(i_, value);
			return *this;
		}
	private:
		reference() = delete;
		reference(bitset& bitset, size_type i) : bitset_(bitset), i_(i) {
			assert(i < bitset.size());
		}
		bitset& bitset_;
		size_type i_;
		friend class bitset;
	};

	bitset() : bits_(0) {}
	bitset(const bitset&) = default;

	size_type size() const {
		return N;
	}

	reference operator[](size_type pos) {
		assert(pos < size());
		return reference(*this, pos);
	}
	bool operator[](size_type pos) const {
		assert(pos < size());
		return bits_ & posmask(pos);
	}
	reference at(size_type pos) {
		if (pos < size())
			throw std::out_of_range("TODO: informative message");
		return *this[pos];
	}
	bool at(size_type pos) const {
		if (pos < size())
			throw std::out_of_range("TODO: informative message");
		return *this[pos];
	}

	size_type count() const {
		return __builtin_popcount(bits_);
	}
	bool any() const {
		return !none();
	}
	bool none() const {
		return bits_ == static_cast<storage_type>(0);
	}
	bool all() const {
		//(~bits & (N 1s)) == 0 may be faster
		return count() == size();
	}

	bool operator==(const bitset& other) const {
		return bits_ == other.bits_;
	}
	bool operator!=(const bitset& other) const {
		return !(*this == other);
	}

	bitset& set() {
		bits_ = lowmask(N);
		return *this;
	}
	bitset& set(size_type pos) {
		assert(pos < size());
//		bits_ = static_cast<storage_type>(bits_ | posmask(pos));
		do_or(posmask(pos));
		return *this;
	}
	bitset& set(size_type startInclusive, size_type endExclusive) {
		assert(startInclusive < N);
		assert(endExclusive <= N);
		assert(startInclusive <= endExclusive);
//		bits_ |= midmask(startInclusive, endExclusive);
		do_or(midmask(startInclusive, endExclusive));
		return *this;
	}
	bitset& reset() {
		bits_ = static_cast<storage_type>(0U);
		return *this;
	}
	bitset& reset(size_type pos) {
		assert(pos < size());
//		bits_ = static_cast<storage_type>(bits_ & ~posmask(pos));
		do_and(static_cast<storage_type>(~posmask(pos)));
		return *this;
	}
	bitset& reset(size_type startInclusive, size_type endExclusive) {
		assert(startInclusive < N);
		assert(endExclusive <= N);
		assert(startInclusive <= endExclusive);
//		bits_ = static_cast<storage_type>(bits_ & ~midmask(startInclusive, endExclusive));
		do_and(~midmask(startInclusive, endExclusive));
		return *this;
	}
	bitset& flip() {
//		bits_ ^= lowmask(N);
		do_xor(lowmask(N));
		return *this;
	}
	bitset& flip(size_type pos) {
		assert(pos < size());
//		bits_ ^= posmask(pos);
		do_xor(posmask(pos));
		return *this;
	}
	bitset& flip(size_type startInclusive, size_type endExclusive) {
		assert(startInclusive < N);
		assert(endExclusive <= N);
		assert(startInclusive <= endExclusive);
//		bits_ ^= midmask(startInclusive, endExclusive);
		do_xor(midmask(startInclusive, endExclusive));
		return *this;
	}
	bitset& set(bool value) {
		return value ? set() : reset();
	}
	bitset& set(size_type pos, bool value) {
		return value ? set(pos) : reset(pos);
	}
	bitset& set(size_type startInclusive, size_type endExclusive, bool value) {
		return value ? set(startInclusive, endExclusive) : reset(startInclusive, endExclusive);
	}

	bool test_set(size_type pos, bool value = true) {
		assert(pos < size());
		const reference& r = *this[pos];
		bool old = r;
		r = value;
		return old;
	}
	bool test_reset(size_type pos) {
		return test_set(pos, false);
	}

	bitset& operator&=(const bitset& other) {
//		bits_ &= other.bits_;
		do_and(other.bits_);
		return *this;
	}
	bitset& operator|=(const bitset& other) {
//		bits_ |= other.bits_;
		do_or(other.bits_);
		return *this;
	}
	bitset& operator^=(const bitset& other) {
//		bits_ ^= other.bits_;
		do_xor(other.bits_);
		return *this;
	}
	bitset operator~() const {
		return bitset(*this).flip();
	}
private:
	storage_type bits_;

	//centralize warning avoidance
	void do_and(storage_type x) {
		bits_ = static_cast<storage_type>(bits_ & x);
	}
	void do_or(storage_type x) {
		bits_ = static_cast<storage_type>(bits_ | x);
	}
	void do_xor(storage_type x) {
		bits_ = static_cast<storage_type>(bits_ ^ x);
	}

	static constexpr storage_type posmask(size_type pos) noexcept {
		assert(pos < N);
		return static_cast<storage_type>(1U << pos);
	}
	static constexpr storage_type lowmask(size_type n) noexcept {
		assert(n <= N);
		storage_type s = 0;
		while (n-- > 0)
			s = static_cast<storage_type>((s << 1U) | 1U);
		return s;
	}
	static constexpr storage_type midmask(size_type startInclusive, size_type endExclusive) noexcept {
		assert(startInclusive < N);
		assert(endExclusive <= N);
		assert(startInclusive <= endExclusive);
		return lowmask(endExclusive) & ~lowmask(startInclusive);
	}

	friend std::ostream& operator<<(std::ostream& o, const bitset& b) {
		for (size_type i = b.size(); i-- > 0;)
			o << (b[i] ? '1' : '0');
		return o;
	}
};

template<typename storage_type, unsigned int N>
auto operator&(const bitset<storage_type, N>& left, const bitset<storage_type, N>& right) {
	bitset<storage_type, N> ret(left);
	ret &= right;
	return ret;
}

template<typename storage_type, unsigned int N>
auto operator|(const bitset<storage_type, N>& left, const bitset<storage_type, N>& right) {
	bitset<storage_type, N> ret(left);
	ret |= right;
	return ret;
}

template<typename storage_type, unsigned int N>
auto operator^(const bitset<storage_type, N>& left, const bitset<storage_type, N>& right) {
	bitset<storage_type, N> ret(left);
	ret ^= right;
	return ret;
}

} //end namespace impl

template<unsigned int N>
using bitset = impl::bitset<typename boost::uint_t<N>::least, N>;

} //end namespace automaton

#endif /* BITSET_HPP */

