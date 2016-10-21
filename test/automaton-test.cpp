#include "automaton.hpp"
#include <gtest/gtest.h>

using namespace automaton;
using namespace automaton::impl;

TEST(AutomatonTest, EmptyLanguage) {
	auto a = Automaton<2>::empty();
	EXPECT_TRUE(a->deterministic());
	EXPECT_FALSE(a->run({}));
	EXPECT_FALSE(a->run({0}));
	EXPECT_FALSE(a->run({1}));
	EXPECT_FALSE(a->run({0, 1}));
	EXPECT_FALSE(a->run({1, 0}));
}

TEST(AutomatonTest, AllLanguage) {
	auto a = Automaton<2>::all();
	EXPECT_TRUE(a->deterministic());
	EXPECT_TRUE(a->run({}));
	EXPECT_TRUE(a->run({0}));
	EXPECT_TRUE(a->run({1}));
	EXPECT_TRUE(a->run({0, 1}));
	EXPECT_TRUE(a->run({1, 0}));
}

TEST(AutomatonTest, Any) {
	auto a = Automaton<2>::any();
	EXPECT_TRUE(a->deterministic());
	EXPECT_FALSE(a->run({}));
	EXPECT_TRUE(a->run({0}));
	EXPECT_TRUE(a->run({1}));
	EXPECT_FALSE(a->run({0, 1}));
	EXPECT_FALSE(a->run({1, 0}));
}

TEST(AutomatonTest, Epsilon) {
	auto a = Automaton<2>::epsilon();
	EXPECT_TRUE(a->deterministic());
	EXPECT_TRUE(a->run({}));
	EXPECT_FALSE(a->run({0}));
	EXPECT_FALSE(a->run({1}));
	EXPECT_FALSE(a->run({0, 1}));
	EXPECT_FALSE(a->run({1, 0}));
}

TEST(AutomatonTest, Lit) {
	auto a = Automaton<2>::lit(0);
	EXPECT_TRUE(a->deterministic());
	EXPECT_FALSE(a->run({}));
	EXPECT_TRUE(a->run({0}));
	EXPECT_FALSE(a->run({1}));
	EXPECT_FALSE(a->run({0, 1}));
	EXPECT_FALSE(a->run({1, 0}));

	a = Automaton<2>::lit(1);
	EXPECT_TRUE(a->deterministic());
	EXPECT_FALSE(a->run({}));
	EXPECT_TRUE(a->run({1}));
	EXPECT_FALSE(a->run({0}));
	EXPECT_FALSE(a->run({0, 1}));
	EXPECT_FALSE(a->run({1, 0}));
}

TEST(AutomatonTest, Cat) {
	auto a = Automaton<2>::lit(0);
	auto cat = Automaton<2>::cat({a});
	EXPECT_FALSE(cat->run({}));
	EXPECT_TRUE(cat->run({0}));
	EXPECT_FALSE(cat->run({1}));
	EXPECT_FALSE(cat->run({0, 1}));
	EXPECT_FALSE(cat->run({1, 0}));

	auto b = Automaton<2>::lit(1);
	auto c = Automaton<2>::lit(0);

	cat = Automaton<2>::cat({a, b, c});
	EXPECT_FALSE(cat->run({}));
	EXPECT_FALSE(cat->run({0}));
	EXPECT_FALSE(cat->run({1}));
	EXPECT_FALSE(cat->run({0, 1}));
	EXPECT_FALSE(cat->run({1, 0}));
	EXPECT_FALSE(cat->run({0, 0, 0}));
	EXPECT_FALSE(cat->run({0, 0, 1}));
	EXPECT_TRUE(cat->run({0, 1, 0}));
	EXPECT_FALSE(cat->run({0, 1, 1}));
	EXPECT_FALSE(cat->run({1, 0, 0}));
	EXPECT_FALSE(cat->run({1, 0, 1}));
	EXPECT_FALSE(cat->run({1, 1, 0}));
	EXPECT_FALSE(cat->run({1, 1, 1}));
}

