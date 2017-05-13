#include "precompiled.hpp"
#include "algoutils.hpp"
#include <gtest/gtest.h>

TEST(AlgoutilsTest, ApplyReversePermutationSanity) {
	dynarray<int> foo(5), perm(5);
	std::iota(foo.begin(), foo.end(), 0);
	perm[0] = 1; perm[1] = 0; perm[2] = 4; perm[3] = 3; perm[4] = 2;
	apply_reverse_permutation(foo.begin(), foo.end(), perm.begin());
	EXPECT_EQ(foo[1], 0);
	EXPECT_EQ(foo[0], 1);
	EXPECT_EQ(foo[4], 2);
	EXPECT_EQ(foo[3], 3);
	EXPECT_EQ(foo[2], 4);
}

TEST(AlgoutilsTest, IsPossiblyMirroredRotationPermutation) {
	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({0}));
	ASSERT_FALSE(is_possibly_mirrored_rotation_permutation({1}));
	ASSERT_FALSE(is_possibly_mirrored_rotation_permutation({42}));

	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({0, 1}));
	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({1, 0}));
	ASSERT_FALSE(is_possibly_mirrored_rotation_permutation({0, 0}));
	ASSERT_FALSE(is_possibly_mirrored_rotation_permutation({1, 1}));

	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({0, 1, 2, 3}));
	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({1, 2, 3, 0}));
	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({2, 3, 0, 1}));
	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({3, 0, 1, 2}));
	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({0, 3, 2, 1}));
	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({1, 0, 3, 2}));
	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({2, 1, 0, 3}));
	ASSERT_TRUE(is_possibly_mirrored_rotation_permutation({3, 2, 1, 0}));
	ASSERT_FALSE(is_possibly_mirrored_rotation_permutation({0, 2, 1, 3}));
	ASSERT_FALSE(is_possibly_mirrored_rotation_permutation({1, 0, 2, 3}));
}

TEST(AlgoutilsTest, UnorderedEqual) {
	ASSERT_TRUE(unordered_equal({0}, {0}));
	ASSERT_TRUE(unordered_equal({1, 0}, {0, 1}));
	ASSERT_TRUE(unordered_equal({0, 0}, {0, 0}));
	ASSERT_TRUE(unordered_equal({0, 1, 0}, {1, 0, 0}));

	ASSERT_FALSE(unordered_equal({0}, {1}));
	ASSERT_FALSE(unordered_equal({0, 0}, {0}));
}