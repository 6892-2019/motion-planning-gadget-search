#include "precompiled.hpp"
#include "varint.hpp"
#include <doctest/doctest.h>

using std::uint64_t;
using std::pair;

const static std::pair<uint64_t, std::ptrdiff_t> upv_data[] = {
	{0, 1},	{1, 1}, {(1ul << 7) - 1, 1},
	{1ul << 7, 2},  {(1ul << 14) - 1, 2},
	{1ul << 14, 3}, {(1ul << 21) - 1, 3},
	{1ul << 21, 4}, {(1ul << 28) - 1, 4},
	{1ul << 28, 5}, {(1ul << 35) - 1, 5},
	{1ul << 35, 6}, {(1ul << 42) - 1, 6},
	{1ul << 42, 7}, {(1ul << 49) - 1, 7},
	{1ul << 49, 8}, {(1ul << 56) - 1, 8},
	{1ul << 56, 9}, {std::numeric_limits<std::uint64_t>::max(), 9},
};

TEST_CASE("upv_RoundtripBoundaries") {
	using upv::write, upv::read;
	std::array<std::byte, 11> data;
	for (auto&& [thing, expected_length] : upv_data) {
		std::fill(data.begin(), data.end(), std::byte{0});
		std::byte* end = write(data.data(), thing);
		CHECK_EQ(end - data.begin(), expected_length);

		end = data.data();
		uint64_t rt = read(end);
		CHECK_EQ(rt, thing);
		CHECK_EQ(end - data.begin(), expected_length);
	}
}

TEST_CASE("upv_RoundtripSmallExhaustive") {
	using upv::write, upv::read;
	std::array<std::byte, 11> data;
	for (std::uint64_t i = 0; i < (1ul << 14); ++i) {
		std::fill(data.begin(), data.end(), std::byte{0});
		std::byte* write_end = write(data.data(), i);
		std::byte* read_end = data.data();
		uint64_t rt = read(read_end);
		CHECK_EQ(rt, i);
		CHECK_EQ(write_end - data.data(), read_end - data.data());
	}
}

TEST_CASE("upv_RoundtripRandom") {
	using upv::write, upv::read;
	std::array<std::byte, 11> data;
	std::knuth_b rng(0);
	std::uniform_int_distribution<int> width(14, 64);
	std::independent_bits_engine<decltype(rng), 64, std::uint64_t> value(1);
	for (int samples = 0; samples < 1000000; ++samples) {
		int bits = width(rng);
		std::uint64_t i = value();
		if (bits < 64)
			i &= (1ul << bits) - 1;
		std::fill(data.begin(), data.end(), std::byte{0});
		std::byte* write_end = write(data.data(), i);
		std::byte* read_end = data.data();
		uint64_t rt = read(read_end);
		CHECK_EQ(rt, i);
		CHECK_EQ(write_end - data.data(), read_end - data.data());
	}
}