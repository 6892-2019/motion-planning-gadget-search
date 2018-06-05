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

//unsigned int if T is smaller, else T
template<typename Integral>
struct bitset_promote {
	static_assert(std::is_unsigned_v<Integral>, "");
	using type = std::conditional_t<
			(std::numeric_limits<Integral>::digits > std::numeric_limits<unsigned int>::digits),
			Integral,
			unsigned int>;
};
template<typename Integral>
using bitset_promote_t = typename bitset_promote<Integral>::type;

inline unsigned int ctz(unsigned int x) {
	if (!x)
		//this is what x86-64 tzcnt returns, so should help GCC fold it
		return std::numeric_limits<unsigned int>::digits;
	return __builtin_ctz(x);
}
inline unsigned int ctz(unsigned long x) {
	if (!x)
		//this is what x86-64 tzcnt returns, so should help GCC fold it
		return std::numeric_limits<unsigned long>::digits;
	return __builtin_ctzl(x);
}
inline unsigned int ctz(unsigned long long x) {
	if (!x)
		//this is what x86-64 tzcnt returns, so should help GCC fold it
		return std::numeric_limits<unsigned long long>::digits;
	return __builtin_ctzll(x);
}
inline unsigned int ctz(unsigned char x) {
	return ctz(static_cast<unsigned int>(x));
}
inline unsigned int ctz(unsigned short x) {
	return ctz(static_cast<unsigned int>(x));
}

