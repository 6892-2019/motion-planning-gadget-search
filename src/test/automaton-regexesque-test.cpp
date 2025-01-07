#include "precompiled.hpp"
#include "automaton.hpp"
#include "automaton-regexesque.hpp"
#include "util.hpp"
#include <doctest/doctest.h>

using namespace automaton;

TEST_CASE("AutomatonTest_EmptyLanguage") {
	auto a = empty<2>();
	CHECK_UNARY(a.deterministic());
	CHECK_UNARY(a.isEmpty());
	CHECK_UNARY_FALSE(a.infinite());
	CHECK_UNARY_FALSE(a.run({}));
	CHECK_UNARY_FALSE(a.run({0}));
	CHECK_UNARY_FALSE(a.run({1}));
	CHECK_UNARY_FALSE(a.run({0, 1}));
	CHECK_UNARY_FALSE(a.run({1, 0}));
}

TEST_CASE("AutomatonTest_AllLanguage") {
	auto a = all<2>();
	CHECK_UNARY(a.deterministic());
	CHECK_UNARY_FALSE(a.isEmpty());
	CHECK_UNARY(a.infinite());
	CHECK_UNARY(a.run({}));
	CHECK_UNARY(a.run({0}));
	CHECK_UNARY(a.run({1}));
	CHECK_UNARY(a.run({0, 1}));
	CHECK_UNARY(a.run({1, 0}));
}

TEST_CASE("AutomatonTest_Any") {
	auto a = any<2>();
	CHECK_UNARY(a.deterministic());
	CHECK_UNARY_FALSE(a.isEmpty());
	CHECK_UNARY_FALSE(a.infinite());
	CHECK_UNARY_FALSE(a.run({}));
	CHECK_UNARY(a.run({0}));
	CHECK_UNARY(a.run({1}));
	CHECK_UNARY_FALSE(a.run({0, 1}));
	CHECK_UNARY_FALSE(a.run({1, 0}));
}

TEST_CASE("AutomatonTest_AnyTrinary") {
	auto a = any<3>();
	CHECK_UNARY(a.deterministic());
	CHECK_UNARY_FALSE(a.isEmpty());
	CHECK_UNARY_FALSE(a.infinite());
	CHECK_UNARY_FALSE(a.run({}));
	CHECK_UNARY(a.run({0}));
	CHECK_UNARY(a.run({1}));
	CHECK_UNARY(a.run({2}));
	CHECK_UNARY_FALSE(a.run({0, 1}));
	CHECK_UNARY_FALSE(a.run({1, 0}));
	CHECK_UNARY_FALSE(a.run({0, 1, 2}));
	CHECK_UNARY_FALSE(a.run({2, 1, 0}));
}

TEST_CASE("AutomatonTest_Epsilon") {
	auto a = epsilon<2>();
	CHECK_UNARY(a.deterministic());
	CHECK_UNARY_FALSE(a.isEmpty());
	CHECK_UNARY_FALSE(a.infinite());
	CHECK_UNARY(a.run({}));
	CHECK_UNARY_FALSE(a.run({0}));
	CHECK_UNARY_FALSE(a.run({1}));
	CHECK_UNARY_FALSE(a.run({0, 1}));
	CHECK_UNARY_FALSE(a.run({1, 0}));
}

TEST_CASE("AutomatonTest_Lit") {
	auto a = lit<2>(0);
	CHECK_UNARY(a.deterministic());
	CHECK_UNARY_FALSE(a.isEmpty());
	CHECK_UNARY_FALSE(a.infinite());
	CHECK_UNARY_FALSE(a.run({}));
	CHECK_UNARY(a.run({0}));
	CHECK_UNARY_FALSE(a.run({1}));
	CHECK_UNARY_FALSE(a.run({0, 1}));
	CHECK_UNARY_FALSE(a.run({1, 0}));

	a = lit<2>(1);
	CHECK_UNARY(a.deterministic());
	CHECK_UNARY_FALSE(a.isEmpty());
	CHECK_UNARY_FALSE(a.infinite());
	CHECK_UNARY_FALSE(a.run({}));
	CHECK_UNARY(a.run({1}));
	CHECK_UNARY_FALSE(a.run({0}));
	CHECK_UNARY_FALSE(a.run({0, 1}));
	CHECK_UNARY_FALSE(a.run({1, 0}));
}

