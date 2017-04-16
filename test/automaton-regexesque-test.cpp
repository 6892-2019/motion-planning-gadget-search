#include "precompiled.hpp"
#include "automaton.hpp"
#include "util.hpp"
#include <gtest/gtest.h>

using namespace automaton;

TEST(AutomatonTest, EmptyLanguage) {
	auto a = empty<2>();
	EXPECT_TRUE(a.deterministic());
	EXPECT_TRUE(a.isEmpty());
	EXPECT_FALSE(a.infinite());
	EXPECT_FALSE(a.run({}));
	EXPECT_FALSE(a.run({0}));
	EXPECT_FALSE(a.run({1}));
	EXPECT_FALSE(a.run({0, 1}));
	EXPECT_FALSE(a.run({1, 0}));
}

TEST(AutomatonTest, AllLanguage) {
	auto a = all<2>();
	EXPECT_TRUE(a.deterministic());
	EXPECT_FALSE(a.isEmpty());
	EXPECT_TRUE(a.infinite());
	EXPECT_TRUE(a.run({}));
	EXPECT_TRUE(a.run({0}));
	EXPECT_TRUE(a.run({1}));
	EXPECT_TRUE(a.run({0, 1}));
	EXPECT_TRUE(a.run({1, 0}));
}

TEST(AutomatonTest, Any) {
	auto a = any<2>();
	EXPECT_TRUE(a.deterministic());
	EXPECT_FALSE(a.isEmpty());
	EXPECT_FALSE(a.infinite());
	EXPECT_FALSE(a.run({}));
	EXPECT_TRUE(a.run({0}));
	EXPECT_TRUE(a.run({1}));
	EXPECT_FALSE(a.run({0, 1}));
	EXPECT_FALSE(a.run({1, 0}));
}

TEST(AutomatonTest, AnyTrinary) {
	auto a = any<3>();
	EXPECT_TRUE(a.deterministic());
	EXPECT_FALSE(a.isEmpty());
	EXPECT_FALSE(a.infinite());
	EXPECT_FALSE(a.run({}));
	EXPECT_TRUE(a.run({0}));
	EXPECT_TRUE(a.run({1}));
	EXPECT_TRUE(a.run({2}));
	EXPECT_FALSE(a.run({0, 1}));
	EXPECT_FALSE(a.run({1, 0}));
	EXPECT_FALSE(a.run({0, 1, 2}));
	EXPECT_FALSE(a.run({2, 1, 0}));
}

TEST(AutomatonTest, Epsilon) {
	auto a = epsilon<2>();
	EXPECT_TRUE(a.deterministic());
	EXPECT_FALSE(a.isEmpty());
	EXPECT_FALSE(a.infinite());
	EXPECT_TRUE(a.run({}));
	EXPECT_FALSE(a.run({0}));
	EXPECT_FALSE(a.run({1}));
	EXPECT_FALSE(a.run({0, 1}));
	EXPECT_FALSE(a.run({1, 0}));
}

TEST(AutomatonTest, Lit) {
	auto a = lit<2>(0);
	EXPECT_TRUE(a.deterministic());
	EXPECT_FALSE(a.isEmpty());
	EXPECT_FALSE(a.infinite());
	EXPECT_FALSE(a.run({}));
	EXPECT_TRUE(a.run({0}));
	EXPECT_FALSE(a.run({1}));
	EXPECT_FALSE(a.run({0, 1}));
	EXPECT_FALSE(a.run({1, 0}));

	a = lit<2>(1);
	EXPECT_TRUE(a.deterministic());
	EXPECT_FALSE(a.isEmpty());
	EXPECT_FALSE(a.infinite());
	EXPECT_FALSE(a.run({}));
	EXPECT_TRUE(a.run({1}));
	EXPECT_FALSE(a.run({0}));
	EXPECT_FALSE(a.run({0, 1}));
	EXPECT_FALSE(a.run({1, 0}));
}

