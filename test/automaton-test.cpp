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
    for (auto i = v.cbegin(); i != v.cend(); ++i)
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

/**
 * Asserts that the given automata are equivalent on all strings by checking all
 * strings up to the given length, and by other tests.  (Do not call this method
 * if the automata may differ on longer strings.)
 */
template<int AlphabetSize>
void equivalentOnAllStrings(const Automaton<AlphabetSize>& a, const Automaton<AlphabetSize>& b, int length, int lineno = -1) {
	for (auto& string : allStrings(AlphabetSize, length))
		EXPECT_EQ(a.run(string), b.run(string)) << to_string(string) << " from line " << lineno;
	//TODO: make these const
//	EXPECT_EQ(a.isEmpty(), b.isEmpty()) << " from line " << lineno;
//	EXPECT_EQ(a.infinite(), b.infinite()) << " from line " << lineno;
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
	equivalentOnAllStrings<2>(empty<2>(), empty<2>(), 8);
	equivalentOnAllStrings<2>(all<2>(), all<2>(), 8);
	equivalentOnAllStrings<2>(lit<2>(1), lit<2>(1), 8);
}

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
}

TEST(AutomatonTest, Clone) {
	auto a = range<2>(lit<2>(0), 2, 6);
	auto b = a; //clone
	equivalentOnAllStrings<2>(a, b, 8);
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	auto fc = f; //clone
	equivalentOnAllStrings<2>(f, fc, 8);
	auto g = conj<2>(f, d);
	auto gc = g; //clone
	equivalentOnAllStrings<2>(g, gc, 8);
}

TEST(AutomatonTest, Determinize) {
	auto a = range<2>(lit<2>(0), 2, 6);
	auto b = a; //clone
	b.determinize();
	equivalentOnAllStrings<2>(a, b, 8);
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	auto fc = f; //clone
	fc.determinize();
	equivalentOnAllStrings<2>(f, fc, 8);
	auto g = conj<2>(f, d);
	auto gc = g; //clone
	gc.determinize();
	equivalentOnAllStrings<2>(g, gc, 8);

	auto multOf3 = star<2>(nCopies<2>(any<2>(), 3));
	auto multOf3Clone = multOf3; //clone
	multOf3Clone.determinize();
	equivalentOnAllStrings<2>(multOf3, multOf3Clone, 8, __LINE__);

	auto finiteMultOf3 = conj<2>(multOf3, nCopies<2>(any<2>(), 6));
	auto finiteMultOf3Clone = finiteMultOf3; //clone
	finiteMultOf3Clone.determinize();
	equivalentOnAllStrings<2>(finiteMultOf3, finiteMultOf3Clone, 8, __LINE__);

	auto posMultOf3 = plus<2>(nCopies<2>(any<2>(), 3));
	auto posMultOf3Clone = posMultOf3; //clone
	posMultOf3Clone.determinize();
	equivalentOnAllStrings<2>(posMultOf3, posMultOf3Clone, 8, __LINE__);

	auto finitePosMultOf3 = conj<2>(posMultOf3, nCopies<2>(any<2>(), 6));
	auto finitePosMultOf3Clone = finitePosMultOf3; //clone
	finitePosMultOf3Clone.determinize();
	equivalentOnAllStrings<2>(finitePosMultOf3, finitePosMultOf3Clone, 8, __LINE__);
}

TEST(AutomatonTest, Totalize) {
	auto a = range<2>(lit<2>(0), 2, 6);
	auto b = a; //clone
	b.totalize();
	equivalentOnAllStrings<2>(a, b, 8);
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	auto fc = f; //clone
	fc.totalize();
	equivalentOnAllStrings<2>(f, fc, 8);
	auto g = conj<2>(f, d);
	auto gc = g; //clone
	gc.totalize();
	equivalentOnAllStrings<2>(g, gc, 8);
}

TEST(AutomatonTest, DeterminizeTotalize) {
	auto a = range<2>(lit<2>(0), 2, 6);
	auto b = a; //clone
	b.determinize();
	b.totalize();
	equivalentOnAllStrings<2>(a, b, 8);
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	auto fc = f; //clone
	fc.determinize();
	fc.totalize();
	equivalentOnAllStrings<2>(f, fc, 8);
	auto g = conj<2>(f, d);
	auto gc = g; //clone
	gc.determinize();
	gc.totalize();
	equivalentOnAllStrings<2>(g, gc, 8);
}

