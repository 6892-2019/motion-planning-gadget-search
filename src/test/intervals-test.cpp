#include "precompiled.hpp"
#include "intervals.hpp"
#include <doctest.h>
#include <fmt/format.h>

using std::initializer_list;
using std::pair;
using std::vector;

TEST_CASE("IntervalsTest_MaximalIntervals00") {
	initializer_list<int> input = {};
	auto actual = maximal_intervals(input.begin(), input.end());
	CHECK_EQ(actual.size(), 0);
}

TEST_CASE("IntervalsTest_MaximalIntervals01") {
	initializer_list<int> input = {12};
	auto actual = maximal_intervals(input.begin(), input.end());
	initializer_list<pair<int, int>> expected = {{12, 13}};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_MaximalIntervals02") {
	initializer_list<int> input = {1, 2};
	auto actual = maximal_intervals(input.begin(), input.end());
	initializer_list<pair<int, int>> expected = {{1, 3}};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_MaximalIntervals03") {
	initializer_list<int> input = {1, 2, 3, 4, 5};
	auto actual = maximal_intervals(input.begin(), input.end());
	initializer_list<pair<int, int>> expected = {{1, 6}};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_MaximalIntervals04") {
	initializer_list<int> input = {1, 5};
	auto actual = maximal_intervals(input.begin(), input.end());
	initializer_list<pair<int, int>> expected = {{1, 2}, {5, 6}};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_MaximalIntervals05") {
	initializer_list<int> input = {1, 2, 5};
	auto actual = maximal_intervals(input.begin(), input.end());
	initializer_list<pair<int, int>> expected = {{1, 3}, {5, 6}};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_MaximalIntervals06") {
	initializer_list<int> input = {1, 4, 5};
	auto actual = maximal_intervals(input.begin(), input.end());
	initializer_list<pair<int, int>> expected = {{1, 2}, {4, 6}};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_MaximalIntervals07") {
	initializer_list<int> input = {1, 2, 4, 5};
	auto actual = maximal_intervals(input.begin(), input.end());
	initializer_list<pair<int, int>> expected = {{1, 3}, {4, 6}};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_MaximalIntervals08") {
	initializer_list<int> input = {1, 3, 5};
	auto actual = maximal_intervals(input.begin(), input.end());
	initializer_list<pair<int, int>> expected = {{1, 2}, {3, 4}, {5, 6}};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}



TEST_CASE("IntervalsTest_IntervalAccumulator00") {
	initializer_list<int> inputs = {};
	initializer_list<pair<int, int>> expected = {};
	interval_accumulator<int> accum(256);
	for (int i : inputs)
		accum(i);
	auto actual = std::move(accum).finish();
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalAccumulator01") {
	initializer_list<int> inputs = {5, 6, 1, 2};
	initializer_list<pair<int, int>> expected = {{1, 3}, {5, 7}};
	interval_accumulator<int> accum(2);
	for (int i : inputs)
		accum(i);
	auto actual = std::move(accum).finish();
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalAccumulator02") {
	initializer_list<int> inputs = {1, 2, 1, 2};
	initializer_list<pair<int, int>> expected = {{1, 3}};
	interval_accumulator<int> accum(2);
	for (int i : inputs)
		accum(i);
	auto actual = std::move(accum).finish();
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalAccumulator03") {
	initializer_list<int> inputs = {2, 3, 1, 4};
	initializer_list<pair<int, int>> expected = {{1, 5}};
	interval_accumulator<int> accum(2);
	for (int i : inputs)
		accum(i);
	auto actual = std::move(accum).finish();
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}



