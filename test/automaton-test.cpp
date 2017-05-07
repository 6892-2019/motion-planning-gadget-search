#include "precompiled.hpp"
#include "automaton.hpp"
#include "util.hpp"
#include <gtest/gtest.h>

using namespace automaton;
using namespace automaton::impl;

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

	Automaton<4> twoAccept;
	twoAccept.addState();
	twoAccept.addState();
	twoAccept.setAccept(0);
	twoAccept.setAccept(1);
	auto twoAcceptM = twoAccept;
	twoAcceptM.minimize();
	ASSERT_EQ(twoAcceptM.state_size(), 1) << twoAcceptM;
	equivalentOnAllStrings<4>(twoAccept, twoAcceptM, 4, __LINE__);

	Automaton<4> twoAccept2;
	twoAccept2.addState();
	twoAccept2.addState();
	twoAccept2.addState();
	twoAccept2.setAccept(1);
	twoAccept2.setAccept(2);
	twoAccept2.addTrans(0, 0, 1);
	twoAccept2.addTrans(0, 1, 2);
	auto twoAccept2M = twoAccept2;
	twoAccept2M.minimize();
	ASSERT_EQ(twoAccept2M.state_size(), 2) << twoAccept2M;
	equivalentOnAllStrings<4>(twoAccept2, twoAccept2M, 4, __LINE__);
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

TEST(AutomatonTest, MinimizeDeadEndAcceptStates) {
	//A minimal automaton cannot contain two states with the same accept status
	//and no outgoing transitions, because those states would be Myhill-Nerode
	//equivalent.
	Automaton<4> a;
	a.reserve(10);
	for (AutomatonBase::state_type s = 0; s < 10; ++s)
		a.addState();
	for (AutomatonBase::state_type s : {2, 4, 5, 6, 7, 9, })
		a.setAccept(s);
	a.addTrans(0, 0, 1);
	a.addTrans(0, 3, 8);
	a.addTrans(1, 2, 0);
	a.addTrans(1, 2, 1);
	a.addTrans(1, 3, 1);
	a.addTrans(1, 1, 2);
	a.addTrans(2, 2, 3);
	a.addTrans(3, 3, 3);
	a.addTrans(4, 3, 5);
	a.addTrans(4, 1, 6);
	a.addTrans(4, 3, 9);
	a.addTrans(6, 2, 2);
	a.addTrans(6, 3, 2);
	a.addTrans(7, 1, 1);
	a.addTrans(7, 0, 2);
	a.addTrans(7, 1, 2);
	a.addTrans(8, 0, 5);
	a.addTrans(9, 3, 6);
	a.minimize();

	//TODO: extract this for use in the fuzz tester, or to make it an assertion
	//in HopcroftMinimizer
	std::vector<AutomatonBase::state_type> deadEndAccepts;
	for (AutomatonBase::state_type s = 0; s < a.state_size(); ++s)
		if (a.accept(s) && a.destinations(s).empty())
			deadEndAccepts.push_back(s);
	ASSERT_EQ(deadEndAccepts.size(), 1) << a;
}

TEST(AutomatonTest, ShuffleAccept01) {
	auto left = lit<2>(0, 0), right = lit<2>(1, 1);
	auto comb = shuffleAccept(left, right);
	EXPECT_TRUE(comb.run(0, 0, 1, 1));
	EXPECT_TRUE(comb.run(1, 1, 0, 0));
	EXPECT_FALSE(comb.run(0, 0));
	EXPECT_FALSE(comb.run(0, 0, 0, 0));
	EXPECT_FALSE(comb.run(1, 1));
	EXPECT_FALSE(comb.run(1, 1, 1, 1));
	EXPECT_FALSE(comb.run(0, 1, 0, 1));
	EXPECT_FALSE(comb.run(1, 0, 1, 0));
	EXPECT_FALSE(comb.run());
}