inline unsigned int popcount(unsigned int x) {
	return __builtin_popcount(x);
}
inline unsigned int popcount(unsigned long x) {
	return __builtin_popcountl(x);
}
inline unsigned int popcount(unsigned long long x) {
	return __builtin_popcountll(x);
}
inline unsigned int popcount(unsigned char x) {
	return popcount(static_cast<unsigned int>(x));
}
inline unsigned int popcount(unsigned short x) {
	return popcount(static_cast<unsigned int>(x));
}



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
		return (*this)[pos];
	}
	bool at(size_type pos) const {
		if (pos < size())
			throw std::out_of_range("TODO: informative message");
		return (*this)[pos];
	}

	size_type count() const {
		return popcount(bits_);
	}
	bool any() const {
		return !none();
	}
	bool none() const {
		return bits_ == static_cast<storage_type>(0ULL);
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
		do_and_comp(midmask(startInclusive, endExclusive));
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
	//This overload is ambiguous with set(size_type) when calling set(int)
//	bitset& set(bool value) {
//		return value ? set() : reset();
//	}
	bitset& set(size_type pos, bool value) {
		return value ? set(pos) : reset(pos);
	}
	bitset& set(size_type startInclusive, size_type endExclusive, bool value) {
		return value ? set(startInclusive, endExclusive) : reset(startInclusive, endExclusive);
	}

	bool test_set(size_type pos, bool value = true) {
		assert(pos < size());
		bool old = (*this)[pos];
		(*this)[pos] = value;
		return old;
	}
	bool test_reset(size_type pos) {
		return test_set(pos, false);
	}

	//TODO: these methods have unclear preconditions and untested implementations
//	/**
//	 * Slides all bits towards greater indices by the given distance, starting
//	 * at the given position.  Bits that end up at indices >= N are lost.
//	 */
//	bitset& slide(size_type pos, int distance) {
//		return slide_range(pos, size(), distance);
//	}
//
//	/**
//	 * Slides bits in the given range towards greater indices by the given
//	 * distance, overwriting bits to the right of the range and discarding any
//	 * bits that end up at indices >= N.
//	 */
//	bitset& slide_range(size_type startInclusive, size_type endExclusive, int distance) {
//		storage_type source = midmask(startInclusive, endExclusive);
//		storage_type target = midmask(std::max(startInclusive + distance, 0u), std::min(endExclusive + distance, N));
//		if (distance >= 0)
//			bits_ = static_cast<storage_type>((bits_ & ~(source | target)) | ((bits_ & source) << distance));
//		else
//			bits_ = static_cast<storage_type>((bits_ & ~(source | target)) | ((bits_ & source) >> distance));
//		return *this;
//	}
//
//	/**
//	 * Rotates all bits in the bitset towards greater indices by the given
//	 * distance.
//	 */
//	bitset& rotate(int distance) {
//		return rotate_range(0, size(), distance);
//	}
//
//	/**
//	 * Rotates bits in the given range towards greater indices by the given
//	 * distance.  Bits outside the range are not changed.
//	 */
//	bitset& rotate_range(size_type startInclusive, size_type endExclusive, int distance) {
//		storage_type mask = midmask(startInclusive, endExclusive);
//		storage_type block = bits_ & mask;
//		do_and_comp(mask);
//		size_type split = (endExclusive - startInclusive) - std::abs(distance);
//		storage_type firstmask = midmask(startInclusive, startInclusive+split);
//		storage_type restmask = midmask(startInclusive+split, endExclusive);
//		if (distance >= 0)
//			bits_ = static_cast<storage_type>(bits_ | (((block & firstmask) << distance) & mask) | (((block & restmask) >> (distance - split)) & mask));
//		else
//			bits_ = static_cast<storage_type>(bits_ | (((block & firstmask) >> distance) & mask) | (((block & restmask) << (distance - split)) & mask));
//		return *this;
//	}

	/**
	 * @return the index of the first set bit in this bitset, or > size() if
	 * this set is empty
	 */
	size_type find_first() const {
		return ctz(bits_);
	}

	/**
	 * @return the index of the next set bit following the bit at index prev,
	 * or > size() if this set is empty
	 */
	size_type find_next(size_type prev) const {
		assert(prev < size());
//		bitset_promote_t<storage_type> q = bits_ & ~(prev == std::numeric_limits<storage_type>::digits ? ~storage_type(0u) : (storage_type(1u) << (prev+1)) - 1);
		return ctz(bitset_promote_t<storage_type>(bits_) >> (prev+1)) + prev + 1;
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
	bitset& operator>>=(int distance) {
		bits_ = static_cast<storage_type>(distance >= 0 ? bits_ >> distance : bits_ << -distance);
		return *this;
	}
	bitset& operator<<=(int distance) {
		bits_ = static_cast<storage_type>(distance >= 0 ? bits_ << distance : bits_ >> -distance);
		return *this;
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
	void do_and_comp(storage_type x) {
		bits_ = static_cast<storage_type>(bits_ & ~x);
	}

	static constexpr storage_type posmask(size_type pos) noexcept {
		assert(pos < N);
		return static_cast<storage_type>(storage_type(1u) << pos);
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
		return static_cast<storage_type>(lowmask(endExclusive) & ~lowmask(startInclusive));
	}

	friend std::ostream& operator<<(std::ostream& o, const bitset& b) {
		for (size_type i = b.size(); i-- > 0;)
			o << (b[i] ? '1' : '0');
		return o;
	}
	friend class std::hash<bitset<storage_type, N>>;
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

template<typename storage_type, unsigned int N>
auto operator>>(const bitset<storage_type, N>& left, int distance) {
	bitset<storage_type, N> ret(left);
	ret >>= distance;
	return ret;
}

template<typename storage_type, unsigned int N>
auto operator<<(const bitset<storage_type, N>& left, int distance) {
	bitset<storage_type, N> ret(left);
	ret <<= distance;
	return ret;
}

} //end namespace impl

template<unsigned int N>
using bitset = impl::bitset<typename boost::uint_t<N>::least, N>;

} //end namespace automaton

namespace std {
template<unsigned int N>
struct hash<automaton::bitset<N>> {
	size_t operator()(const automaton::bitset<N>& b) const {
		return b.bits_;
	}
};
}

#endif /* BITSET_HPP */

