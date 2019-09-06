#ifndef VARINT_HPP
#define VARINT_HPP

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