TEST(AutomatonTest, Cat) {
	auto a = lit<2>(0);
	auto foo = cat<2>({a});
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
	EXPECT_FALSE(foo.run({}));
	EXPECT_TRUE(foo.run({0}));
	EXPECT_FALSE(foo.run({1}));
	EXPECT_FALSE(foo.run({0, 1}));
	EXPECT_FALSE(foo.run({1, 0}));

	auto b = lit<2>(1);
	auto c = lit<2>(0);

	foo = cat<2>({a, b, c});
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
	EXPECT_FALSE(foo.run({}));
	EXPECT_FALSE(foo.run({0}));
	EXPECT_FALSE(foo.run({1}));
	EXPECT_FALSE(foo.run({0, 1}));
	EXPECT_FALSE(foo.run({1, 0}));
	EXPECT_FALSE(foo.run({0, 0, 0}));
	EXPECT_FALSE(foo.run({0, 0, 1}));
	EXPECT_TRUE(foo.run({0, 1, 0}));
	EXPECT_FALSE(foo.run({0, 1, 1}));
	EXPECT_FALSE(foo.run({1, 0, 0}));
	EXPECT_FALSE(foo.run({1, 0, 1}));
	EXPECT_FALSE(foo.run({1, 1, 0}));
	EXPECT_FALSE(foo.run({1, 1, 1}));

	auto e = epsilon<2>();
	auto ecat = cat<2>({e});
	equivalentOnAllStrings<2>(e, ecat, 8, __LINE__);
	auto ee = cat<2>({e, e});
	equivalentOnAllStrings<2>(e, ee, 8, __LINE__);
	auto f = cat<2>({e, e, e, e, e});
	equivalentOnAllStrings<2>(e, f, 8, __LINE__);
}

TEST(AutomatonTest, Alt) {
	auto a = lit<2>(0);
	auto foo = alt<2>({a});
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
	EXPECT_FALSE(foo.run({}));
	EXPECT_TRUE(foo.run({0}));
	EXPECT_FALSE(foo.run({1}));
	EXPECT_FALSE(foo.run({0, 1}));
	EXPECT_FALSE(foo.run({1, 0}));

	auto b = lit<2>(1);
	foo = alt<2>({a, b});
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
	EXPECT_FALSE(foo.run({}));
	EXPECT_TRUE(foo.run({0}));
	EXPECT_TRUE(foo.run({1}));
	EXPECT_FALSE(foo.run({0, 1}));
	EXPECT_FALSE(foo.run({1, 0}));
	EXPECT_FALSE(foo.run({0, 0, 0}));
	EXPECT_FALSE(foo.run({0, 0, 1}));
	EXPECT_FALSE(foo.run({0, 1, 0}));
	EXPECT_FALSE(foo.run({0, 1, 1}));
	EXPECT_FALSE(foo.run({1, 0, 0}));
	EXPECT_FALSE(foo.run({1, 0, 1}));
	EXPECT_FALSE(foo.run({1, 1, 0}));
	EXPECT_FALSE(foo.run({1, 1, 1}));

	auto c = cat<2>({lit<2>(1), lit<2>(0)});
	foo = alt<2>({a, c});
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
	EXPECT_FALSE(foo.run({}));
	EXPECT_TRUE(foo.run({0}));
	EXPECT_FALSE(foo.run({1}));
	EXPECT_FALSE(foo.run({0, 1}));
	EXPECT_TRUE(foo.run({1, 0}));
	EXPECT_FALSE(foo.run({0, 0, 0}));
	EXPECT_FALSE(foo.run({0, 0, 1}));
	EXPECT_FALSE(foo.run({0, 1, 0}));
	EXPECT_FALSE(foo.run({0, 1, 1}));
	EXPECT_FALSE(foo.run({1, 0, 0}));
	EXPECT_FALSE(foo.run({1, 0, 1}));
	EXPECT_FALSE(foo.run({1, 1, 0}));
	EXPECT_FALSE(foo.run({1, 1, 1}));
}

TEST(AutomatonTest, CatAlt) {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, a, a});
	equivalentOnAllStrings<2>(foo, nCopies<2>(any<2>(), 3), 4, __LINE__);
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
}

TEST(AutomatonTest, CatAltLitAlt) {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, lit<2>(1), a});
	EXPECT_FALSE(foo.run({0, 0, 0}));
	EXPECT_FALSE(foo.run({0, 0, 1}));
	EXPECT_TRUE(foo.run({0, 1, 0}));
	EXPECT_TRUE(foo.run({0, 1, 1}));
	EXPECT_FALSE(foo.run({1, 0, 0}));
	EXPECT_FALSE(foo.run({1, 0, 1}));
	EXPECT_TRUE(foo.run({1, 1, 0}));
	EXPECT_TRUE(foo.run({1, 1, 1}));
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
}

