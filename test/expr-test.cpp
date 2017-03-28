#include "precompiled.hpp"
#include "expr.hpp"
#include "automaton.hpp"
#include "regex.hpp" //for interpret(Expr::const_ptr)
#include <gtest/gtest.h>

using automaton::impl::Expr;
using automaton::impl::interpret;
using automaton::Automaton;

TEST(ExprTest, PathPuzzleColConstraint) {
	//((.{3}*(1|2).{2}.{3}*(1|2).{2}.{3}*)&.{9})
	auto selectSubset = interpret<3>(Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(2), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(2), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }));
	EXPECT_FALSE(selectSubset->isEmpty());
	EXPECT_FALSE(selectSubset->infinite());
	EXPECT_FALSE(selectSubset->run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(selectSubset->run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(selectSubset->run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(selectSubset->run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(selectSubset->run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(selectSubset->run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(selectSubset->run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(selectSubset->run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//((.{3}*(0|0).{2}.{3}*)&.{9})
	auto unselectSubset = interpret<3>(Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(0), Expr::lit(0), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }));
	EXPECT_FALSE(unselectSubset->isEmpty());
	EXPECT_FALSE(unselectSubset->infinite());
	EXPECT_TRUE(unselectSubset->run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	EXPECT_FALSE(unselectSubset->run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//exactly two of the three elements at positions {0, 3, 6} should be 1 or 2
	//(the intersection of the above regexes)
	auto subset = interpret<3>(Expr::conj({Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(2), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(2), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }), Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(0), Expr::lit(0), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }), }));
	EXPECT_FALSE(subset->isEmpty());
	EXPECT_FALSE(subset->infinite());
	EXPECT_FALSE(subset->run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(subset->run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(subset->run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(subset->run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(subset->run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(subset->run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(subset->run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	EXPECT_FALSE(subset->run({1, 0, 0, 1, 0, 0, 1, 0, 0}));
}

TEST(ExprTest, PathPuzzleColConstraintBinary) {
	//((.{3}*(1|1).{2}.{3}*(1|1).{2}.{3}*)&.{9})
	auto selectSubset = interpret<2>(Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(1), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(1), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }));
	EXPECT_FALSE(selectSubset->isEmpty());
	EXPECT_FALSE(selectSubset->infinite());
	EXPECT_FALSE(selectSubset->run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(selectSubset->run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(selectSubset->run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(selectSubset->run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(selectSubset->run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(selectSubset->run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(selectSubset->run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(selectSubset->run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//((.{3}*(0|0).{2}.{3}*)&.{9})
	auto unselectSubset = interpret<2>(Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(0), Expr::lit(0), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }));
	EXPECT_FALSE(unselectSubset->isEmpty());
	EXPECT_FALSE(unselectSubset->infinite());
	EXPECT_TRUE(unselectSubset->run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(unselectSubset->run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	EXPECT_FALSE(unselectSubset->run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//(.{3}*0.{2}.{3}*)
	auto unselectSubsetInfinite = interpret<2>(Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::lit(0), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }));
	unselectSubsetInfinite->minimize();
	EXPECT_FALSE(unselectSubsetInfinite->isEmpty());
	EXPECT_TRUE(unselectSubsetInfinite->infinite());
	EXPECT_TRUE(unselectSubsetInfinite->run({0, 0, 0}));
	EXPECT_TRUE(unselectSubsetInfinite->run({0, 0, 1}));
	EXPECT_TRUE(unselectSubsetInfinite->run({0, 1, 1}));
	EXPECT_FALSE(unselectSubsetInfinite->run({1, 0, 0}));
	EXPECT_TRUE(unselectSubsetInfinite->run({1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubsetInfinite->run({1, 0, 0, 0, 0, 1}));
	EXPECT_TRUE(unselectSubsetInfinite->run({1, 0, 0, 0, 1, 1}));
	EXPECT_FALSE(unselectSubsetInfinite->run({1, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(unselectSubsetInfinite->run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubsetInfinite->run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubsetInfinite->run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubsetInfinite->run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(unselectSubsetInfinite->run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubsetInfinite->run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(unselectSubsetInfinite->run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	EXPECT_FALSE(unselectSubsetInfinite->run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//(.{3}*0.{2}) - i.e., checks the last three characters of strings with length a multiple of three
	auto unselectSubsetLeftInfinite = interpret<2>(Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::lit(0), Expr::repeat(Expr::any(), 2, 2)}));
	unselectSubsetLeftInfinite->minimize();
	EXPECT_FALSE(unselectSubsetLeftInfinite->isEmpty());
	EXPECT_TRUE(unselectSubsetLeftInfinite->infinite());
	EXPECT_TRUE(unselectSubsetLeftInfinite->run({0, 0, 0}));
	EXPECT_TRUE(unselectSubsetLeftInfinite->run({0, 0, 1}));
	EXPECT_TRUE(unselectSubsetLeftInfinite->run({0, 1, 1}));
	EXPECT_FALSE(unselectSubsetLeftInfinite->run({1, 0, 0}));
	EXPECT_TRUE(unselectSubsetLeftInfinite->run({1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubsetLeftInfinite->run({1, 0, 0, 0, 0, 1}));
	EXPECT_TRUE(unselectSubsetLeftInfinite->run({1, 0, 0, 0, 1, 1}));
	EXPECT_FALSE(unselectSubsetLeftInfinite->run({1, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(unselectSubsetLeftInfinite->run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubsetLeftInfinite->run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(unselectSubsetLeftInfinite->run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(unselectSubsetLeftInfinite->run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(unselectSubsetLeftInfinite->run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(unselectSubsetLeftInfinite->run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_FALSE(unselectSubsetLeftInfinite->run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	EXPECT_FALSE(unselectSubsetLeftInfinite->run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//(.{3}*0) - i.e., checks the last character of strings with length 3n+1
	auto endsWith0 = interpret<2>(Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::lit(0)}));
	endsWith0->minimize();
	EXPECT_FALSE(endsWith0->isEmpty());
	EXPECT_TRUE(endsWith0->infinite());
	EXPECT_TRUE(endsWith0->run({0}));
	EXPECT_FALSE(endsWith0->run({1}));
	EXPECT_TRUE(endsWith0->run({0, 0, 0, 0}));
	EXPECT_TRUE(endsWith0->run({0, 0, 1, 0}));
	EXPECT_TRUE(endsWith0->run({0, 1, 1, 0}));
	EXPECT_FALSE(endsWith0->run({1, 0, 0, 1}));
	EXPECT_TRUE(endsWith0->run({1, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(endsWith0->run({1, 0, 0, 0, 0, 1, 0}));
	EXPECT_TRUE(endsWith0->run({1, 0, 0, 0, 1, 1, 0}));
	EXPECT_FALSE(endsWith0->run({1, 0, 0, 1, 0, 0, 1}));
	EXPECT_TRUE(endsWith0->run({0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(endsWith0->run({1, 0, 0, 0, 0, 0, 0, 0, 0, 1}));
	EXPECT_TRUE(endsWith0->run({0, 0, 0, 1, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(endsWith0->run({0, 0, 0, 0, 0, 0, 1, 0, 0, 1}));
	EXPECT_TRUE(endsWith0->run({1, 0, 0, 1, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(endsWith0->run({1, 0, 0, 0, 0, 0, 1, 0, 0, 1}));
	EXPECT_FALSE(endsWith0->run({0, 0, 0, 1, 0, 0, 1, 0, 0, 1}));
	EXPECT_FALSE(endsWith0->run({1, 0, 0, 1, 0, 0, 1, 0, 0, 1}));

	//exactly two of the three elements at positions {0, 3, 6} should be 1
	//(the intersection of the above regexes)
	auto subset = interpret<2>(Expr::conj({Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(1), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(1), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }), Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(0), Expr::lit(0), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }), }));
	EXPECT_FALSE(subset->isEmpty());
	EXPECT_FALSE(subset->infinite());
	EXPECT_FALSE(subset->run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(subset->run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(subset->run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(subset->run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(subset->run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(subset->run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	EXPECT_TRUE(subset->run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	EXPECT_FALSE(subset->run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//(.{3}*.{2}0.{3}*.{2}0.{3}*)
	auto atLeastTwoZeroes = interpret<2>(Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::repeat(Expr::any(), 2, 2), Expr::lit(0), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::repeat(Expr::any(), 2, 2), Expr::lit(0), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }));
	atLeastTwoZeroes->minimize();
	EXPECT_FALSE(atLeastTwoZeroes->isEmpty());
	EXPECT_TRUE(atLeastTwoZeroes->infinite());
	EXPECT_FALSE(atLeastTwoZeroes->run({}));
	EXPECT_TRUE(atLeastTwoZeroes->run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(atLeastTwoZeroes->run({0, 0, 0, 0, 0, 0, 0, 0, 1}));
	EXPECT_TRUE(atLeastTwoZeroes->run({0, 0, 0, 0, 0, 1, 0, 0, 0}));
	EXPECT_TRUE(atLeastTwoZeroes->run({0, 0, 1, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(atLeastTwoZeroes->run({0, 0, 0, 0, 0, 1, 0, 0, 1}));
	EXPECT_FALSE(atLeastTwoZeroes->run({0, 0, 1, 0, 0, 0, 0, 0, 1}));
	EXPECT_FALSE(atLeastTwoZeroes->run({0, 0, 1, 0, 0, 1, 0, 0, 0}));
	EXPECT_FALSE(atLeastTwoZeroes->run({0, 0, 1, 0, 0, 1, 0, 0, 1}));
}