TEST(AutomatonTest, TotalizeDeterminize) {
	auto a = range<2>(lit<2>(0), 2, 6);
	auto b = a; //clone
	b.totalize();
	b.determinize();
	equivalentOnAllStrings<2>(a, b, 8);
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	auto fc = f; //clone
	fc.totalize();
	fc.determinize();
	equivalentOnAllStrings<2>(f, fc, 8);
	auto g = conj<2>(f, d);
	auto gc = g; //clone
	gc.totalize();
	gc.determinize();
	equivalentOnAllStrings<2>(g, gc, 8);
}

TEST(AutomatonTest, RemoveDeadStates) {
	auto p = lit<2>(0);
	auto q = p; //clone
	q.removeDeadStates();
	equivalentOnAllStrings<2>(p, q, 8, __LINE__);
	p = cat<2>({lit<2>(0), lit<2>(1)});
	q = p; //clone
	q.removeDeadStates();
	equivalentOnAllStrings<2>(p, q, 8, __LINE__);
	auto a = range<2>(lit<2>(0), 2, 6);
	auto b = a; //clone
	b.removeDeadStates();
	equivalentOnAllStrings<2>(a, b, 8, __LINE__);
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	auto fc = f; //clone
	fc.removeDeadStates();
	equivalentOnAllStrings<2>(f, fc, 8, __LINE__);
	auto g = conj<2>(f, d);
	auto gc = g; //clone
	gc.removeDeadStates();
	equivalentOnAllStrings<2>(g, gc, 8, __LINE__);
	auto h = comp<2>(lit<2>(0));
	auto hc = h; //clone
	hc.removeDeadStates();
	equivalentOnAllStrings<2>(h, hc, 8, __LINE__);
}

TEST(AutomatonTest, DeterminizeRemoveDeadStates) {
	auto a = range<2>(lit<2>(0), 2, 6);
	auto b = a; //clone
	b.determinize();
	b.removeDeadStates();
	equivalentOnAllStrings<2>(a, b, 8, __LINE__);
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	auto fc = f; //clone
	fc.determinize();
	fc.removeDeadStates();
	equivalentOnAllStrings<2>(f, fc, 8, __LINE__);
	auto g = conj<2>(f, d);
	auto gc = g; //clone
	gc.determinize();
	gc.removeDeadStates();
	equivalentOnAllStrings<2>(g, gc, 8, __LINE__);
}

TEST(AutomatonTest, Minimize) {
	auto p = lit<2>(0);
	auto q = p; //clone
	q.minimize();
	equivalentOnAllStrings<2>(p, q, 8, __LINE__);
	p = cat<2>({lit<2>(0), lit<2>(1)});
	q = p; //clone
	q.minimize();
	equivalentOnAllStrings<2>(p, q, 8, __LINE__);
	auto a = range<2>(lit<2>(0), 2, 6);
	auto b = a; //clone
	b.minimize();
	equivalentOnAllStrings<2>(a, b, 8, __LINE__);
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	auto fc = f; //clone
	fc.minimize();
	equivalentOnAllStrings<2>(f, fc, 8, __LINE__);
	auto g = conj<2>(f, d);
	auto gc = g; //clone
	gc.minimize();
	equivalentOnAllStrings<2>(g, gc, 8, __LINE__);
	auto h = comp<2>(lit<2>(0));
	auto hc = h; //clone
	hc.minimize();
	equivalentOnAllStrings<2>(h, hc, 8, __LINE__);
	h = all<2>();
	hc = h; //clone
	hc.minimize();
	equivalentOnAllStrings<2>(h, hc, 8, __LINE__);
	h = empty<2>();
	hc = h; //clone
	hc.minimize();
	equivalentOnAllStrings<2>(h, hc, 8, __LINE__);
	h = alt<2>({d, d, d, d, d, d, d});
	hc = h; //clone
	hc.minimize();
	equivalentOnAllStrings<2>(h, hc, 8, __LINE__);

	auto multOf3 = star<2>(nCopies<2>(any<2>(), 3));
	auto multOf3Clone = multOf3; //clone
	multOf3Clone.minimize();
	equivalentOnAllStrings<2>(multOf3, multOf3Clone, 8, __LINE__);

	auto multOf3Zero = cat<2>({star<2>(nCopies<2>(any<2>(), 3)), lit<2>(0)});
	auto multOf3ZeroClone = multOf3Zero; //clone
	multOf3ZeroClone.minimize();
	equivalentOnAllStrings<2>(multOf3Zero, multOf3ZeroClone, 8, __LINE__);

	auto multOf3One = cat<2>({star<2>(nCopies<2>(any<2>(), 3)), lit<2>(1)});
	auto multOf3OneClone = multOf3One; //clone
	multOf3OneClone.minimize();
	equivalentOnAllStrings<2>(multOf3One, multOf3OneClone, 8, __LINE__);

	auto finiteMultOf3 = conj<2>(multOf3, nCopies<2>(any<2>(), 6));
	auto finiteMultOf3Clone = finiteMultOf3; //clone
	finiteMultOf3Clone.minimize();
	equivalentOnAllStrings<2>(finiteMultOf3, finiteMultOf3Clone, 8, __LINE__);

	auto posMultOf3 = plus<2>(nCopies<2>(any<2>(), 3));
	auto posMultOf3Clone = posMultOf3; //clone
	posMultOf3Clone.minimize();
	equivalentOnAllStrings<2>(posMultOf3, posMultOf3Clone, 8, __LINE__);

	auto finitePosMultOf3 = conj<2>(posMultOf3, nCopies<2>(any<2>(), 6));
	auto finitePosMultOf3Clone = finitePosMultOf3; //clone
	finitePosMultOf3Clone.minimize();
	equivalentOnAllStrings<2>(finitePosMultOf3, finitePosMultOf3Clone, 8, __LINE__);

	auto zeroStarOne = cat<2>({star<2>(lit<2>(0)), lit<2>(1)});
	auto zeroStarOneClone = zeroStarOne; //clone
	zeroStarOneClone.minimize();
	equivalentOnAllStrings<2>(zeroStarOne, zeroStarOneClone, 8, __LINE__);

	auto zeroZeroStarOne = cat<2>({star<2>(cat<2>({lit<2>(0), lit<2>(0)})), lit<2>(1)});
	auto zeroZeroStarOneClone = zeroZeroStarOne; //clone
	zeroZeroStarOneClone.minimize();
	equivalentOnAllStrings<2>(zeroZeroStarOne, zeroZeroStarOneClone, 8, __LINE__);

	auto zeroOneStarOne = cat<2>({star<2>(cat<2>({lit<2>(0), lit<2>(1)})), lit<2>(1)});
	auto zeroOneStarOneClone = zeroOneStarOne; //clone
	zeroOneStarOneClone.minimize();
	equivalentOnAllStrings<2>(zeroOneStarOne, zeroOneStarOneClone, 8, __LINE__);
}