TEST(AutomatonTest, CatAltLitAltLitAlt) {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, lit<2>(1), a, lit<2>(1), a});
	EXPECT_FALSE(foo.run({0, 0, 0, 1, 0}));
	EXPECT_FALSE(foo.run({0, 0, 1, 1, 0}));
	EXPECT_TRUE(foo.run({0, 1, 0, 1, 0}));
	EXPECT_TRUE(foo.run({0, 1, 1, 1, 0}));
	EXPECT_FALSE(foo.run({1, 0, 0, 1, 0}));
	EXPECT_FALSE(foo.run({1, 0, 1, 1, 0}));
	EXPECT_TRUE(foo.run({1, 1, 0, 1, 0}));
	EXPECT_TRUE(foo.run({1, 1, 1, 1, 0}));
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
}

TEST(AutomatonTest, Conj) {
	auto a = lit<2>(0);
	auto b = lit<2>(0);
	auto foo = conj<2>(a, b);
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
	EXPECT_FALSE(foo.run({}));
	EXPECT_TRUE(foo.run({0}));
	EXPECT_FALSE(foo.run({1}));
	EXPECT_FALSE(foo.run({0, 1}));
	EXPECT_FALSE(foo.run({1, 0}));

	auto c = lit<2>(1);
	foo = conj<2>(a, c);
	EXPECT_TRUE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
	EXPECT_FALSE(foo.run({}));
	EXPECT_FALSE(foo.run({0}));
	EXPECT_FALSE(foo.run({1}));
	EXPECT_FALSE(foo.run({0, 1}));
	EXPECT_FALSE(foo.run({1, 0}));

	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	foo = conj<2>(f, d);
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_FALSE(foo.infinite());
	EXPECT_FALSE(foo.run({}));
	EXPECT_FALSE(foo.run({0}));
	EXPECT_FALSE(foo.run({1}));
	EXPECT_TRUE(foo.run({0, 1}));
	EXPECT_FALSE(foo.run({1, 0}));
}

TEST(AutomatonTest, Star) {
	auto a = lit<2>(0);
	auto s = star<2>(a);
	EXPECT_TRUE(s.deterministic());
	EXPECT_FALSE(s.isEmpty());
	EXPECT_TRUE(s.infinite());
	EXPECT_TRUE(s.run({}));
	EXPECT_TRUE(s.run({0}));
	EXPECT_TRUE(s.run({0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0}));
	EXPECT_FALSE(s.run({1}));
	EXPECT_FALSE(s.run({0, 1}));
	EXPECT_FALSE(s.run({0, 0, 1}));
	EXPECT_FALSE(s.run({1, 0, 0}));
}

