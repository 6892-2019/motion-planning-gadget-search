#include "automaton.hpp"
#include <gtest/gtest.h>

using namespace automaton;
using namespace automaton::impl;

namespace {

//for custom assertion failure messages
//I couldn't get the compiler to find an operator<< overload, shrug
std::string to_string(const std::vector<unsigned int>& v) {
	std::string str;
    str += '(';
	auto i = v.cbegin();
	if (i != v.cend())
		str += std::to_string(*i);
    for (; i != v.cend(); ++i)
        str += std::to_string(*i);
    str += ')';
    return str;
}

std::vector<std::vector<unsigned int>> allStrings(unsigned int alphabetSize, unsigned int length) {
	assert(alphabetSize > 0);
	std::vector<std::vector<unsigned int>> ret;

	//Starting from the empty string, take all strings from the last generation
	//and append each possible symbol to them.
	auto start = ret.size();
	ret.emplace_back();
	for (unsigned int n = 1; n <= length; ++n) {
		auto end = ret.size();
		for (auto i = start; i < end; ++i) {
			for (unsigned int s = 0; s < alphabetSize; ++s) {
				//We can't just ret.push_back(ret[i]) because reallocation will
				//move ret[i] out from under us.
				auto copy = ret[i];
				copy.push_back(s);
				ret.push_back(std::move(copy));
			}
		}
		start = end;
	}
	return ret;
}

template<int AlphabetSize>
void equivalentOnAllStrings(typename Automaton<AlphabetSize>::const_ptr a, typename Automaton<AlphabetSize>::const_ptr b, int length) {
	for (auto& string : allStrings(AlphabetSize, length))
		EXPECT_EQ(a->run(string), b->run(string)) << to_string(string);
}

} //end anonymous namespace

TEST(AutomatonTest, AllStringsUtilFn) {
	auto s = allStrings(2, 0);
	EXPECT_EQ(s.size(), 1);
	EXPECT_TRUE(s[0].empty());

	s = allStrings(2, 1);
	EXPECT_EQ(s.size(), 3);
	auto sizeOne = [](const auto& v){return v.size() == 1;};
	EXPECT_EQ(std::count_if(s.begin(), s.end(), sizeOne), 2);

	s = allStrings(2, 2);
	EXPECT_EQ(s.size(), 7);
	EXPECT_EQ(std::count_if(s.begin(), s.end(), sizeOne), 2);
	auto sizeTwo = [](const auto& v){return v.size() == 2;};
	EXPECT_EQ(std::count_if(s.begin(), s.end(), sizeTwo), 4);

	s = allStrings(2, 3);
	EXPECT_EQ(s.size(), 15);
	auto sizeThree = [](const auto& v){return v.size() == 3;};
	EXPECT_EQ(std::count_if(s.begin(), s.end(), sizeThree), 8);
}

TEST(AutomatonTest, EquivOnAllStringsUtilFn) {
	equivalentOnAllStrings<2>(Automaton<2>::empty(), Automaton<2>::empty(), 8);
	equivalentOnAllStrings<2>(Automaton<2>::all(), Automaton<2>::all(), 8);
	equivalentOnAllStrings<2>(Automaton<2>::lit(1), Automaton<2>::lit(1), 8);
}

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

TEST(AutomatonTest, Clone) {
	auto a = Automaton<2>::range(Automaton<2>::lit(0), 2, 6);
	auto b = a->clone();
	equivalentOnAllStrings<2>(a, b, 8);
	auto d = Automaton<2>::cat({Automaton<2>::lit(0), Automaton<2>::lit(1)});
	auto e = Automaton<2>::cat({Automaton<2>::lit(1), Automaton<2>::lit(0)});
	auto f = Automaton<2>::alt({d, e});
	auto fc = f->clone();
	equivalentOnAllStrings<2>(f, fc, 8);
	auto g = Automaton<2>::conj(f, d);
	auto gc = g->clone();
	equivalentOnAllStrings<2>(g, gc, 8);
}

TEST(AutomatonTest, Determinize) {
	auto a = Automaton<2>::range(Automaton<2>::lit(0), 2, 6);
	auto b = a->clone();
	b->determinize();
	equivalentOnAllStrings<2>(a, b, 8);
	auto d = Automaton<2>::cat({Automaton<2>::lit(0), Automaton<2>::lit(1)});
	auto e = Automaton<2>::cat({Automaton<2>::lit(1), Automaton<2>::lit(0)});
	auto f = Automaton<2>::alt({d, e});
	auto fc = f->clone();
	fc->determinize();
	equivalentOnAllStrings<2>(f, fc, 8);
	auto g = Automaton<2>::conj(f, d);
	auto gc = g->clone();
	gc->determinize();
	equivalentOnAllStrings<2>(g, gc, 8);
}

TEST(AutomatonTest, Totalize) {
	auto a = Automaton<2>::range(Automaton<2>::lit(0), 2, 6);
	auto b = a->clone();
	b->totalize();
	equivalentOnAllStrings<2>(a, b, 8);
	auto d = Automaton<2>::cat({Automaton<2>::lit(0), Automaton<2>::lit(1)});
	auto e = Automaton<2>::cat({Automaton<2>::lit(1), Automaton<2>::lit(0)});
	auto f = Automaton<2>::alt({d, e});
	auto fc = f->clone();
	fc->totalize();
	equivalentOnAllStrings<2>(f, fc, 8);
	auto g = Automaton<2>::conj(f, d);
	auto gc = g->clone();
	gc->totalize();
	equivalentOnAllStrings<2>(g, gc, 8);
}

TEST(AutomatonTest, DeterminizeTotalize) {
	auto a = Automaton<2>::range(Automaton<2>::lit(0), 2, 6);
	auto b = a->clone();
	b->determinize();
	b->totalize();
	equivalentOnAllStrings<2>(a, b, 8);
	auto d = Automaton<2>::cat({Automaton<2>::lit(0), Automaton<2>::lit(1)});
	auto e = Automaton<2>::cat({Automaton<2>::lit(1), Automaton<2>::lit(0)});
	auto f = Automaton<2>::alt({d, e});
	auto fc = f->clone();
	fc->determinize();
	fc->totalize();
	equivalentOnAllStrings<2>(f, fc, 8);
	auto g = Automaton<2>::conj(f, d);
	auto gc = g->clone();
	gc->determinize();
	gc->totalize();
	equivalentOnAllStrings<2>(g, gc, 8);
}

TEST(AutomatonTest, TotalizeDeterminize) {
	auto a = Automaton<2>::range(Automaton<2>::lit(0), 2, 6);
	auto b = a->clone();
	b->totalize();
	b->determinize();
	equivalentOnAllStrings<2>(a, b, 8);
	auto d = Automaton<2>::cat({Automaton<2>::lit(0), Automaton<2>::lit(1)});
	auto e = Automaton<2>::cat({Automaton<2>::lit(1), Automaton<2>::lit(0)});
	auto f = Automaton<2>::alt({d, e});
	auto fc = f->clone();
	fc->totalize();
	fc->determinize();
	equivalentOnAllStrings<2>(f, fc, 8);
	auto g = Automaton<2>::conj(f, d);
	auto gc = g->clone();
	gc->totalize();
	gc->determinize();
	equivalentOnAllStrings<2>(g, gc, 8);
}