TEST(AutomatonTest, CatAltMinimize) {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, a, a});
	auto catClone = foo; //clone
	catClone.minimize();
	equivalentOnAllStrings<2>(foo, catClone, 8, __LINE__);
}

TEST(AutomatonTest, CatAltLitAltMinimize) {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, lit<2>(1), a});
	auto catClone = foo; //clone
	catClone.minimize();
	equivalentOnAllStrings<2>(foo, catClone, 8, __LINE__);
}

TEST(AutomatonTest, CatAltLitAltLitAltMinimize) {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, lit<2>(1), a, lit<2>(1), a});
	auto catClone = foo; //clone
	catClone.minimize();
	equivalentOnAllStrings<2>(foo, catClone, 8, __LINE__);
}

TEST(AutomatonTest, CatAltLitAltRemoveDeadStates) {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, lit<2>(1), a});
	auto catClone = foo; //clone
	catClone.removeDeadStates();
	equivalentOnAllStrings<2>(foo, catClone, 8, __LINE__);
}

TEST(AutomatonTest, CatAltMinimizeTrinary) {
	auto a = alt<3>({lit<3>(0), lit<3>(1)});
	auto b = any<3>();
	auto foo = cat<3>({a, b, a});
	auto catClone = foo; //clone
	catClone.minimize();
	equivalentOnAllStrings<3>(foo, catClone, 8, __LINE__);
}

TEST(AutomatonTest, HashSanity) {
	auto a = any<2>();
	std::hash<Automaton<2>>()(a);
}

template<class A>
bool eq(const A& left, const A& right) {
	bool e = left == right;
	if (e)
		//ASSERT_EQ can only be used in functions returning void, for whatever reason
		assert(std::hash<A>()(left) == std::hash<A>()(right));
	return e;
}
template<unsigned int S>
bool eq(boost::intrusive_ptr<Automaton<S>> left, boost::intrusive_ptr<Automaton<S>> right) {
	return eq(*left, *right);
}

TEST(AutomatonTest, EqualitySanity) {
	ASSERT_TRUE(eq(any<2>(), any<2>()));
	ASSERT_TRUE(eq(any<4>(), any<4>()));
	ASSERT_TRUE(eq(any<5>(), any<5>()));
	ASSERT_TRUE(eq(any<12>(), any<12>()));

	ASSERT_TRUE(eq(lit<2>(0), lit<2>(0)));
	ASSERT_FALSE(eq(lit<2>(0), lit<2>(1)));
}