// SPDX-License-Identifier: MIT
// Copyright 2019 Massachusetts Institute of Technology
// Copyright 2025 Jeffrey Bosboom
#include "precompiled.hpp"
#include "proj_compare.hpp"
#include <doctest/doctest.h>

TEST_CASE("ProjLess00") {
	std::tuple<int> a(0), b(1);
	CHECK_UNARY(proj_less<0>()(a, b));
	CHECK_UNARY_FALSE(proj_less<0>()(b, a));
	CHECK_UNARY_FALSE(proj_less<0>()(a, a));
	CHECK_UNARY_FALSE(proj_less<0>()(b, b));
}

TEST_CASE("ProjLess01") {
	std::tuple<int, double> a(0, 0.0), b(1, 1.0);
	CHECK_UNARY(proj_less<0>()(a, b));
	CHECK_UNARY_FALSE(proj_less<0>()(b, a));
	CHECK_UNARY_FALSE(proj_less<0>()(a, a));
	CHECK_UNARY_FALSE(proj_less<0>()(b, b));
	CHECK_UNARY(proj_less<1>()(a, b));
	CHECK_UNARY_FALSE(proj_less<1>()(b, a));
	CHECK_UNARY_FALSE(proj_less<1>()(a, a));
	CHECK_UNARY_FALSE(proj_less<1>()(b, b));
}

TEST_CASE("ProjLess02") {
	std::pair<int, double> a(0, 0.0), b(1, 1.0);
	CHECK_UNARY(proj_less<0>()(a, b));
	CHECK_UNARY_FALSE(proj_less<0>()(b, a));
	CHECK_UNARY_FALSE(proj_less<0>()(a, a));
	CHECK_UNARY_FALSE(proj_less<0>()(b, b));
	CHECK_UNARY(proj_less<1>()(a, b));
	CHECK_UNARY_FALSE(proj_less<1>()(b, a));
	CHECK_UNARY_FALSE(proj_less<1>()(a, a));
	CHECK_UNARY_FALSE(proj_less<1>()(b, b));
}

TEST_CASE("ProjLess03") {
	std::tuple<int, int, int, int> a(0, 0, 0, 0), b(1, 1, 1, 1);
	CHECK_UNARY(proj_less<0, 3>()(a, b));
	CHECK_UNARY_FALSE(proj_less<0, 3>()(b, a));
	CHECK_UNARY_FALSE(proj_less<0, 3>()(a, a));
	CHECK_UNARY_FALSE(proj_less<0, 3>()(b, b));
}

TEST_CASE("CoordLessLeft00") {
	std::pair<int, double> a(0, 0.0);
	int b = 1;
	double c = 1.0;
	CHECK_UNARY(coord_less_left<0>()(a, b));
	CHECK_UNARY(coord_less_left<1>()(a, c));
}

TEST_CASE("CoordLessRight00") {
	std::pair<int, double> a(1, 1.0);
	int b = 0;
	double c = 0.0;
	CHECK_UNARY(coord_less_right<0>()(b, a));
	CHECK_UNARY(coord_less_right<1>()(c, a));
}