TEST_CASE("IntervalsTest_IntervalIntersection00") {
	initializer_list<pair<int, int>> left = {};
	initializer_list<pair<int, int>> right = {};
	initializer_list<pair<int, int>> expected = {};
	auto actual = interval_intersection(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_intersection(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalIntersection01") {
	initializer_list<pair<int, int>> left = {{1, 100}};
	initializer_list<pair<int, int>> right = {};
	initializer_list<pair<int, int>> expected = {};
	auto actual = interval_intersection(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_intersection(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalIntersection02") {
	initializer_list<pair<int, int>> left = {{1, 100}};
	initializer_list<pair<int, int>> right = {{1, 10}};
	initializer_list<pair<int, int>> expected = {{1, 10}};
	auto actual = interval_intersection(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_intersection(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalIntersection03") {
	initializer_list<pair<int, int>> left = {{1, 100}};
	initializer_list<pair<int, int>> right = {{90, 100}};
	initializer_list<pair<int, int>> expected = {{90, 100}};
	auto actual = interval_intersection(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_intersection(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalIntersection04") {
	initializer_list<pair<int, int>> left = {{1, 100}};
	initializer_list<pair<int, int>> right = {{45, 55}};
	initializer_list<pair<int, int>> expected = {{45, 55}};
	auto actual = interval_intersection(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_intersection(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalIntersection05") {
	initializer_list<pair<int, int>> left = {{1, 100}};
	initializer_list<pair<int, int>> right = {{100, 200}};
	initializer_list<pair<int, int>> expected = {};
	auto actual = interval_intersection(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_intersection(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalIntersection06") {
	initializer_list<pair<int, int>> left = {{1, 110}};
	initializer_list<pair<int, int>> right = {{90, 200}};
	initializer_list<pair<int, int>> expected = {{90, 110}};
	auto actual = interval_intersection(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_intersection(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}



TEST_CASE("IntervalsTest_IntervalUnion00") {
	initializer_list<pair<int, int>> left = {};
	initializer_list<pair<int, int>> right = {};
	initializer_list<pair<int, int>> expected = {};
	auto actual = interval_union(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_union(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalUnion01") {
	initializer_list<pair<int, int>> left = {{1, 10}};
	initializer_list<pair<int, int>> right = {};
	initializer_list<pair<int, int>> expected = {{1, 10}};
	auto actual = interval_union(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_union(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalUnion02") {
	initializer_list<pair<int, int>> left = {{1, 10}};
	initializer_list<pair<int, int>> right = {{1, 10}};
	initializer_list<pair<int, int>> expected = {{1, 10}};
	auto actual = interval_union(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_union(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalUnion03") {
	initializer_list<pair<int, int>> left = {{1, 10}};
	initializer_list<pair<int, int>> right = {{3, 7}};
	initializer_list<pair<int, int>> expected = {{1, 10}};
	auto actual = interval_union(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_union(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}

TEST_CASE("IntervalsTest_IntervalUnion04") {
	initializer_list<pair<int, int>> left = {{1, 2}, {3, 4}};
	initializer_list<pair<int, int>> right = {{2, 3}, {4, 5}};
	initializer_list<pair<int, int>> expected = {{1, 5}};
	auto actual = interval_union(left.begin(), left.end(), right.begin(), right.end());
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
	auto actual2 = interval_union(right.begin(), right.end(), left.begin(), left.end());
	CHECK_UNARY(std::equal(actual2.begin(), actual2.end(), expected.begin(), expected.end()));
}



namespace {
vector<int> indices_of_set_bits(unsigned int x) {
	vector<int> ret;
	for (int i = 0; x; ++i) {
		if (x & 1)
			ret.push_back(i);
		x >>= 1;
	}
	return ret;
}
}

TEST_CASE("IntervalsTest_IntervalIntersectionExhaustion") {
	//Intersection of intervals is equivalent to intersection of elements.
	//This test isn't perfect, because it only tests maximal ranges.  But we
	//intend to keep our interval sets maximal, so it's a case we care about.
	constexpr unsigned int limit = 1 << 8;
	for (unsigned int left = 0; left < limit; ++left) {
		auto leftbits = indices_of_set_bits(left);
		auto leftranges = maximal_intervals(leftbits.cbegin(), leftbits.cend());
		for (unsigned int right = 0; right < limit; ++right) {
			auto rightbits = indices_of_set_bits(right);
			auto rightranges = maximal_intervals(rightbits.cbegin(), rightbits.cend());

			auto expectbits = indices_of_set_bits(left & right);
			auto expectranges = maximal_intervals(expectbits.cbegin(), expectbits.cend());

			auto actual = interval_intersection(leftranges.cbegin(), leftranges.cend(), rightranges.cbegin(), rightranges.cend());
			//doctest stringization grumble: uncomment this and use --abort-after=1 to see what failed
//			fmt::print(stderr, "{} / {} / {} / {}\n", leftranges, rightranges, actual, expectranges);
			CHECK_EQ(actual, expectranges);
		}
	}
}

TEST_CASE("IntervalsTest_IntervalUnionExhaustion") {
	//Union of intervals is equivalent to union of elements.
	//This test isn't perfect, because it only tests maximal ranges.  But we
	//intend to keep our interval sets maximal, so it's a case we care about.
	constexpr unsigned int limit = 1 << 8;
	for (unsigned int left = 0; left < limit; ++left) {
		auto leftbits = indices_of_set_bits(left);
		auto leftranges = maximal_intervals(leftbits.cbegin(), leftbits.cend());
		for (unsigned int right = 0; right < limit; ++right) {
			auto rightbits = indices_of_set_bits(right);
			auto rightranges = maximal_intervals(rightbits.cbegin(), rightbits.cend());

			auto expectbits = indices_of_set_bits(left | right);
			auto expectranges = maximal_intervals(expectbits.cbegin(), expectbits.cend());

			auto actual = interval_union(leftranges.cbegin(), leftranges.cend(), rightranges.cbegin(), rightranges.cend());
			//doctest stringization grumble: uncomment this and use --abort-after=1 to see what failed
//			fmt::print(stderr, "{} / {} / {} / {}\n", leftranges, rightranges, actual, expectranges);
			CHECK_EQ(actual, expectranges);
		}
	}
}

TEST_CASE("IntervalsTest_IntervalDifferenceExhaustion") {
	//Difference of intervals is equivalent to difference of elements.
	//This test isn't perfect, because it only tests maximal ranges.  But we
	//intend to keep our interval sets maximal, so it's a case we care about.
	constexpr unsigned int limit = 1 << 8;
	for (unsigned int left = 0; left < limit; ++left) {
		auto leftbits = indices_of_set_bits(left);
		auto leftranges = maximal_intervals(leftbits.cbegin(), leftbits.cend());
		for (unsigned int right = 0; right < limit; ++right) {
			auto rightbits = indices_of_set_bits(right);
			auto rightranges = maximal_intervals(rightbits.cbegin(), rightbits.cend());

			auto expectbits = indices_of_set_bits(left & ~right);
			auto expectranges = maximal_intervals(expectbits.cbegin(), expectbits.cend());

			auto actual = interval_difference(leftranges.cbegin(), leftranges.cend(), rightranges.cbegin(), rightranges.cend());
			//doctest stringization grumble: uncomment this and use --abort-after=1 to see what failed
//			fmt::print(stderr, "{} / {} / {} / {}\n", leftranges, rightranges, actual, expectranges);
			CHECK_EQ(actual, expectranges);
		}
	}
}

TEST_CASE("IntervalsTest_IntervalCoalesceExhaustion") {
	//Coalescence of the sorted concatenation of interval lists is equivalent to union.
	//For this test, we don't care about left and right, because the sorted
	//concatenation is the same.
	constexpr unsigned int limit = 1 << 8;
	for (unsigned int left = 0; left < limit; ++left) {
		auto leftbits = indices_of_set_bits(left);
		auto leftranges = maximal_intervals(leftbits.cbegin(), leftbits.cend());
		for (unsigned int right = 0; right <= left; ++right) {
			auto rightbits = indices_of_set_bits(right);
			auto rightranges = maximal_intervals(rightbits.cbegin(), rightbits.cend());

			//could also compare against interval_union here
			auto expectbits = indices_of_set_bits(left | right);
			auto expectranges = maximal_intervals(expectbits.cbegin(), expectbits.cend());

			auto mergeranges = leftranges;
			mergeranges.insert(mergeranges.end(), rightranges.begin(), rightranges.end());
			std::sort(mergeranges.begin(), mergeranges.end());
			auto actual = interval_coalesce(mergeranges.cbegin(), mergeranges.cend());
			//doctest stringization grumble: uncomment this and use --abort-after=1 to see what failed
//			fmt::print(stderr, "{} / {} / {} / {} / {}\n", leftranges, rightranges, mergeranges, actual, expectranges);
			CHECK_EQ(actual, expectranges);
		}
	}
}

TEST_CASE("IntervalsTest_IntervalChunkExhaustion") {
	//Chunks should have the chunk size (except the last), be ordered with
	//respect to each other, and together constitute the full set.
	vector<pair<int, int>> sum;
	constexpr unsigned int limit = 1 << 8;
	for (unsigned int left = 0; left < limit; ++left) {
		auto leftbits = indices_of_set_bits(left);
		auto leftranges = maximal_intervals(leftbits.cbegin(), leftbits.cend());
		for (unsigned int chunk_size = 1; chunk_size < leftbits.size() + 1; ++chunk_size) {
			auto chunks = interval_chunk(leftranges.cbegin(), leftranges.cend(), chunk_size);
			sum.clear();
			for (auto chit = chunks.begin(); chit != chunks.end(); ++chit) {
				CHECK_UNARY(!chit->empty());
				sum = interval_union(sum.cbegin(), sum.cend(), chit->cbegin(), chit->cend());
				auto next = std::next(chit);
				if (next != chunks.end()) {
					CHECK_EQ(interval_size(chit->cbegin(), chit->cend()), chunk_size);
					CHECK_LE(chit->back().second, next->front().first);
				}
			}
			CHECK_EQ(sum, leftranges);
		}
	}
}

TEST_CASE("IntervalsTest_IntervalInflateExhaustion") {
	constexpr unsigned int limit = 1 << 8;
	for (unsigned int left = 0; left < limit; ++left) {
		auto leftbits = indices_of_set_bits(left);
		auto leftranges = maximal_intervals(leftbits.cbegin(), leftbits.cend());
		auto actual = interval_inflate(leftranges.begin(), leftranges.end());
		CHECK_EQ(actual, leftbits);
		CHECK_UNARY(std::is_sorted(actual.cbegin(), actual.cend()));
	}
}

TEST_CASE("IntervalsTest_IntervalContainsExhaustion") {
	constexpr int element_limit = 16;
	constexpr unsigned int limit = 1 << element_limit;
	for (unsigned int left = 0; left < limit; ++left) {
		auto leftbits = indices_of_set_bits(left);
		auto leftranges = maximal_intervals(leftbits.cbegin(), leftbits.cend());
		//deliberately try two cases never in the ranges
		for (int test = -1; test <= element_limit; ++test) {
//			fmt::print("{} {}\n", leftranges, test);
			CHECK_EQ(interval_contains(leftranges, test), std::binary_search(leftbits.cbegin(), leftbits.cend(), test));
		}
	}
}