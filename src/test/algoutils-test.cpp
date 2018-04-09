#include "precompiled.hpp"
#include "algoutils.hpp"
#include <doctest.h>

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