TEST(AutomatonTest, NCopies) {
	auto a = lit<2>(0);
	auto s = nCopies<2>(a, 3);
	EXPECT_FALSE(s.isEmpty());
	EXPECT_FALSE(s.infinite());
	EXPECT_FALSE(s.run({}));
	EXPECT_FALSE(s.run({0}));
	EXPECT_FALSE(s.run({0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({1}));
	EXPECT_FALSE(s.run({0, 1}));
	EXPECT_FALSE(s.run({0, 0, 1}));
	EXPECT_FALSE(s.run({1, 0, 0}));
}

TEST(AutomatonTest, StarNCopies) {
	//any multiple of 3, including 0
	auto s = star<2>(nCopies<2>(any<2>(), 3));
	EXPECT_FALSE(s.isEmpty());
	EXPECT_TRUE(s.infinite());
	EXPECT_TRUE(s.run({}));
	EXPECT_FALSE(s.run({0}));
	EXPECT_FALSE(s.run({1}));
	EXPECT_FALSE(s.run({0, 0}));
	EXPECT_FALSE(s.run({1, 1}));
	EXPECT_TRUE(s.run({0, 0, 0}));
	EXPECT_TRUE(s.run({1, 0, 1}));
	EXPECT_FALSE(s.run({0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(AutomatonTest, PlusNCopies) {
	//any multiple of 3 except 0
	auto s = plus<2>(nCopies<2>(any<2>(), 3));
	EXPECT_FALSE(s.isEmpty());
	EXPECT_TRUE(s.infinite());
	EXPECT_FALSE(s.run({}));
	EXPECT_FALSE(s.run({0}));
	EXPECT_FALSE(s.run({1}));
	EXPECT_FALSE(s.run({0, 0}));
	EXPECT_FALSE(s.run({1, 1}));
	EXPECT_TRUE(s.run({0, 0, 0}));
	EXPECT_TRUE(s.run({1, 0, 1}));
	EXPECT_FALSE(s.run({0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(AutomatonTest, NOrMore) {
	auto a = lit<2>(0);
	auto s = nOrMore<2>(a, 3);
	EXPECT_FALSE(s.isEmpty());
	EXPECT_TRUE(s.infinite());
	EXPECT_FALSE(s.run({}));
	EXPECT_FALSE(s.run({0}));
	EXPECT_FALSE(s.run({0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({1}));
	EXPECT_FALSE(s.run({0, 1}));
	EXPECT_FALSE(s.run({0, 0, 1}));
	EXPECT_FALSE(s.run({1, 0, 0}));
}

TEST(AutomatonTest, Range) {
	auto a = lit<2>(0);
	auto s = range<2>(a, 2, 6);
	EXPECT_FALSE(s.isEmpty());
	EXPECT_FALSE(s.infinite());
	EXPECT_FALSE(s.run({}));
	EXPECT_FALSE(s.run({0}));
	EXPECT_TRUE(s.run({0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({1}));
	EXPECT_FALSE(s.run({0, 1}));
	EXPECT_FALSE(s.run({0, 0, 1}));
	EXPECT_FALSE(s.run({1, 0, 0}));
}

TEST(AutomatonTest, Comp) {
	auto a = lit<2>(0);
	auto foo = comp<2>(a);
	EXPECT_FALSE(foo.isEmpty());
	EXPECT_TRUE(foo.infinite());
	EXPECT_TRUE(foo.run({}));
	EXPECT_FALSE(foo.run({0}));
	EXPECT_TRUE(foo.run({1}));
	EXPECT_TRUE(foo.run({0, 1, 0}));

	auto s = range<2>(a, 2, 6);
	s = comp<2>(s);
	EXPECT_FALSE(s.isEmpty());
	EXPECT_TRUE(s.infinite());
	EXPECT_TRUE(s.run({}));
	EXPECT_TRUE(s.run({0}));
	EXPECT_FALSE(s.run({0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0}));
	EXPECT_FALSE(s.run({0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(s.run({0, 0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(s.run({1}));
	EXPECT_TRUE(s.run({0, 1}));
	EXPECT_TRUE(s.run({0, 0, 1}));
	EXPECT_TRUE(s.run({1, 0, 0}));

	auto t = alt({lit<2>(0), lit<2>(1)});
	auto tc = comp(t);
	EXPECT_TRUE(tc.run({}));
	EXPECT_FALSE(tc.run({0}));
	EXPECT_FALSE(tc.run({1}));
	EXPECT_TRUE(tc.run({0, 0}));
	EXPECT_TRUE(tc.run({0, 1}));
	EXPECT_TRUE(tc.run({1, 0}));
	EXPECT_TRUE(tc.run({1, 1}));

	auto u = alt({cat({lit<2>(0), lit<2>(1)})});
	auto uc = comp(u);
	EXPECT_TRUE(uc.run({}));
	EXPECT_TRUE(uc.run({0}));
	EXPECT_TRUE(uc.run({1}));
	EXPECT_TRUE(uc.run({0, 0}));
	EXPECT_FALSE(uc.run({0, 1}));
	EXPECT_TRUE(uc.run({1, 0}));
	EXPECT_TRUE(uc.run({1, 1}));

	auto u3 = alt({cat({lit<2>(0), lit<2>(1)}), cat({lit<2>(0), lit<2>(1)}), cat({lit<2>(0), lit<2>(1)})});
	EXPECT_TRUE(u3.run({0, 1}));
	auto u3c = comp(u3);
	EXPECT_TRUE(u3c.run({}));
	EXPECT_TRUE(u3c.run({0}));
	EXPECT_TRUE(u3c.run({1}));
	EXPECT_TRUE(u3c.run({0, 0}));
	EXPECT_FALSE(u3c.run({0, 1}));
	EXPECT_TRUE(u3c.run({1, 0}));
	EXPECT_TRUE(u3c.run({1, 1}));
}