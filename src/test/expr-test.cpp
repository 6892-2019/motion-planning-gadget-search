// SPDX-License-Identifier: MIT
// Copyright 2018 Massachusetts Institute of Technology
// Copyright 2025 Jeffrey Bosboom
#include "precompiled.hpp"
#include "expr.hpp"
#include "automaton.hpp"
#include "regex.hpp" //for interpret(Expr::const_ptr)
#include <doctest/doctest.h>

using automaton::impl::Expr;
using automaton::impl::interpret;
using automaton::Automaton;

TEST_CASE("ExprTest_PathPuzzleColConstraint") {
	//((.{3}*(1|2).{2}.{3}*(1|2).{2}.{3}*)&.{9})
	auto selectSubset = interpret<3>(Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(2), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(2), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }));
	CHECK_UNARY_FALSE(selectSubset.isEmpty());
	CHECK_UNARY_FALSE(selectSubset.infinite());
	CHECK_UNARY_FALSE(selectSubset.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(selectSubset.run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(selectSubset.run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(selectSubset.run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(selectSubset.run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(selectSubset.run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(selectSubset.run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	CHECK_UNARY(selectSubset.run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//((.{3}*(0|0).{2}.{3}*)&.{9})
	auto unselectSubset = interpret<3>(Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(0), Expr::lit(0), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }));
	CHECK_UNARY_FALSE(unselectSubset.isEmpty());
	CHECK_UNARY_FALSE(unselectSubset.infinite());
	CHECK_UNARY(unselectSubset.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubset.run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubset.run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubset.run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(unselectSubset.run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubset.run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(unselectSubset.run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	CHECK_UNARY_FALSE(unselectSubset.run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//exactly two of the three elements at positions {0, 3, 6} should be 1 or 2
	//(the intersection of the above regexes)
	auto subset = interpret<3>(Expr::conj({Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(2), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(2), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }), Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(0), Expr::lit(0), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }), }));
	CHECK_UNARY_FALSE(subset.isEmpty());
	CHECK_UNARY_FALSE(subset.infinite());
	CHECK_UNARY_FALSE(subset.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(subset.run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(subset.run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(subset.run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(subset.run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(subset.run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(subset.run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	CHECK_UNARY_FALSE(subset.run({1, 0, 0, 1, 0, 0, 1, 0, 0}));
}

TEST_CASE("ExprTest_PathPuzzleColConstraintBinary") {
	//((.{3}*(1|1).{2}.{3}*(1|1).{2}.{3}*)&.{9})
	auto selectSubset = interpret<2>(Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(1), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(1), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }));
	CHECK_UNARY_FALSE(selectSubset.isEmpty());
	CHECK_UNARY_FALSE(selectSubset.infinite());
	CHECK_UNARY_FALSE(selectSubset.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(selectSubset.run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(selectSubset.run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(selectSubset.run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(selectSubset.run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(selectSubset.run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(selectSubset.run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	CHECK_UNARY(selectSubset.run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//((.{3}*(0|0).{2}.{3}*)&.{9})
	auto unselectSubset = interpret<2>(Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(0), Expr::lit(0), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }));
	CHECK_UNARY_FALSE(unselectSubset.isEmpty());
	CHECK_UNARY_FALSE(unselectSubset.infinite());
	CHECK_UNARY(unselectSubset.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubset.run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubset.run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubset.run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(unselectSubset.run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubset.run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(unselectSubset.run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	CHECK_UNARY_FALSE(unselectSubset.run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//(.{3}*0.{2}.{3}*)
	auto unselectSubsetInfinite = interpret<2>(Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::lit(0), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }));
	unselectSubsetInfinite.minimize();
	CHECK_UNARY_FALSE(unselectSubsetInfinite.isEmpty());
	CHECK_UNARY(unselectSubsetInfinite.infinite());
	CHECK_UNARY(unselectSubsetInfinite.run({0, 0, 0}));
	CHECK_UNARY(unselectSubsetInfinite.run({0, 0, 1}));
	CHECK_UNARY(unselectSubsetInfinite.run({0, 1, 1}));
	CHECK_UNARY_FALSE(unselectSubsetInfinite.run({1, 0, 0}));
	CHECK_UNARY(unselectSubsetInfinite.run({1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubsetInfinite.run({1, 0, 0, 0, 0, 1}));
	CHECK_UNARY(unselectSubsetInfinite.run({1, 0, 0, 0, 1, 1}));
	CHECK_UNARY_FALSE(unselectSubsetInfinite.run({1, 0, 0, 1, 0, 0}));
	CHECK_UNARY(unselectSubsetInfinite.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubsetInfinite.run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubsetInfinite.run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubsetInfinite.run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(unselectSubsetInfinite.run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubsetInfinite.run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(unselectSubsetInfinite.run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	CHECK_UNARY_FALSE(unselectSubsetInfinite.run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//(.{3}*0.{2}) - i.e., checks the last three characters of strings with length a multiple of three
	auto unselectSubsetLeftInfinite = interpret<2>(Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::lit(0), Expr::repeat(Expr::any(), 2, 2)}));
	unselectSubsetLeftInfinite.minimize();
	CHECK_UNARY_FALSE(unselectSubsetLeftInfinite.isEmpty());
	CHECK_UNARY(unselectSubsetLeftInfinite.infinite());
	CHECK_UNARY(unselectSubsetLeftInfinite.run({0, 0, 0}));
	CHECK_UNARY(unselectSubsetLeftInfinite.run({0, 0, 1}));
	CHECK_UNARY(unselectSubsetLeftInfinite.run({0, 1, 1}));
	CHECK_UNARY_FALSE(unselectSubsetLeftInfinite.run({1, 0, 0}));
	CHECK_UNARY(unselectSubsetLeftInfinite.run({1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubsetLeftInfinite.run({1, 0, 0, 0, 0, 1}));
	CHECK_UNARY(unselectSubsetLeftInfinite.run({1, 0, 0, 0, 1, 1}));
	CHECK_UNARY_FALSE(unselectSubsetLeftInfinite.run({1, 0, 0, 1, 0, 0}));
	CHECK_UNARY(unselectSubsetLeftInfinite.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubsetLeftInfinite.run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(unselectSubsetLeftInfinite.run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(unselectSubsetLeftInfinite.run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(unselectSubsetLeftInfinite.run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(unselectSubsetLeftInfinite.run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY_FALSE(unselectSubsetLeftInfinite.run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	CHECK_UNARY_FALSE(unselectSubsetLeftInfinite.run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//(.{3}*0) - i.e., checks the last character of strings with length 3n+1
	auto endsWith0 = interpret<2>(Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::lit(0)}));
	endsWith0.minimize();
	CHECK_UNARY_FALSE(endsWith0.isEmpty());
	CHECK_UNARY(endsWith0.infinite());
	CHECK_UNARY(endsWith0.run({0}));
	CHECK_UNARY_FALSE(endsWith0.run({1}));
	CHECK_UNARY(endsWith0.run({0, 0, 0, 0}));
	CHECK_UNARY(endsWith0.run({0, 0, 1, 0}));
	CHECK_UNARY(endsWith0.run({0, 1, 1, 0}));
	CHECK_UNARY_FALSE(endsWith0.run({1, 0, 0, 1}));
	CHECK_UNARY(endsWith0.run({1, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(endsWith0.run({1, 0, 0, 0, 0, 1, 0}));
	CHECK_UNARY(endsWith0.run({1, 0, 0, 0, 1, 1, 0}));
	CHECK_UNARY_FALSE(endsWith0.run({1, 0, 0, 1, 0, 0, 1}));
	CHECK_UNARY(endsWith0.run({0, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(endsWith0.run({1, 0, 0, 0, 0, 0, 0, 0, 0, 1}));
	CHECK_UNARY(endsWith0.run({0, 0, 0, 1, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(endsWith0.run({0, 0, 0, 0, 0, 0, 1, 0, 0, 1}));
	CHECK_UNARY(endsWith0.run({1, 0, 0, 1, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(endsWith0.run({1, 0, 0, 0, 0, 0, 1, 0, 0, 1}));
	CHECK_UNARY_FALSE(endsWith0.run({0, 0, 0, 1, 0, 0, 1, 0, 0, 1}));
	CHECK_UNARY_FALSE(endsWith0.run({1, 0, 0, 1, 0, 0, 1, 0, 0, 1}));

	//exactly two of the three elements at positions {0, 3, 6} should be 1
	//(the intersection of the above regexes)
	auto subset = interpret<2>(Expr::conj({Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(1), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(1), Expr::lit(1), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }), Expr::conj({Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::alt({Expr::lit(0), Expr::lit(0), }), Expr::repeat(Expr::any(), 2, 2), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }), Expr::repeat(Expr::any(), 9, 9), }), }));
	CHECK_UNARY_FALSE(subset.isEmpty());
	CHECK_UNARY_FALSE(subset.infinite());
	CHECK_UNARY_FALSE(subset.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(subset.run({1, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(subset.run({0, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(subset.run({0, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(subset.run({1, 0, 0, 1, 0, 0, 0, 0, 0}));
	CHECK_UNARY(subset.run({1, 0, 0, 0, 0, 0, 1, 0, 0}));
	CHECK_UNARY(subset.run({0, 0, 0, 1, 0, 0, 1, 0, 0}));
	CHECK_UNARY_FALSE(subset.run({1, 0, 0, 1, 0, 0, 1, 0, 0}));

	//(.{3}*.{2}0.{3}*.{2}0.{3}*)
	auto atLeastTwoZeroes = interpret<2>(Expr::cat({Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::repeat(Expr::any(), 2, 2), Expr::lit(0), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), Expr::repeat(Expr::any(), 2, 2), Expr::lit(0), Expr::repeat(Expr::repeat(Expr::any(), 3, 3), 0, Expr::unlimited), }));
	atLeastTwoZeroes.minimize();
	CHECK_UNARY_FALSE(atLeastTwoZeroes.isEmpty());
	CHECK_UNARY(atLeastTwoZeroes.infinite());
	CHECK_UNARY_FALSE(atLeastTwoZeroes.run({}));
	CHECK_UNARY(atLeastTwoZeroes.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(atLeastTwoZeroes.run({0, 0, 0, 0, 0, 0, 0, 0, 1}));
	CHECK_UNARY(atLeastTwoZeroes.run({0, 0, 0, 0, 0, 1, 0, 0, 0}));
	CHECK_UNARY(atLeastTwoZeroes.run({0, 0, 1, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(atLeastTwoZeroes.run({0, 0, 0, 0, 0, 1, 0, 0, 1}));
	CHECK_UNARY_FALSE(atLeastTwoZeroes.run({0, 0, 1, 0, 0, 0, 0, 0, 1}));
	CHECK_UNARY_FALSE(atLeastTwoZeroes.run({0, 0, 1, 0, 0, 1, 0, 0, 0}));
	CHECK_UNARY_FALSE(atLeastTwoZeroes.run({0, 0, 1, 0, 0, 1, 0, 0, 1}));
}