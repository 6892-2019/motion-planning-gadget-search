/*
 * File:   bitset.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on November 2, 2016, 7:54 PM
 */

#ifndef BITSET_HPP
#define BITSET_HPP

#include <iostream>
#include <cassert>
#include <bit>
#include <boost/integer.hpp>
#include <farmhash/farmhash.h>

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

[[noreturn, gnu::cold]] void bitset_throw_out_of_range(unsigned int index, unsigned int size);



template<typename storage_type, unsigned int N>
class bitset {
public:
	//I guess in some cases loops using std::size_t may be faster due to x86 quirks?
	using size_type = unsigned int;
	class reference {
	public:
		reference(const reference&) = default;
		operator bool() const;
		reference& operator=(bool value);
	private:
		reference() = delete;
		reference(bitset& bitset, size_type i);
		bitset& bitset_;
		size_type i_;
		friend class bitset;
	};

	bitset();
	bitset(const bitset&) = default;

	//We deliberately do not define size() because it is ambiguous between
	//capacity() (when thinking of the bitset as a group of bits) and count()
	//(when thinking of the bitset as a container of small integers).
	size_type capacity() const;

	reference operator[](size_type pos);
	bool operator[](size_type pos) const;
	reference at(size_type pos);
	bool at(size_type pos) const;

	size_type count() const;
	bool any() const;
	bool none() const;
	bool all() const;

	bool operator==(const bitset& other) const;

	bitset& set();
	bitset& set(size_type pos);
	bitset& set(size_type startInclusive, size_type endExclusive);
	bitset& reset();
	bitset& reset(size_type pos);
	bitset& reset(size_type startInclusive, size_type endExclusive);
	bitset& flip();
	bitset& flip(size_type pos);
	bitset& flip(size_type startInclusive, size_type endExclusive);
	//This overload is ambiguous with set(size_type) when calling set(int)
//	bitset& set(bool value);
	bitset& set(size_type pos, bool value);
	bitset& set(size_type startInclusive, size_type endExclusive, bool value);

	bool test_set(size_type pos, bool value = true);
	bool test_reset(size_type pos);

	/**
	 * @return the index of the first set bit in this bitset, or > size() if
	 * this set is empty
	 */
	size_type find_first() const;
	/**
	 * @return the index of the next set bit following the bit at index prev,
	 * or > size() if this set is empty
	 */
	size_type find_next(size_type prev) const;

	bitset& operator&=(const bitset& other);
	bitset& operator|=(const bitset& other);
	bitset& operator^=(const bitset& other);
	bitset operator~() const;
	bitset& operator>>=(int distance);
	bitset& operator<<=(int distance);
private:
	storage_type bits_;

	//centralize warning avoidance
	void do_and(storage_type x);
	void do_or(storage_type x);
	void do_xor(storage_type x);
	void do_and_comp(storage_type x);

	static constexpr storage_type posmask(size_type pos) noexcept;
	static constexpr storage_type lowmask(size_type n) noexcept;
	static constexpr storage_type midmask(size_type startInclusive, size_type endExclusive) noexcept;

	friend class std::hash<bitset<storage_type, N>>;
};

