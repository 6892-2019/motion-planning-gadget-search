#include "precompiled.hpp"
#include "algoutils.hpp"
#include <doctest/doctest.h>

TEST_CASE("AlgoutilsTest_ApplyReversePermutationSanity") {
	dynarray<int> foo(5), perm(5);
	std::iota(foo.begin(), foo.end(), 0);
	perm[0] = 1; perm[1] = 0; perm[2] = 4; perm[3] = 3; perm[4] = 2;
	apply_reverse_permutation(foo.begin(), foo.end(), perm.begin());
	CHECK_EQ(foo[1], 0);
	CHECK_EQ(foo[0], 1);
	CHECK_EQ(foo[4], 2);
	CHECK_EQ(foo[3], 3);
	CHECK_EQ(foo[2], 4);
}

TEST_CASE("AlgoutilsTest_IsPossiblyMirroredRotationPermutation") {
	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({0}));
	REQUIRE_UNARY_FALSE(is_possibly_mirrored_rotation_permutation({1}));
	REQUIRE_UNARY_FALSE(is_possibly_mirrored_rotation_permutation({42}));

	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({0, 1}));
	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({1, 0}));
	REQUIRE_UNARY_FALSE(is_possibly_mirrored_rotation_permutation({0, 0}));
	REQUIRE_UNARY_FALSE(is_possibly_mirrored_rotation_permutation({1, 1}));

	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({0, 1, 2, 3}));
	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({1, 2, 3, 0}));
	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({2, 3, 0, 1}));
	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({3, 0, 1, 2}));
	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({0, 3, 2, 1}));
	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({1, 0, 3, 2}));
	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({2, 1, 0, 3}));
	REQUIRE_UNARY(is_possibly_mirrored_rotation_permutation({3, 2, 1, 0}));
	REQUIRE_UNARY_FALSE(is_possibly_mirrored_rotation_permutation({0, 2, 1, 3}));
	REQUIRE_UNARY_FALSE(is_possibly_mirrored_rotation_permutation({1, 0, 2, 3}));
}

TEST_CASE("AlgoutilsTest_UnorderedEqual") {
	REQUIRE_UNARY(unordered_equal({0}, {0}));
	REQUIRE_UNARY(unordered_equal({1, 0}, {0, 1}));
	REQUIRE_UNARY(unordered_equal({0, 0}, {0, 0}));
	REQUIRE_UNARY(unordered_equal({0, 1, 0}, {1, 0, 0}));

	REQUIRE_UNARY_FALSE(unordered_equal({0}, {1}));
	REQUIRE_UNARY_FALSE(unordered_equal({0, 0}, {0}));
}

TEST_CASE("AlgoutilsTest_PartitionOnRangeExclusion00") {
	dynarray<int> foo(7);
	std::iota(foo.begin(), foo.end(), 0);
	std::vector<std::pair<int, int>> v;
	v.emplace_back(2, 5);

	int* x = partition_on_range_exclusion(foo.begin(), foo.end(), v.cbegin(), v.cend());
	CHECK_EQ(x, foo.begin()+4);
	CHECK_EQ(foo[0], 0);
	CHECK_EQ(foo[1], 1);
	CHECK_EQ(foo[2], 5);
	CHECK_EQ(foo[3], 6);
}

TEST_CASE("AlgoutilsTest_PartitionOnRangeExclusion01") {
	dynarray<int> foo(7);
	std::iota(foo.begin(), foo.end(), 0);
	std::vector<std::pair<int, int>> v;
	v.emplace_back(0, 2);
	v.emplace_back(5, 1000);

	int* x = partition_on_range_exclusion(foo.begin(), foo.end(), v.cbegin(), v.cend());
	CHECK_EQ(x, foo.begin()+3);
	CHECK_EQ(foo[0], 2);
	CHECK_EQ(foo[1], 3);
	CHECK_EQ(foo[2], 4);
}

TEST_CASE("AlgoutilsTest_PartitionOnRangeExclusion02") {
	dynarray<int> foo(7);
	std::iota(foo.begin(), foo.end(), 0);
	std::vector<std::pair<int, int>> v;
	v.emplace_back(1, 5);
	v.emplace_back(5, 6);

	int* x = partition_on_range_exclusion(foo.begin(), foo.end(), v.cbegin(), v.cend());
	CHECK_EQ(x, foo.begin()+2);
	CHECK_EQ(foo[0], 0);
	CHECK_EQ(foo[1], 6);
}

TEST_CASE("AlgoutilsTest_MergeUnique00") {
	std::initializer_list<int> left = {0, 2, 3}, right = {1, 3, 4};
	std::vector<int> actual;
	merge_unique(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(actual));
	std::initializer_list<int> expected = {0, 1, 2, 3, 4};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("AlgoutilsTest_MergeUnique01") {
	std::initializer_list<int> left = {}, right = {1, 3, 4};
	std::vector<int> actual;
	merge_unique(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(actual));
	std::initializer_list<int> expected = {1, 3, 4};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

TEST_CASE("AlgoutilsTest_MergeUnique02") {
	std::initializer_list<int> left = {0, 2, 3}, right = {};
	std::vector<int> actual;
	merge_unique(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(actual));
	std::initializer_list<int> expected = {0, 2, 3};
	CHECK_UNARY(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}