TEST_CASE("AutomatonTest_VarargLit") {
	auto a = lit<2>(0, 0, 0, 0, 0);
	CHECK_UNARY(a.deterministic());
	CHECK_UNARY_FALSE(a.isEmpty());
	CHECK_UNARY_FALSE(a.infinite());
	CHECK_UNARY(a.run({0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(a.run({}));
	CHECK_UNARY_FALSE(a.run({0, 0}));

	a = lit<2>(0, 0, 0, 1, 0);
	CHECK_UNARY(a.deterministic());
	CHECK_UNARY_FALSE(a.isEmpty());
	CHECK_UNARY_FALSE(a.infinite());
	CHECK_UNARY(a.run({0, 0, 0, 1, 0}));
	CHECK_UNARY_FALSE(a.run({}));
	CHECK_UNARY_FALSE(a.run({0, 0}));

	equivalentOnAllStrings<2>(lit<2>(), epsilon<2>(), 8, __LINE__);
}

TEST_CASE("AutomatonTest_VarargRun") {
	auto a = lit<2>(0, 0, 0, 1, 0);
	CHECK_UNARY(a.run(0, 0, 0, 1, 0));
	CHECK_UNARY_FALSE(a.run());
	CHECK_UNARY_FALSE(a.run(0, 0));
	CHECK_UNARY_FALSE(a.run(0, 0, 0, 0, 0));
}

TEST_CASE("AutomatonTest_Cat") {
	auto a = lit<2>(0);
	auto foo = cat<2>({a});
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
	CHECK_UNARY_FALSE(foo.run({}));
	CHECK_UNARY(foo.run({0}));
	CHECK_UNARY_FALSE(foo.run({1}));
	CHECK_UNARY_FALSE(foo.run({0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0}));

	auto b = lit<2>(1);
	auto c = lit<2>(0);

	foo = cat<2>({a, b, c});
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
	CHECK_UNARY_FALSE(foo.run({}));
	CHECK_UNARY_FALSE(foo.run({0}));
	CHECK_UNARY_FALSE(foo.run({1}));
	CHECK_UNARY_FALSE(foo.run({0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 1}));
	CHECK_UNARY(foo.run({0, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 1, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 1, 1}));

	auto e = epsilon<2>();
	auto ecat = cat<2>({e});
	equivalentOnAllStrings<2>(e, ecat, 8, __LINE__);
	auto ee = cat<2>({e, e});
	equivalentOnAllStrings<2>(e, ee, 8, __LINE__);
	auto f = cat<2>({e, e, e, e, e});
	equivalentOnAllStrings<2>(e, f, 8, __LINE__);
}

TEST_CASE("AutomatonTest_EmptyCatList") {
	auto a = cat<2>({});
	equivalentOnAllStrings<2>(a, epsilon<2>(), 8, __LINE__);
}

TEST_CASE("AutomatonTest_EmptyVarargCat") {
	auto a = cat<2>();
	equivalentOnAllStrings<2>(a, epsilon<2>(), 8, __LINE__);
}

TEST_CASE("AutomatonTest_SingleVarargCat") {
	auto a = cat<2>(lit<2>(0));
	equivalentOnAllStrings<2>(a, lit<2>(0), 8, __LINE__);
}

TEST_CASE("AutomatonTest_VarargCat") {
	auto a = lit<2>(0);
	auto b = lit<2>(1);
	auto foo = cat<2>(a, b, a);
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
	CHECK_UNARY_FALSE(foo.run({}));
	CHECK_UNARY_FALSE(foo.run({0}));
	CHECK_UNARY_FALSE(foo.run({1}));
	CHECK_UNARY_FALSE(foo.run({0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 1}));
	CHECK_UNARY(foo.run({0, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 1, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 1, 1}));
}

TEST_CASE("AutomatonTest_Alt") {
	auto a = lit<2>(0);
	auto foo = alt<2>({a});
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
	CHECK_UNARY_FALSE(foo.run({}));
	CHECK_UNARY(foo.run({0}));
	CHECK_UNARY_FALSE(foo.run({1}));
	CHECK_UNARY_FALSE(foo.run({0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0}));

	auto b = lit<2>(1);
	foo = alt<2>({a, b});
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
	CHECK_UNARY_FALSE(foo.run({}));
	CHECK_UNARY(foo.run({0}));
	CHECK_UNARY(foo.run({1}));
	CHECK_UNARY_FALSE(foo.run({0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 1}));
	CHECK_UNARY_FALSE(foo.run({0, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 1, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 1, 1}));

	auto c = cat<2>({lit<2>(1), lit<2>(0)});
	foo = alt<2>({a, c});
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
	CHECK_UNARY_FALSE(foo.run({}));
	CHECK_UNARY(foo.run({0}));
	CHECK_UNARY_FALSE(foo.run({1}));
	CHECK_UNARY_FALSE(foo.run({0, 1}));
	CHECK_UNARY(foo.run({1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 1}));
	CHECK_UNARY_FALSE(foo.run({0, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 1, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 1, 1}));
}

TEST_CASE("AutomatonTest_EmptyAltList") {
	auto a = alt<2>({});
	equivalentOnAllStrings<2>(a, empty<2>(), 8, __LINE__);
}

TEST_CASE("AutomatonTest_EmptyVarargAlt") {
	auto a = alt<2>();
	equivalentOnAllStrings<2>(a, empty<2>(), 8, __LINE__);
}

TEST_CASE("AutomatonTest_VarargAlt") {
	auto a = lit<2>(0);
	auto c = cat<2>({lit<2>(1), lit<2>(0)});
	auto foo = alt(a, c);
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
	CHECK_UNARY_FALSE(foo.run({}));
	CHECK_UNARY(foo.run({0}));
	CHECK_UNARY_FALSE(foo.run({1}));
	CHECK_UNARY_FALSE(foo.run({0, 1}));
	CHECK_UNARY(foo.run({1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 1}));
	CHECK_UNARY_FALSE(foo.run({0, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 1, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 1, 1}));
}

TEST_CASE("AutomatonTest_CatAlt") {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, a, a});
	equivalentOnAllStrings<2>(foo, nCopies<2>(any<2>(), 3), 4, __LINE__);
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
}

TEST_CASE("AutomatonTest_CatAltLitAlt") {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, lit<2>(1), a});
	CHECK_UNARY_FALSE(foo.run({0, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 1}));
	CHECK_UNARY(foo.run({0, 1, 0}));
	CHECK_UNARY(foo.run({0, 1, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 1}));
	CHECK_UNARY(foo.run({1, 1, 0}));
	CHECK_UNARY(foo.run({1, 1, 1}));
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
}