TEST(AutomatonTest, ShuffleAccept02) {
	auto left = star(lit<2>(0, 0)), right = lit<2>(1, 1);
	auto comb = shuffleAccept(left, right);
	EXPECT_FALSE(comb.run());
	EXPECT_FALSE(comb.run(0, 0));
	EXPECT_TRUE(comb.run(1, 1));
	EXPECT_TRUE(comb.run(0, 0, 1, 1));
	EXPECT_TRUE(comb.run(0, 0, 1, 1, 0, 0));
	EXPECT_TRUE(comb.run(1, 1, 0, 0));
	EXPECT_FALSE(comb.run(0, 0, 0, 0));
	EXPECT_FALSE(comb.run(1, 1, 1, 1));
	EXPECT_FALSE(comb.run(0, 1, 0, 1));
	EXPECT_FALSE(comb.run(1, 0, 1, 0));
}

TEST(AutomatonTest, ShuffleAcceptWithEmpty) {
	auto left = star(lit<2>(0, 0)), right = empty<2>();
	auto comb = shuffleAccept(left, right);
	equivalentOnAllStrings<2>(comb, empty<2>(), 8, __LINE__);
}

TEST(AutomatonTest, ShuffleAcceptSymmetry) {
	auto left = cat<2>({star<2>(cat<2>({lit<2>(0), lit<2>(1)})), lit<2>(1)});
	auto right = plus<2>(nCopies<2>(any<2>(), 3));
	equivalentOnAllStrings<2>(shuffleAccept(left, right), shuffleAccept(right, left), 8, __LINE__);
	left.minimize();
	right.minimize();
	equivalentOnAllStrings<2>(shuffleAccept(left, right), shuffleAccept(right, left), 8, __LINE__);
}

TEST(AutomatonTest, ShuffleAcceptInvariantToDuplication) {
	auto left = cat<2>({star<2>(cat<2>({lit<2>(0), lit<2>(1)})), lit<2>(1)});
	equivalentOnAllStrings<2>(shuffleAccept(left, left), shuffleAccept(left, alt(left, left)), 8, __LINE__);
	equivalentOnAllStrings<2>(shuffleAccept(left, left), shuffleAccept(alt(left, left), left), 8, __LINE__);
	equivalentOnAllStrings<2>(shuffleAccept(left, left), shuffleAccept(alt(left, left), alt(left, left)), 8, __LINE__);
}

TEST(AutomatonTest, ShuffleAcceptComposeMinimize) {
	auto left = cat<2>({star<2>(cat<2>({lit<2>(0), lit<2>(1)})), lit<2>(1)});
	auto right = plus<2>(nCopies<2>(any<2>(), 3));
	auto shuf = shuffleAccept(left, right);
	shuf.minimize();
	left.minimize();
	right.minimize();
	auto minshuf = shuffleAccept(left, right);
	equivalentOnAllStrings<2>(shuf, minshuf, 8, __LINE__);
}

TEST(AutomatonTest, ShuffleAcceptSymmetry2) {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase), rshuf = shuffleAccept(parallelToggleBase, noop);
	equivalentOnAllStrings<4>(shuf, rshuf, 4, __LINE__);

	noop.minimize();
	parallelToggleBase.minimize();
	auto mshuf = shuffleAccept(noop, parallelToggleBase), rmshuf = shuffleAccept(parallelToggleBase, noop);
	equivalentOnAllStrings<4>(mshuf, rmshuf, 4, __LINE__);
}

TEST(AutomatonTest, ShuffleAcceptComposeMinimize2) {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase), rshuf = shuffleAccept(parallelToggleBase, noop);
	noop.minimize();
	parallelToggleBase.minimize();
	auto mshuf = shuffleAccept(noop, parallelToggleBase), rmshuf = shuffleAccept(parallelToggleBase, noop);
	equivalentOnAllStrings<4>(shuf, mshuf, 4, __LINE__);
	equivalentOnAllStrings<4>(rshuf, rmshuf, 4, __LINE__);
}

