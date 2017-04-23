#include "precompiled.hpp"
#include "algoutils.hpp"
#include <gtest/gtest.h>

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