TEST_CASE("AutomatonTest_CatAltLitAltLitAlt") {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, lit<2>(1), a, lit<2>(1), a});
	CHECK_UNARY_FALSE(foo.run({0, 0, 0, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({0, 0, 1, 1, 0}));
	CHECK_UNARY(foo.run({0, 1, 0, 1, 0}));
	CHECK_UNARY(foo.run({0, 1, 1, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 0, 1, 0}));
	CHECK_UNARY_FALSE(foo.run({1, 0, 1, 1, 0}));
	CHECK_UNARY(foo.run({1, 1, 0, 1, 0}));
	CHECK_UNARY(foo.run({1, 1, 1, 1, 0}));
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
}

TEST_CASE("AutomatonTest_Conj") {
	auto a = lit<2>(0);
	auto b = lit<2>(0);
	auto foo = conj<2>(a, b);
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
	CHECK_UNARY_FALSE(foo.run({}));
	CHECK_UNARY(foo.run({0}));
	CHECK_UNARY_FALSE(foo.run({1}));
	CHECK_UNARY_FALSE(foo.run({0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0}));

	auto c = lit<2>(1);
	foo = conj<2>(a, c);
	CHECK_UNARY(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
	CHECK_UNARY_FALSE(foo.run({}));
	CHECK_UNARY_FALSE(foo.run({0}));
	CHECK_UNARY_FALSE(foo.run({1}));
	CHECK_UNARY_FALSE(foo.run({0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0}));

	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	foo = conj<2>(f, d);
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY_FALSE(foo.infinite());
	CHECK_UNARY_FALSE(foo.run({}));
	CHECK_UNARY_FALSE(foo.run({0}));
	CHECK_UNARY_FALSE(foo.run({1}));
	CHECK_UNARY(foo.run({0, 1}));
	CHECK_UNARY_FALSE(foo.run({1, 0}));
}

TEST_CASE("AutomatonTest_Star") {
	auto a = lit<2>(0);
	auto s = star<2>(a);
	CHECK_UNARY(s.deterministic());
	CHECK_UNARY_FALSE(s.isEmpty());
	CHECK_UNARY(s.infinite());
	CHECK_UNARY(s.run({}));
	CHECK_UNARY(s.run({0}));
	CHECK_UNARY(s.run({0, 0}));
	CHECK_UNARY(s.run({0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({1}));
	CHECK_UNARY_FALSE(s.run({0, 1}));
	CHECK_UNARY_FALSE(s.run({0, 0, 1}));
	CHECK_UNARY_FALSE(s.run({1, 0, 0}));
}

TEST_CASE("AutomatonTest_NCopies") {
	auto a = lit<2>(0);
	auto s = nCopies<2>(a, 3);
	CHECK_UNARY_FALSE(s.isEmpty());
	CHECK_UNARY_FALSE(s.infinite());
	CHECK_UNARY_FALSE(s.run({}));
	CHECK_UNARY_FALSE(s.run({0}));
	CHECK_UNARY_FALSE(s.run({0, 0}));
	CHECK_UNARY(s.run({0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({1}));
	CHECK_UNARY_FALSE(s.run({0, 1}));
	CHECK_UNARY_FALSE(s.run({0, 0, 1}));
	CHECK_UNARY_FALSE(s.run({1, 0, 0}));
}

TEST_CASE("AutomatonTest_StarNCopies") {
	//any multiple of 3, including 0
	auto s = star<2>(nCopies<2>(any<2>(), 3));
	CHECK_UNARY_FALSE(s.isEmpty());
	CHECK_UNARY(s.infinite());
	CHECK_UNARY(s.run({}));
	CHECK_UNARY_FALSE(s.run({0}));
	CHECK_UNARY_FALSE(s.run({1}));
	CHECK_UNARY_FALSE(s.run({0, 0}));
	CHECK_UNARY_FALSE(s.run({1, 1}));
	CHECK_UNARY(s.run({0, 0, 0}));
	CHECK_UNARY(s.run({1, 0, 1}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST_CASE("AutomatonTest_PlusNCopies") {
	//any multiple of 3 except 0
	auto s = plus<2>(nCopies<2>(any<2>(), 3));
	CHECK_UNARY_FALSE(s.isEmpty());
	CHECK_UNARY(s.infinite());
	CHECK_UNARY_FALSE(s.run({}));
	CHECK_UNARY_FALSE(s.run({0}));
	CHECK_UNARY_FALSE(s.run({1}));
	CHECK_UNARY_FALSE(s.run({0, 0}));
	CHECK_UNARY_FALSE(s.run({1, 1}));
	CHECK_UNARY(s.run({0, 0, 0}));
	CHECK_UNARY(s.run({1, 0, 1}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST_CASE("AutomatonTest_NOrMore") {
	auto a = lit<2>(0);
	auto s = nOrMore<2>(a, 3);
	CHECK_UNARY_FALSE(s.isEmpty());
	CHECK_UNARY(s.infinite());
	CHECK_UNARY_FALSE(s.run({}));
	CHECK_UNARY_FALSE(s.run({0}));
	CHECK_UNARY_FALSE(s.run({0, 0}));
	CHECK_UNARY(s.run({0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({1}));
	CHECK_UNARY_FALSE(s.run({0, 1}));
	CHECK_UNARY_FALSE(s.run({0, 0, 1}));
	CHECK_UNARY_FALSE(s.run({1, 0, 0}));
}

TEST_CASE("AutomatonTest_Range") {
	auto a = lit<2>(0);
	auto s = range<2>(a, 2, 6);
	CHECK_UNARY_FALSE(s.isEmpty());
	CHECK_UNARY_FALSE(s.infinite());
	CHECK_UNARY_FALSE(s.run({}));
	CHECK_UNARY_FALSE(s.run({0}));
	CHECK_UNARY(s.run({0, 0}));
	CHECK_UNARY(s.run({0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({1}));
	CHECK_UNARY_FALSE(s.run({0, 1}));
	CHECK_UNARY_FALSE(s.run({0, 0, 1}));
	CHECK_UNARY_FALSE(s.run({1, 0, 0}));
}

TEST_CASE("AutomatonTest_Comp") {
	auto a = lit<2>(0);
	auto foo = comp<2>(a);
	CHECK_UNARY_FALSE(foo.isEmpty());
	CHECK_UNARY(foo.infinite());
	CHECK_UNARY(foo.run({}));
	CHECK_UNARY_FALSE(foo.run({0}));
	CHECK_UNARY(foo.run({1}));
	CHECK_UNARY(foo.run({0, 1, 0}));

	auto s = range<2>(a, 2, 6);
	s = comp<2>(s);
	CHECK_UNARY_FALSE(s.isEmpty());
	CHECK_UNARY(s.infinite());
	CHECK_UNARY(s.run({}));
	CHECK_UNARY(s.run({0}));
	CHECK_UNARY_FALSE(s.run({0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0}));
	CHECK_UNARY_FALSE(s.run({0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(s.run({0, 0, 0, 0, 0, 0, 0}));
	CHECK_UNARY(s.run({1}));
	CHECK_UNARY(s.run({0, 1}));
	CHECK_UNARY(s.run({0, 0, 1}));
	CHECK_UNARY(s.run({1, 0, 0}));

	auto t = alt({lit<2>(0), lit<2>(1)});
	auto tc = comp(t);
	CHECK_UNARY(tc.run({}));
	CHECK_UNARY_FALSE(tc.run({0}));
	CHECK_UNARY_FALSE(tc.run({1}));
	CHECK_UNARY(tc.run({0, 0}));
	CHECK_UNARY(tc.run({0, 1}));
	CHECK_UNARY(tc.run({1, 0}));
	CHECK_UNARY(tc.run({1, 1}));

	auto u = alt({cat({lit<2>(0), lit<2>(1)})});
	auto uc = comp(u);
	CHECK_UNARY(uc.run({}));
	CHECK_UNARY(uc.run({0}));
	CHECK_UNARY(uc.run({1}));
	CHECK_UNARY(uc.run({0, 0}));
	CHECK_UNARY_FALSE(uc.run({0, 1}));
	CHECK_UNARY(uc.run({1, 0}));
	CHECK_UNARY(uc.run({1, 1}));

	auto u3 = alt({cat({lit<2>(0), lit<2>(1)}), cat({lit<2>(0), lit<2>(1)}), cat({lit<2>(0), lit<2>(1)})});
	CHECK_UNARY(u3.run({0, 1}));
	auto u3c = comp(u3);
	CHECK_UNARY(u3c.run({}));
	CHECK_UNARY(u3c.run({0}));
	CHECK_UNARY(u3c.run({1}));
	CHECK_UNARY(u3c.run({0, 0}));
	CHECK_UNARY_FALSE(u3c.run({0, 1}));
	CHECK_UNARY(u3c.run({1, 0}));
	CHECK_UNARY(u3c.run({1, 1}));
}

TEST_CASE("AutomatonTest_AltCatVsCatMaybe") {
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto foo = alt(ltr, cat(ltr, rtl));
	auto bar = cat(ltr, maybe(rtl));
	equivalentOnAllStrings(foo, bar, 4, __LINE__);
}

TEST_CASE("AutomatonTest_DeducedCatViaBase") {
	Automaton<2> foo = lit<2>(0);
	AutomatonBase& r = foo;
	const AutomatonBase& cr = foo;
	auto golden = cat(foo, foo, foo);
	equivalentOnAllStrings(cat(foo, r, cr), golden, 8, __LINE__);
	equivalentOnAllStrings(cat(r, foo, cr), golden, 8, __LINE__);
	equivalentOnAllStrings(cat(r, cr, foo), golden, 8, __LINE__);

	equivalentOnAllStrings(cat(foo, r, foo), golden, 8, __LINE__);
	equivalentOnAllStrings(cat(foo, cr, foo), golden, 8, __LINE__);
	equivalentOnAllStrings(cat(r, foo, foo), golden, 8, __LINE__);
	equivalentOnAllStrings(cat(cr, foo, foo), golden, 8, __LINE__);

	equivalentOnAllStrings(cat(foo, r, lit<2>(0)), golden, 8, __LINE__);
	equivalentOnAllStrings(cat(foo, cr, lit<2>(0)), golden, 8, __LINE__);
	equivalentOnAllStrings(cat(lit<2>(0), foo, r), golden, 8, __LINE__);
	equivalentOnAllStrings(cat(lit<2>(0), foo, cr), golden, 8, __LINE__);
	equivalentOnAllStrings(cat(r, lit<2>(0), foo), golden, 8, __LINE__);
	equivalentOnAllStrings(cat(cr, lit<2>(0), foo), golden, 8, __LINE__);
}

TEST_CASE("AutomatonTest_DeducibleButSpecifiedCatViaBase") {
	Automaton<2> foo = lit<2>(0);
	AutomatonBase& r = foo;
	const AutomatonBase& cr = foo;
	auto golden = cat(foo, foo, foo);
	equivalentOnAllStrings(cat<2>(foo, r, cr), golden, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(r, foo, cr), golden, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(r, cr, foo), golden, 8, __LINE__);

	equivalentOnAllStrings(cat<2>(foo, r, foo), golden, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(foo, cr, foo), golden, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(r, foo, foo), golden, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(cr, foo, foo), golden, 8, __LINE__);

	equivalentOnAllStrings(cat<2>(foo, r, lit<2>(0)), golden, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(foo, cr, lit<2>(0)), golden, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(lit<2>(0), foo, r), golden, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(lit<2>(0), foo, cr), golden, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(r, lit<2>(0), foo), golden, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(cr, lit<2>(0), foo), golden, 8, __LINE__);
}

TEST_CASE("AutomatonTest_SpecifiedCatViaBase") {
	Automaton<2> foo = lit<2>(0);
	AutomatonBase& r = foo;
	const AutomatonBase& cr = foo;
	auto golden2 = cat(foo, foo), golden3 = cat(foo, foo, foo);
	equivalentOnAllStrings(cat<2>(r, r), golden2, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(r, cr), golden2, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(cr, r), golden2, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(cr, cr), golden2, 8, __LINE__);

	equivalentOnAllStrings(cat<2>(r, lit<2>(0)), golden2, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(cr, lit<2>(0)), golden2, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(lit<2>(0), r), golden2, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(lit<2>(0), cr), golden2, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(r, lit<2>(0), cr), golden3, 8, __LINE__);
	equivalentOnAllStrings(cat<2>(cr, lit<2>(0), r), golden3, 8, __LINE__);
}

TEST_CASE("AutomatonTest_DeducedAltViaBase") {
	Automaton<4> a = lit<4>(0), b = lit<4>(1), c = lit<4>(2);
	AutomatonBase& ra = a;
	AutomatonBase& rb = b;
	AutomatonBase& rc = c;
	const AutomatonBase& cra = a;
	const AutomatonBase& crb = b;
	const AutomatonBase& crc = c;
	auto golden = alt(a, b, c);
	equivalentOnAllStrings(alt(a, rb, crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(ra, b, crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(ra, crb, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(ra, rb, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(cra, crb, c), golden, 4, __LINE__);

	equivalentOnAllStrings(alt(ra, b, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(a, rb, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(a, b, rc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(cra, b, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(a, crb, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(a, b, crc), golden, 4, __LINE__);

	equivalentOnAllStrings(alt(lit<4>(0), rb, rc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(ra, lit<4>(1), rc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(ra, rb, lit<4>(2)), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(lit<4>(0), crb, crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(cra, lit<4>(1), crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(cra, crb, lit<4>(2)), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(lit<4>(0), rb, crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(ra, lit<4>(1), crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(ra, crb, lit<4>(2)), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(lit<4>(0), crb, rc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(cra, lit<4>(1), rc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt(cra, rb, lit<4>(2)), golden, 4, __LINE__);
}

TEST_CASE("AutomatonTest_DeducibleButSpecifiedAltViaBase") {
	Automaton<4> a = lit<4>(0), b = lit<4>(1), c = lit<4>(2);
	AutomatonBase& ra = a;
	AutomatonBase& rb = b;
	AutomatonBase& rc = c;
	const AutomatonBase& cra = a;
	const AutomatonBase& crb = b;
	const AutomatonBase& crc = c;
	auto golden = alt(a, b, c);
	equivalentOnAllStrings(alt<4>(a, rb, crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(ra, b, crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(ra, crb, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(ra, rb, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(cra, crb, c), golden, 4, __LINE__);

	equivalentOnAllStrings(alt<4>(ra, b, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(a, rb, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(a, b, rc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(cra, b, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(a, crb, c), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(a, b, crc), golden, 4, __LINE__);

	equivalentOnAllStrings(alt<4>(lit<4>(0), rb, rc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(ra, lit<4>(1), rc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(ra, rb, lit<4>(2)), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(lit<4>(0), crb, crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(cra, lit<4>(1), crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(cra, crb, lit<4>(2)), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(lit<4>(0), rb, crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(ra, lit<4>(1), crc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(ra, crb, lit<4>(2)), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(lit<4>(0), crb, rc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(cra, lit<4>(1), rc), golden, 4, __LINE__);
	equivalentOnAllStrings(alt<4>(cra, rb, lit<4>(2)), golden, 4, __LINE__);
}