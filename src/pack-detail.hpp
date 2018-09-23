/*
 * File:   pack-detail.hpp
 * Author: Jeffrey Bosboom <jbosboom@csail.mit.edu>
 *
 * Created on September 21, 2018, 1:13 AM
 */

#ifndef AUTOMATON_PACK_DETAIL_HPP_INCLUDED
#define AUTOMATON_PACK_DETAIL_HPP_INCLUDED

/**
 * Contains details exposed for testing purposes only.
 */

namespace automaton {
namespace detail {

#define VARINT_ONE 244
#define VARINT_TWO 252
#define VARINT_THREE 253
#define VARINT_FOUR 254
#define VARINT_FIVE 255

class PackWriter {
public:
	PackWriter(Pack* first, Pack* last) : cur_(first), last_(last), first_(first), overflow_(false) {}
	PackWriter& write8(unsigned int value) {
		assert((value & 0xFFU) == value);
		writeBytes<1>(value);
		return *this;
	}
	PackWriter& write16(unsigned int value) {
		assert((value & 0xFFFFU) == value);
		writeBytes<2>(value);
		return *this;
	}
	PackWriter& write24(unsigned int value) {
		assert((value & 0xFFFFFFU) == value);
		writeBytes<3>(value);
		return *this;
	}
	PackWriter& write32(unsigned int value) {
		assert((value & 0xFFFFFFFFU) == value);
		writeBytes<4>(value);
		return *this;
	}
	PackWriter& writeBytes(unsigned int value, unsigned int count) {
		switch (count) {
			case 1: return write8(value);
			case 2: return write16(value);
			case 3: return write24(value);
			case 4: return write32(value);
			default:
				assert(false);
				__builtin_unreachable();
		}
	}
	PackWriter& writeVarint(unsigned int value) {
		//inspired by https://sqlite.org/src4/doc/trunk/www/varint.wiki but
		//only with 32-bit range, so recovering a few more small values.
		if (value <= VARINT_ONE)
			write8(value);
		else if (value <= (VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE - 1) {
			write8((value - VARINT_ONE) / 256 + VARINT_ONE + 1);
			write8((value - VARINT_ONE) % 256);
		} else if (value <= (VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE - 1 + 65536) {
			write8(VARINT_THREE);
			write8((value - ((VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE)) / 256);
			write8((value - ((VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE)) % 256);
		} else if (value <= 16777215) {
			write8(VARINT_FOUR);
			write24(value);
		} else {
			write8(VARINT_FIVE);
			write32(value);
		}
		return *this;
	}

	PackWriter& seek(Pack* pos) {
		assert(first_ <= pos);
		if (!(pos < last_)) {
			pos = last_;
			overflow_ = true;
		}
		cur_ = pos;
		return *this;
	}
	Pack* tell() const {return cur_;}
	bool overflow() const {return overflow_;}
private:
	template<unsigned int N>
	void writeBytes(unsigned int value) {
		std::array<std::byte, N> array;
		std::memcpy(array.begin(), &value, N);
		write(array.begin(), array.end());
	}
	void write(std::byte* begin, std::byte* end) {
		auto count = end - begin;
		if (count > last_ - cur_) {
			count = last_ - cur_;
			overflow_ = true;
		}
		cur_ = std::copy_n(begin, count, cur_);
	}
	Pack* cur_;
	Pack* const last_;
	Pack* const first_;
	bool overflow_; //set on attempt to write past last_
};


class PackReader {
public:
	PackReader(const Pack* first, const Pack* last) : cur_(first), last_(last), first_(first), overflow_(false) {}
	unsigned int read8() {
		return readBytes<1>();
	}
	unsigned int read16() {
		return readBytes<2>();
	}
	unsigned int read24() {
		return readBytes<3>();
	}
	unsigned int read32() {
		return readBytes<4>();
	}
	unsigned int readBytes(unsigned int count) {
		switch (count) {
			case 1: return read8();
			case 2: return read16();
			case 3: return read24();
			case 4: return read32();
			default:
				assert(false);
				__builtin_unreachable();
		}
	}
	unsigned int readVarint() {
		unsigned int first = read8();
		if (first <= VARINT_ONE)
			return first;
		if (first <= VARINT_TWO) {
			unsigned int second = read8();
			return VARINT_ONE + 256*(first - VARINT_ONE - 1) + second;
		}
		if (first == VARINT_THREE) {
			unsigned int second = read8();
			unsigned int third = read8();
			return (VARINT_TWO - VARINT_ONE)*256 + VARINT_ONE + 256*second + third;
		}
		if (first == VARINT_FOUR)
			return read24();
		if (first == VARINT_FIVE)
			return read32();
		__builtin_unreachable();
	}

	PackReader& seek(Pack* pos) {
		assert(first_ <= pos && pos < last_);
		cur_ = pos;
		return *this;
	}
	const Pack* tell() const {return cur_;}
	bool overflow() const {return overflow_;}
private:
	template<unsigned int N>
	unsigned int readBytes() {
		unsigned int value = 0;
		if constexpr (N == 3) {
			//If we won't read off the end by doing so, it's much faster to load
			//an aligned dword and mask off the bytes we want.
			if (cur_ + 4 <= last_) {
				std::memcpy(&value, cur_, 4);
				value &= 0x00FFFFFF;
				cur_ += N;
				return value;
			}
		}

		if (cur_ + N > last_) {
			overflow_ = true;
			return 0;
		}
		std::memcpy(&value, cur_, N);
		cur_ += N;
		return value;
	}
	const Pack* cur_;
	const Pack* const last_;
	const Pack* const first_;
	bool overflow_; //set on attempt to read past last_
};

} //namespace detail
} //namespace automaton

#endif /* AUTOMATON_PACK_DETAIL_HPP_INCLUDED */