template<typename storage_type, unsigned int N>
bitset<storage_type, N>::reference::reference(bitset& bitset, size_type i) : bitset_(bitset), i_(i) {
	assert(i < bitset.capacity());
}
template<typename storage_type, unsigned int N>
bitset<storage_type, N>::reference::operator bool() const {
	//select the overload returning bool to avoid infinite recursion
	return static_cast<const bitset&>(bitset_)[i_];
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::reference::operator=(bool value) -> reference& {
	bitset_.set(i_, value);
	return *this;
}

template<typename storage_type, unsigned int N>
bitset<storage_type, N>::bitset() : bits_(0) {}
//Outlining this constructor increases the size of the final executables by a lot.
//template<typename storage_type, unsigned int N>
//bitset<storage_type, N>::bitset(const bitset&) = default;

template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::capacity() const -> size_type {
	return N;
}

template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::operator[](size_type pos) -> reference {
	assert(pos < capacity());
	return reference(*this, pos);
}
template<typename storage_type, unsigned int N>
bool bitset<storage_type, N>::operator[](size_type pos) const {
	assert(pos < capacity());
	return bits_ & posmask(pos);
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::at(size_type pos) -> reference {
	if (pos < capacity()) bitset_throw_out_of_range(pos, capacity());
	return (*this)[pos];
}
template<typename storage_type, unsigned int N>
bool bitset<storage_type, N>::at(size_type pos) const {
	if (pos < capacity()) bitset_throw_out_of_range(pos, capacity());
	return (*this)[pos];
}

template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::count() const -> size_type {
	return std::popcount(bits_);
}
template<typename storage_type, unsigned int N>
bool bitset<storage_type, N>::any() const {
	return !none();
}
template<typename storage_type, unsigned int N>
bool bitset<storage_type, N>::none() const {
	return bits_ == static_cast<storage_type>(0ULL);
}
template<typename storage_type, unsigned int N>
bool bitset<storage_type, N>::all() const {
	//(~bits & (N 1s)) == 0 may be faster
	return count() == capacity();
}

template<typename storage_type, unsigned int N>
bool bitset<storage_type, N>::operator==(const bitset& other) const {
	return bits_ == other.bits_;
}

template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::set() -> bitset& {
	bits_ = lowmask(N);
	return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::set(size_type pos) -> bitset& {
	assert(pos < capacity());
	do_or(posmask(pos));
	return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::set(size_type startInclusive, size_type endExclusive) -> bitset& {
	assert(startInclusive < N);
	assert(endExclusive <= N);
	assert(startInclusive <= endExclusive);
	do_or(midmask(startInclusive, endExclusive));
	return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::reset() -> bitset& {
	bits_ = static_cast<storage_type>(0U);
	return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::reset(size_type pos) -> bitset& {
	assert(pos < capacity());
	do_and(static_cast<storage_type>(~posmask(pos)));
	return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::reset(size_type startInclusive, size_type endExclusive) -> bitset& {
	assert(startInclusive < N);
	assert(endExclusive <= N);
	assert(startInclusive <= endExclusive);
	do_and_comp(midmask(startInclusive, endExclusive));
	return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::flip() -> bitset& {
	do_xor(lowmask(N));
	return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::flip(size_type pos) -> bitset& {
	assert(pos < capacity());
	do_xor(posmask(pos));
	return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::flip(size_type startInclusive, size_type endExclusive) -> bitset& {
	assert(startInclusive < N);
	assert(endExclusive <= N);
	assert(startInclusive <= endExclusive);
	do_xor(midmask(startInclusive, endExclusive));
	return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::set(size_type pos, bool value) -> bitset& {
	return value ? set(pos) : reset(pos);
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::set(size_type startInclusive, size_type endExclusive, bool value) -> bitset& {
	return value ? set(startInclusive, endExclusive) : reset(startInclusive, endExclusive);
}

template<typename storage_type, unsigned int N>
bool bitset<storage_type, N>::test_set(size_type pos, bool value) {
	assert(pos < capacity());
	bool old = (*this)[pos];
	(*this)[pos] = value;
	return old;
}
template<typename storage_type, unsigned int N>
bool bitset<storage_type, N>::test_reset(size_type pos) {
	return test_set(pos, false);
}

template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::find_first() const -> size_type {
   return std::countr_zero(bits_);
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::find_next(size_type prev) const -> size_type {
   assert(prev < capacity());
   //shifting by prev+1 all at once might be undefined behavior
   return std::countr_zero((bitset_promote_t<storage_type>(bits_) >> (prev)) >> 1) + prev + 1;
}

template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::operator&=(const bitset& other) -> bitset& {
   do_and(other.bits_);
   return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::operator|=(const bitset& other) -> bitset& {
   do_or(other.bits_);
   return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::operator^=(const bitset& other) -> bitset& {
   do_xor(other.bits_);
   return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::operator~() const -> bitset {
   return bitset(*this).flip();
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::operator>>=(int distance) -> bitset& {
   bits_ = static_cast<storage_type>(distance >= 0 ? bits_ >> distance : bits_ << -distance);
   return *this;
}
template<typename storage_type, unsigned int N>
auto bitset<storage_type, N>::operator<<=(int distance) -> bitset& {
   bits_ = static_cast<storage_type>(distance >= 0 ? bits_ << distance : bits_ >> -distance);
   return *this;
}

template<typename storage_type, unsigned int N>
void bitset<storage_type, N>::do_and(storage_type x) {
	bits_ = static_cast<storage_type>(bits_ & x);
}
template<typename storage_type, unsigned int N>
void bitset<storage_type, N>::do_or(storage_type x) {
	bits_ = static_cast<storage_type>(bits_ | x);
}
template<typename storage_type, unsigned int N>
void bitset<storage_type, N>::do_xor(storage_type x) {
	bits_ = static_cast<storage_type>(bits_ ^ x);
}
template<typename storage_type, unsigned int N>
void bitset<storage_type, N>::do_and_comp(storage_type x) {
	bits_ = static_cast<storage_type>(bits_ & ~x);
}

template<typename storage_type, unsigned int N>
constexpr storage_type bitset<storage_type, N>::posmask(size_type pos) noexcept {
	assert(pos < N);
	return static_cast<storage_type>(storage_type(1u) << pos);
}
template<typename storage_type, unsigned int N>
constexpr storage_type bitset<storage_type, N>::lowmask(size_type n) noexcept {
	assert(n <= N);
	storage_type s = 0;
	while (n-- > 0)
		s = static_cast<storage_type>((s << 1U) | 1U);
	return s;
}
template<typename storage_type, unsigned int N>
constexpr storage_type bitset<storage_type, N>::midmask(size_type startInclusive, size_type endExclusive) noexcept {
	assert(startInclusive < N);
	assert(endExclusive <= N);
	assert(startInclusive <= endExclusive);
	return static_cast<storage_type>(lowmask(endExclusive) & ~lowmask(startInclusive));
}

template<typename storage_type, unsigned int N>
bool operator!=(const bitset<storage_type, N>& left, const bitset<storage_type, N>& right) {
	return !(left == right);
}

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

template<typename storage_type, unsigned int N>
std::ostream& operator<<(std::ostream& o, const bitset<storage_type, N>& b) {
	for (auto i = b.capacity(); i-- > 0;)
		o << (b[i] ? '1' : '0');
	return o;
}

} //end namespace impl

template<unsigned int N>
using bitset = impl::bitset<typename boost::uint_t<N>::least, N>;

} //end namespace automaton

namespace std {
template<unsigned int N>
struct hash<automaton::bitset<N>> {
	size_t operator()(const automaton::bitset<N>& b) const {
		return farmhash::Fingerprint(b.bits_);
	}
};
}

#define BITSET_EXTERN_TEMPLATE extern
#include "bitset-instantiations.hpp"

#endif /* BITSET_HPP */

