#include "precompiled.hpp"
#include "bitset.hpp"

#define BITSET_EXTERN_TEMPLATE /* not extern */
#include "bitset-instantiations.hpp"

namespace automaton {
namespace impl {

unsigned int ctz(unsigned int x) {
	if (!x)
		//this is what x86-64 tzcnt returns, so should help GCC fold it
		return std::numeric_limits<unsigned int>::digits;
	return __builtin_ctz(x);
}
unsigned int ctz(unsigned long x) {
	if (!x)
		//this is what x86-64 tzcnt returns, so should help GCC fold it
		return std::numeric_limits<unsigned long>::digits;
	return __builtin_ctzl(x);
}
unsigned int ctz(unsigned long long x) {
	if (!x)
		//this is what x86-64 tzcnt returns, so should help GCC fold it
		return std::numeric_limits<unsigned long long>::digits;
	return __builtin_ctzll(x);
}
unsigned int ctz(unsigned char x) {
	return ctz(static_cast<unsigned int>(x));
}
unsigned int ctz(unsigned short x) {
	return ctz(static_cast<unsigned int>(x));
}

unsigned int popcount(unsigned int x) {
	return __builtin_popcount(x);
}
unsigned int popcount(unsigned long x) {
	return __builtin_popcountl(x);
}
unsigned int popcount(unsigned long long x) {
	return __builtin_popcountll(x);
}
unsigned int popcount(unsigned char x) {
	return popcount(static_cast<unsigned int>(x));
}
unsigned int popcount(unsigned short x) {
	return popcount(static_cast<unsigned int>(x));
}

[[noreturn, gnu::cold]] void bitset_throw_out_of_range(unsigned int index, unsigned int size) {
	std::string msg = "bitset index out of range: index " + std::to_string(index) + ", size " + std::to_string(size);
	throw std::out_of_range(msg);
}

}//namespace impl
}//namespace automaton