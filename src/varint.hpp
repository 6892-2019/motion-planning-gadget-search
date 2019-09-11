#ifndef VARINT_HPP
#define VARINT_HPP

#include <cstddef>
#include <cstdint>

namespace upv {
/* Functions for unary-prefix varints, which are like the traditional
 * continuation-bit-based vbyte, except packing all of the continuation bits
 * into the first byte.  The advantage of this format is that fewer branches are
 * needed to decode it and they should consume fewer branch prediction
 * resources.  For example:
 *
 * 0xxxxxxx
 * 10xxxxxx xxxxxxxx
 * 110xxxxx xxxxxxxx xxxxxxxx
 *
 * ...and so on, up to 11111110 as the first byte, after which seven data bytes
 * follow.  To allow encoding 64 bits in nine bytes, we allow a first byte of
 * 11111111 with eight following bytes.  (Note that this forecloses the
 * possibility of encoding 128-bit quantities using an additional byte of
 * continuation bits.)
 *
 * The first byte contains the high bits of the encoded integer, but the
 * trailing bytes are little-endian.  That is, only one shift-or is required
 * regardless of the length of the value.  No bias is applied, so "overlong"
 * encodings are possible, which these functions read but don't write (Postel).
 */

//The current read and write implementations may be too clever in using
//variable-length memcpy.  A switch with separate cases for each length might
//perform better.

constexpr inline std::byte prefixes[] = {
	std::byte{0b00000000},
	std::byte{0b10000000},
	std::byte{0b11000000},
	std::byte{0b11100000},
	std::byte{0b11110000},
	std::byte{0b11111000},
	std::byte{0b11111100},
	std::byte{0b11111110},
};
inline std::byte* write(std::byte*& dest, std::uint64_t value) {
	//There's no clean way to avoid treating the 8-byte case specially, but it
	//should be rare/predictable.  (And arguably we should be switch-casing
	//every case anyway.)
	if (value < (1ul << 56)) {
		//We still need a bit to encode zero.  This is annoying because lzcnt would
		//give 64, which is 0 after subtraction, so this actually is a branch.
		int sigbits = value ? 64 - __builtin_clzl(value) : 1;
		unsigned int length = static_cast<unsigned int>((sigbits + 6)/7);
		std::byte high_byte;
		std::memcpy(&high_byte, reinterpret_cast<char*>(&value)+(length-1), 1);
		high_byte |= prefixes[length-1];
		std::memcpy(dest, &high_byte, sizeof(high_byte));
		std::memcpy(dest+1, &value, length-1);
		dest += length;
	} else {
		*dest++ = ~std::byte{0};
		std::memcpy(dest, &value, sizeof(value));
		dest += sizeof(value);
	}
	return dest;
}
inline std::byte* write(std::byte* const& dest, std::uint64_t value) {
	std::byte* d = dest;
	return write(d, value);
}

inline std::uint64_t read(const std::byte*& src) {
	std::uint64_t ret = 0;
	//Count the number of leading 1s in the first byte, working around __builtin_clz undefinedness.
	unsigned int header_comp = std::to_integer<unsigned int>(~*src);
	int trailers = header_comp ? __builtin_clz(header_comp) - 24 : 8;
	std::memcpy(&ret, src+1, trailers);
	if (trailers < 8) {//8 is special: no significant bits in the first byte
		std::byte high_byte_bits = *src & ~prefixes[trailers];
		std::memcpy(reinterpret_cast<char*>(&ret)+trailers, &high_byte_bits, 1);
	}
	src += trailers + 1;
	return ret;
}
inline std::uint64_t read(std::byte*& src) {
	const std::byte* s = src;
	std::uint64_t ret = read(s);
	src = src + (s - src);
	return ret;
}
}//namespace upv

//TODO: factor varint32 out of PackReader/Writer

namespace varint64 {
//https://sqlite.org/src4/doc/trunk/www/varint.wiki but little-endian
constexpr inline std::uint64_t VARINT_ONE = 240, VARINT_TWO = 248, VARINT_THREE = 249,
		VARINT_FOUR = 250, VARINT_FIVE = 251, VARINT_SIX = 252, VARINT_SEVEN = 253, VARINT_EIGHT = 254;
inline void write(std::byte*& dest, std::uint64_t value) {
	//memcpy, but always to and advancing dest
	auto write = [&dest](const std::uint64_t& value, std::size_t amount) {
		//TODO: assert higher bytes are zero?
		std::memcpy(dest, &value, amount);
		dest += amount;
	};

	if (value <= VARINT_ONE)
		write(value, 1);
	else if (value <= (VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE - 1) {
		write((value - VARINT_ONE) / 256 + VARINT_ONE + 1, 1);
		write((value - VARINT_ONE) % 256, 1);
	} else if (value <= (VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE - 1 + 65536) {
		write(VARINT_THREE, 1);
		write((value - ((VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE)) / 256, 1);
		write((value - ((VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE)) % 256, 1);
	} else {
		//TODO: this might be too clever (making the write length not a constant)
		//Compute the minimum number of bytes to represent the value.
		std::size_t amount = sizeof(std::uint64_t) - (__builtin_clzl(value) / 8);
		write(VARINT_TWO - 1 + amount, 1);
		write(value, amount);
	}
}

inline std::uint64_t read(const std::byte*& src) {
	std::uint64_t first = 0;
	std::memcpy(&first, src++, 1);
	if (first <= VARINT_ONE)
		return first;
	else if (first <= VARINT_TWO) {
		std::uint64_t second = 0;
		std::memcpy(&second, src++, 1);
		return VARINT_ONE + 256*(first - VARINT_ONE - 1) + second;
	} else if (first == VARINT_THREE) {
		std::uint64_t second = 0, third = 0;
		std::memcpy(&second, src++, 1);
		std::memcpy(&third, src++, 1);
		return (VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE + 256*second + third;
	} else {
		std::size_t amount = first - (VARINT_TWO-1);
		std::uint64_t ret = 0;
		std::memcpy(&ret, src, amount);
		src += amount;
		return ret;
	}
}
} //namespace varint64

#endif /* VARINT_HPP */