TEST(AutomatonTest, MinimizationPreservesLanguage) {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto mnoop = noop;
	mnoop.minimize();
	equivalentOnAllStrings(noop, mnoop, 4, __LINE__);
}

TEST(AutomatonTest, MinimizationPreservesLanguage2) {
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto mp = parallelToggleBase;
	mp.minimize();
	equivalentOnAllStrings(parallelToggleBase, mp, 4, __LINE__);
}

TEST(AutomatonTest, MinimizationPreservesLanguage3) {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	auto mshuf = shuf;
	mshuf.minimize();
	equivalentOnAllStrings<4>(shuf, mshuf, 4, __LINE__);
}

TEST(AutomatonTest, MinimizationPreservesLanguage4) {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	noop.minimize();
	parallelToggleBase.minimize();
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	auto mshuf = shuf;
	mshuf.minimize();
	equivalentOnAllStrings<4>(shuf, mshuf, 4, __LINE__);
}

TEST(AutomatonTest, MinimizationPreservesLanguage5) {
	auto ltr = lit<4>(0, 1), rtl = lit<4>(1, 0);
	auto parallelToggleBase = alt(ltr, cat(ltr, rtl));
	auto mp = parallelToggleBase;
	mp.minimize();
	equivalentOnAllStrings(parallelToggleBase, mp, 4, __LINE__);
}

TEST(AutomatonTest, ShuffleAcceptComposeMinimize3) {
	auto noop = lit<4>(0);
	auto ltr = lit<4>(0, 1), rtl = lit<4>(1, 0);
	auto parallelToggleBase = alt(ltr, cat(ltr, rtl));
	auto shuf = shuffleAccept(noop, parallelToggleBase), rshuf = shuffleAccept(parallelToggleBase, noop);
	parallelToggleBase.minimize();
	auto mshuf = shuffleAccept(noop, parallelToggleBase), rmshuf = shuffleAccept(parallelToggleBase, noop);
	equivalentOnAllStrings<4>(shuf, mshuf, 4, __LINE__);
	equivalentOnAllStrings<4>(rshuf, rmshuf, 4, __LINE__);
}

TEST(AutomatonTest, Canonicalize) {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	EXPECT_TRUE(same_language(noop, canonicalize(noop)));
	auto redundant = alt(noop, noop, noop);
	EXPECT_TRUE(same_language(redundant, canonicalize(redundant)));
}

TEST(AutomatonTest, Canonicalize1) {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	auto cshuf = canonicalize(shuf);
	dynarray<AutomatonBase::state_type> renumbering(cshuf.state_size()), work(cshuf.state_size());
	std::iota(renumbering.begin(), renumbering.end(), 0);
	do {
		auto tshuf = cshuf;
		std::copy(renumbering.begin(), renumbering.end(), work.begin());
		tshuf.renumberStates(work.begin());
		tshuf.canonicalize();
		EXPECT_TRUE(tshuf.canonical());
		EXPECT_TRUE(same_language(tshuf, cshuf));
		ASSERT_EQ(tshuf, cshuf);
	} while (std::next_permutation(renumbering.begin()+1, renumbering.end()));
}

TEST(AutomatonTest, Canonicalize2) {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	auto cshuf = canonicalize(shuf);
	dynarray<AutomatonBase::state_type> renumbering(shuf.state_size());
	std::iota(renumbering.begin(), renumbering.end(), 0);
	std::knuth_b rng(0);
	for (int i = 0; i < 100; ++i) {
		auto tshuf = shuf;
		std::shuffle(renumbering.begin()+1, renumbering.end(), rng);
		tshuf.renumberStates(renumbering.begin());
		EXPECT_TRUE(same_language(tshuf, cshuf));
		tshuf.canonicalize();
		EXPECT_TRUE(tshuf.canonical());
		EXPECT_TRUE(same_language(tshuf, cshuf));
		ASSERT_EQ(tshuf, cshuf);
	}
}