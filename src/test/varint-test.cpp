#include "precompiled.hpp"
#include "varint.hpp"
#include <doctest.h>

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

TEST_CASE("upv_Roundtrip") {
	using upv::write, upv::read;
	std::array<std::byte, 11> data;
	for (const auto [thing, expected_length] : upv_data) {
		std::fill(data.begin(), data.end(), std::byte{0});
		std::byte* end = write(data.data(), thing);
		CHECK_EQ(end - data.begin(), expected_length);

		end = data.data();
		uint64_t rt = read(end);
		CHECK_EQ(rt, thing);
		CHECK_EQ(end - data.begin(), expected_length);
	}
}