TEST(AutomatonTest, Alt) {
	auto a = Automaton<2>::lit(0);
	auto alt = Automaton<2>::alt({a});
	EXPECT_FALSE(alt->run({}));
	EXPECT_TRUE(alt->run({0}));
	EXPECT_FALSE(alt->run({1}));
	EXPECT_FALSE(alt->run({0, 1}));
	EXPECT_FALSE(alt->run({1, 0}));

	auto b = Automaton<2>::lit(1);
	alt = Automaton<2>::alt({a, b});
	EXPECT_FALSE(alt->run({}));
	EXPECT_TRUE(alt->run({0}));
	EXPECT_TRUE(alt->run({1}));
	EXPECT_FALSE(alt->run({0, 1}));
	EXPECT_FALSE(alt->run({1, 0}));
	EXPECT_FALSE(alt->run({0, 0, 0}));
	EXPECT_FALSE(alt->run({0, 0, 1}));
	EXPECT_FALSE(alt->run({0, 1, 0}));
	EXPECT_FALSE(alt->run({0, 1, 1}));
	EXPECT_FALSE(alt->run({1, 0, 0}));
	EXPECT_FALSE(alt->run({1, 0, 1}));
	EXPECT_FALSE(alt->run({1, 1, 0}));
	EXPECT_FALSE(alt->run({1, 1, 1}));

	auto c = Automaton<2>::cat({Automaton<2>::lit(1), Automaton<2>::lit(0)});
	alt = Automaton<2>::alt({a, c});
	EXPECT_FALSE(alt->run({}));
	EXPECT_TRUE(alt->run({0}));
	EXPECT_FALSE(alt->run({1}));
	EXPECT_FALSE(alt->run({0, 1}));
	EXPECT_TRUE(alt->run({1, 0}));
	EXPECT_FALSE(alt->run({0, 0, 0}));
	EXPECT_FALSE(alt->run({0, 0, 1}));
	EXPECT_FALSE(alt->run({0, 1, 0}));
	EXPECT_FALSE(alt->run({0, 1, 1}));
	EXPECT_FALSE(alt->run({1, 0, 0}));
	EXPECT_FALSE(alt->run({1, 0, 1}));
	EXPECT_FALSE(alt->run({1, 1, 0}));
	EXPECT_FALSE(alt->run({1, 1, 1}));
}

TEST(AutomatonTest, Conj) {
	auto a = Automaton<2>::lit(0);
	auto b = Automaton<2>::lit(0);
	auto cat = Automaton<2>::conj(a, b);
	EXPECT_FALSE(cat->run({}));
	EXPECT_TRUE(cat->run({0}));
	EXPECT_FALSE(cat->run({1}));
	EXPECT_FALSE(cat->run({0, 1}));
	EXPECT_FALSE(cat->run({1, 0}));

	auto c = Automaton<2>::lit(1);
	cat = Automaton<2>::conj(a, c);
	EXPECT_FALSE(cat->run({}));
	EXPECT_FALSE(cat->run({0}));
	EXPECT_FALSE(cat->run({1}));
	EXPECT_FALSE(cat->run({0, 1}));
	EXPECT_FALSE(cat->run({1, 0}));

	auto d = Automaton<2>::cat({Automaton<2>::lit(0), Automaton<2>::lit(1)});
	auto e = Automaton<2>::cat({Automaton<2>::lit(1), Automaton<2>::lit(0)});
	auto f = Automaton<2>::alt({d, e});
	cat = Automaton<2>::conj(f, d);
	EXPECT_FALSE(cat->run({}));
	EXPECT_FALSE(cat->run({0}));
	EXPECT_FALSE(cat->run({1}));
	EXPECT_TRUE(cat->run({0, 1}));
	EXPECT_FALSE(cat->run({1, 0}));
}

TEST(AutomatonTest, Star) {
	auto a = Automaton<2>::lit(0);
	auto s = Automaton<2>::star(a);
	EXPECT_TRUE(s->deterministic());
	EXPECT_TRUE(s->run({}));
	EXPECT_TRUE(s->run({0}));
	EXPECT_TRUE(s->run({0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0}));
	EXPECT_FALSE(s->run({1}));
	EXPECT_FALSE(s->run({0, 1}));
	EXPECT_FALSE(s->run({0, 0, 1}));
	EXPECT_FALSE(s->run({1, 0, 0}));
}

TEST(AutomatonTest, NCopies) {
	auto a = Automaton<2>::lit(0);
	auto s = Automaton<2>::nCopies(a, 3);
	EXPECT_FALSE(s->run({}));
	EXPECT_FALSE(s->run({0}));
	EXPECT_FALSE(s->run({0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0}));
	EXPECT_FALSE(s->run({0, 0, 0, 0}));
	EXPECT_FALSE(s->run({0, 0, 0, 0, 0}));
	EXPECT_FALSE(s->run({0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s->run({0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s->run({1}));
	EXPECT_FALSE(s->run({0, 1}));
	EXPECT_FALSE(s->run({0, 0, 1}));
	EXPECT_FALSE(s->run({1, 0, 0}));
}

TEST(AutomatonTest, NOrMore) {
	auto a = Automaton<2>::lit(0);
	auto s = Automaton<2>::nOrMore(a, 3);
	EXPECT_FALSE(s->run({}));
	EXPECT_FALSE(s->run({0}));
	EXPECT_FALSE(s->run({0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0, 0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0, 0, 0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s->run({1}));
	EXPECT_FALSE(s->run({0, 1}));
	EXPECT_FALSE(s->run({0, 0, 1}));
	EXPECT_FALSE(s->run({1, 0, 0}));
}

TEST(AutomatonTest, Range) {
	auto a = Automaton<2>::lit(0);
	auto s = Automaton<2>::range(a, 2, 6);
	EXPECT_FALSE(s->run({}));
	EXPECT_FALSE(s->run({0}));
	EXPECT_TRUE(s->run({0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0, 0, 0}));
	EXPECT_TRUE(s->run({0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s->run({0, 0, 0, 0, 0, 0, 0}));
	EXPECT_FALSE(s->run({1}));
	EXPECT_FALSE(s->run({0, 1}));
	EXPECT_FALSE(s->run({0, 0, 1}));
	EXPECT_FALSE(s->run({1, 0, 0}));
}