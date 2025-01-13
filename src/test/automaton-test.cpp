// SPDX-License-Identifier: MIT
// Copyright 2018 Massachusetts Institute of Technology
// Copyright 2025 Jeffrey Bosboom
#include "precompiled.hpp"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN //genbuild {'entrypoint': True}
#include <doctest/doctest.h>
#include "automaton.hpp"
#include "automaton-regexesque.hpp"
#include "util.hpp"

namespace automaton {
doctest::String toString(OptimizeKind kind) {
	std::array<char, 32> buf;
	auto end = fmt::format_to_n(buf.data(), buf.size()-1, "{}", kind);
	*end.out = '\0';
	return doctest::String(buf.data());
}
} //namespace automaton

using namespace automaton;
using namespace automaton::impl;

template<unsigned int N>
Automaton<N> determinizePreservesLanguage(const Automaton<N>& p) {
	auto q = p;
	q.determinize();
	auto cmp = compare_languages(p, q);
	CHECK_UNARY(cmp.equal());
	//TODO: generate and print witnesses
	return q;
}
template<unsigned int N>
Automaton<N> minimizePreservesLanguage(const Automaton<N>& p) {
	auto q = p;
	q.minimize();
	auto cmp = compare_languages(p, q);
	CHECK_UNARY(cmp.equal());
	//TODO: generate and print witnesses
	return q;
}
static std::array<OptimizeKind, 4> optimize_kinds = {
	OptimizeKind::RIGHT,
	OptimizeKind::LEFT,
	OptimizeKind::RIGHT_LEFT,
	OptimizeKind::LEFT_RIGHT
};
//Sometimes we care which kind is used because we're going to check its size/etc.,
//while other times we just want to check that the language is preserved over
//all kinds.  We always do all of them, but return only one, if requested.
//TODO: template hackery to return a std::array of size matching the number of OptimizeKind arguments passed
template<unsigned int N>
void optimizePreservesLanguage(const Automaton<N>& p) {
	CHECK_UNARY_FALSE(p.deterministic()); //optimize() is just minimize() if it's deterministic
	for (OptimizeKind how : optimize_kinds) {
		CAPTURE(how);
		auto q = p;
		q.optimize(how);
		auto cmp = compare_languages(p, q);
		CHECK_UNARY(cmp.equal());
		//TODO: generate and print witnesses
	}
}
template<unsigned int N>
Automaton<N> optimizePreservesLanguage(const Automaton<N>& p, OptimizeKind requestedReturn) {
	CHECK_UNARY_FALSE(p.deterministic()); //optimize() is just minimize() if it's deterministic
	Automaton<N> rv;
	for (OptimizeKind how : optimize_kinds) {
		CAPTURE(how);
		auto q = p;
		q.optimize(how);
		auto cmp = compare_languages(p, q);
		CHECK_UNARY(cmp.equal());
		//TODO: generate and print witnesses
		if (how == requestedReturn)
			rv = std::move(q);
	}
	return rv;
}

TEST_CASE("AutomatonTest_AddTransEmptySymbolMask") {
	Automaton<2> a;
	a.addState();
	a.addState();
	Automaton<2>::symbol_mask_type empty;
	CHECK_UNARY_FALSE(a.addTrans(0, empty, 1));
	CHECK_UNARY(a.destinations(0).empty());
}

TEST_CASE("AutomatonTest_Clone") {
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

TEST_CASE("AutomatonTest_Determinize00") {
	determinizePreservesLanguage(range<2>(lit<2>(0), 2, 6));
}
TEST_CASE("AutomatonTest_Determinize01") {
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	determinizePreservesLanguage(f);
}
TEST_CASE("AutomatonTest_Determinize02") {
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	auto g = conj<2>(f, d);
	determinizePreservesLanguage(g);
}
TEST_CASE("AutomatonTest_Determinize03") {
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	auto g = conj<2>(f, d);
	determinizePreservesLanguage(g);
}
TEST_CASE("AutomatonTest_Determinize04") {
	determinizePreservesLanguage(star<2>(nCopies<2>(any<2>(), 3))); //multOf3
}
TEST_CASE("AutomatonTest_Determinize05") {
	auto multOf3 = star<2>(nCopies<2>(any<2>(), 3));
	determinizePreservesLanguage(conj<2>(multOf3, nCopies<2>(any<2>(), 6))); //finiteMultOf3
}
TEST_CASE("AutomatonTest_Determinize06") {
	determinizePreservesLanguage(plus<2>(nCopies<2>(any<2>(), 3))); //posMultOf3
}
TEST_CASE("AutomatonTest_Determinize07") {
	auto posMultOf3 = plus<2>(nCopies<2>(any<2>(), 3));
	determinizePreservesLanguage(conj<2>(posMultOf3, nCopies<2>(any<2>(), 6))); //finitePosMultOf3
}

namespace {
Automaton<2> pathologicalZeroZeroAlt() {
	//An automaton with pathologically many alternatives of 0, 0, to stress the
	//set-paging stuff in determinize.
	Automaton<2> a;
	a.reserve(1500);
	a.addState();
	a.addState();
	a.setAccept(1);
	for (int i = 0; a.state_size() < 1500; ++i) {
		auto s = a.addState();
		a.addTrans(0, 0, s);
		a.addTrans(s, 0, 1);
	}
	return a;
}
}

TEST_CASE("AutomatonTest_Determinize08") {
	determinizePreservesLanguage(pathologicalZeroZeroAlt());
}

namespace {
//Builds an NFA where at least one DFA state has a corresponding NFA state set
//of the given size.
Automaton<2> hitNFAStateSetSize(unsigned int size) {
	Automaton<2> a;
	for (unsigned int i = 0; a.state_size() < size; ++i) {
		a.addState();
		a.setAccept(i);
		a.addTrans(0, 0, i);
	}
	return a;
}

//Builds an NFA such that one NFA state set (including duplicate states) has
//size exactly 256, which we must not confuse for zero.  (If we do, we'll
//conclude there are no transitions on 1 in the automaton, thus changing the
//language.)
Automaton<2> tempSetSizeWraps() {
	Automaton<2> a;
	a.reserve(131);
	for (int i = 0; i < 131; ++i) {
		a.addState();
		a.setAccept(i);
	}
	a.addTrans(0, 0, 1);
	a.addTrans(0, 0, 2);
	for (unsigned int i = 3; i < a.state_size(); ++i) {
		a.addTrans(1, 1, i);
		a.addTrans(2, 1, i);
	}
	return a;
}
} //anonymous namespace

TEST_CASE("AutomatonTest_Determinize09") {
	determinizePreservesLanguage(hitNFAStateSetSize(254));
	determinizePreservesLanguage(hitNFAStateSetSize(255));
	determinizePreservesLanguage(hitNFAStateSetSize(256));
}

TEST_CASE("AutomatonTest_Determinize10") {
	determinizePreservesLanguage(tempSetSizeWraps());
}

TEST_CASE("AutomatonTest_Totalize") {
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

TEST_CASE("AutomatonTest_DeterminizeTotalize") {
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

TEST_CASE("AutomatonTest_TotalizeDeterminize") {
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

TEST_CASE("AutomatonTest_RemoveDeadStates") {
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

TEST_CASE("AutomatonTest_DeterminizeRemoveDeadStates") {
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

TEST_CASE("AutomatonTest_Minimize00") {
	minimizePreservesLanguage(lit<2>(0));
}
TEST_CASE("AutomatonTest_Minimize01") {
	minimizePreservesLanguage(cat<2>({lit<2>(0), lit<2>(1)}));
}
TEST_CASE("AutomatonTest_Minimize02") {
	minimizePreservesLanguage(range<2>(lit<2>(0), 2, 6));
}
TEST_CASE("AutomatonTest_Minimize03") {
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	minimizePreservesLanguage(alt<2>({d, e}));
}
TEST_CASE("AutomatonTest_Minimize04") {
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	auto e = cat<2>({lit<2>(1), lit<2>(0)});
	auto f = alt<2>({d, e});
	minimizePreservesLanguage(conj<2>(f, d));
}
TEST_CASE("AutomatonTest_Minimize05") {
	minimizePreservesLanguage(comp<2>(lit<2>(0)));
}
TEST_CASE("AutomatonTest_Minimize06") {
	minimizePreservesLanguage(all<2>());
}
TEST_CASE("AutomatonTest_Minimize07") {
	minimizePreservesLanguage(empty<2>());
}
TEST_CASE("AutomatonTest_Minimize08") {
	auto d = cat<2>({lit<2>(0), lit<2>(1)});
	minimizePreservesLanguage(alt<2>({d, d, d, d, d, d, d}));
}
TEST_CASE("AutomatonTest_Minimize09") {
	minimizePreservesLanguage(star<2>(nCopies<2>(any<2>(), 3))); //multOf3
}
TEST_CASE("AutomatonTest_Minimize10") {
	minimizePreservesLanguage(cat<2>({star<2>(nCopies<2>(any<2>(), 3)), lit<2>(0)})); //multOf3Zero
}
TEST_CASE("AutomatonTest_Minimize11") {
	minimizePreservesLanguage(cat<2>({star<2>(nCopies<2>(any<2>(), 3)), lit<2>(1)})); //multOf3One
}
TEST_CASE("AutomatonTest_Minimize12") {
	auto multOf3 = star<2>(nCopies<2>(any<2>(), 3));
	minimizePreservesLanguage(conj<2>(multOf3, nCopies<2>(any<2>(), 6))); //finiteMultOf3
}
TEST_CASE("AutomatonTest_Minimize13") {
	minimizePreservesLanguage(plus<2>(nCopies<2>(any<2>(), 3))); //posMultOf3
}
TEST_CASE("AutomatonTest_Minimize14") {
	auto posMultOf3 = plus<2>(nCopies<2>(any<2>(), 3));
	minimizePreservesLanguage(conj<2>(posMultOf3, nCopies<2>(any<2>(), 6))); //finitePosMultOf3
}
TEST_CASE("AutomatonTest_Minimize015") {
	minimizePreservesLanguage(cat<2>({star<2>(lit<2>(0)), lit<2>(1)})); //zeroStarOne
}
TEST_CASE("AutomatonTest_Minimize16") {
	minimizePreservesLanguage(cat<2>({star<2>(cat<2>({lit<2>(0), lit<2>(0)})), lit<2>(1)})); //zeroZeroStarOne
}
TEST_CASE("AutomatonTest_Minimize17") {
	minimizePreservesLanguage(cat<2>({star<2>(cat<2>({lit<2>(0), lit<2>(1)})), lit<2>(1)})); //zeroOneStarOne
}
TEST_CASE("AutomatonTest_Minimize18") {
	Automaton<4> twoAccept;
	twoAccept.addState();
	twoAccept.addState();
	twoAccept.setAccept(0);
	twoAccept.setAccept(1);
	auto twoAcceptM = minimizePreservesLanguage(twoAccept);
	CHECK_EQ(twoAcceptM.state_size(), 1);
}
TEST_CASE("AutomatonTest_Minimize19") {
	Automaton<4> twoAccept2;
	twoAccept2.addState();
	twoAccept2.addState();
	twoAccept2.addState();
	twoAccept2.setAccept(1);
	twoAccept2.setAccept(2);
	twoAccept2.addTrans(0, 0, 1);
	twoAccept2.addTrans(0, 1, 2);
	auto twoAccept2M = minimizePreservesLanguage(twoAccept2);
	CHECK_EQ(twoAccept2M.state_size(), 2);
}
TEST_CASE("AutomatonTest_Minimize20") {
	minimizePreservesLanguage(epsilon<2>());
}

TEST_CASE("AutomatonTest_CatAltMinimize") {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, a, a});
	auto catClone = foo; //clone
	catClone.minimize();
	equivalentOnAllStrings<2>(foo, catClone, 8, __LINE__);
}

TEST_CASE("AutomatonTest_CatAltLitAltMinimize") {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, lit<2>(1), a});
	auto catClone = foo; //clone
	catClone.minimize();
	equivalentOnAllStrings<2>(foo, catClone, 8, __LINE__);
}

TEST_CASE("AutomatonTest_CatAltLitAltLitAltMinimize") {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, lit<2>(1), a, lit<2>(1), a});
	auto catClone = foo; //clone
	catClone.minimize();
	equivalentOnAllStrings<2>(foo, catClone, 8, __LINE__);
}

TEST_CASE("AutomatonTest_CatAltLitAltRemoveDeadStates") {
	auto a = alt<2>({lit<2>(0), lit<2>(1)});
	auto foo = cat<2>({a, lit<2>(1), a});
	auto catClone = foo; //clone
	catClone.removeDeadStates();
	equivalentOnAllStrings<2>(foo, catClone, 8, __LINE__);
}

TEST_CASE("AutomatonTest_CatAltMinimizeTrinary") {
	auto a = alt<3>({lit<3>(0), lit<3>(1)});
	auto b = any<3>();
	auto foo = cat<3>({a, b, a});
	auto catClone = foo; //clone
	catClone.minimize();
	equivalentOnAllStrings<3>(foo, catClone, 8, __LINE__);
}

TEST_CASE("AutomatonTest_HashSanity") {
	auto a = any<2>();
	std::hash<Automaton<2>>()(a);
}

void equal_base(const AutomatonBase& l, const AutomatonBase& r) {
	CHECK_EQ(l, r);
	CHECK_EQ(l.hash(), r.hash());
}
void unequal_base(const AutomatonBase& l, const AutomatonBase& r) {
	CHECK_NE(l, r);
}

TEST_CASE("AutomatonTest_EqualitySanity") {
	CHECK_EQ(any<2>(), any<2>());
	CHECK_EQ(lit<2>(0), lit<2>(0));
	CHECK_NE(lit<2>(0), lit<2>(1));
}

TEST_CASE("AutomatonTest_BaseEqualitySanity") {
	equal_base(any<2>(), any<2>());
	equal_base(lit<2>(0), lit<2>(0));
	unequal_base(lit<2>(0), lit<2>(1));
	unequal_base(lit<2>(0), lit<4>(0));
}

TEST_CASE("AutomatonTest_MinimizeDeadEndAcceptStates") {
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
	//CHECK_EQ_MESSAGE(deadEndAccepts.size(), 1, a);
	CHECK_EQ(deadEndAccepts.size(), 1);
}

TEST_CASE("AutomatonTest_ShuffleAccept01") {
	auto left = lit<2>(0, 0), right = lit<2>(1, 1);
	auto comb = shuffleAccept(left, right);
	CHECK_UNARY(comb.run(0, 0, 1, 1));
	CHECK_UNARY(comb.run(1, 1, 0, 0));
	CHECK_UNARY_FALSE(comb.run(0, 0));
	CHECK_UNARY_FALSE(comb.run(0, 0, 0, 0));
	CHECK_UNARY_FALSE(comb.run(1, 1));
	CHECK_UNARY_FALSE(comb.run(1, 1, 1, 1));
	CHECK_UNARY_FALSE(comb.run(0, 1, 0, 1));
	CHECK_UNARY_FALSE(comb.run(1, 0, 1, 0));
	CHECK_UNARY_FALSE(comb.run());
}

TEST_CASE("AutomatonTest_ShuffleAccept02") {
	auto left = star(lit<2>(0, 0)), right = lit<2>(1, 1);
	auto comb = shuffleAccept(left, right);
	CHECK_UNARY_FALSE(comb.run());
	CHECK_UNARY_FALSE(comb.run(0, 0));
	CHECK_UNARY(comb.run(1, 1));
	CHECK_UNARY(comb.run(0, 0, 1, 1));
	CHECK_UNARY(comb.run(0, 0, 1, 1, 0, 0));
	CHECK_UNARY(comb.run(1, 1, 0, 0));
	CHECK_UNARY_FALSE(comb.run(0, 0, 0, 0));
	CHECK_UNARY_FALSE(comb.run(1, 1, 1, 1));
	CHECK_UNARY_FALSE(comb.run(0, 1, 0, 1));
	CHECK_UNARY_FALSE(comb.run(1, 0, 1, 0));
}

TEST_CASE("AutomatonTest_ShuffleAcceptWithEmpty") {
	auto left = star(lit<2>(0, 0)), right = empty<2>();
	auto comb = shuffleAccept(left, right);
	equivalentOnAllStrings<2>(comb, empty<2>(), 8, __LINE__);
}

TEST_CASE("AutomatonTest_ShuffleAcceptSymmetry") {
	auto left = cat<2>({star<2>(cat<2>({lit<2>(0), lit<2>(1)})), lit<2>(1)});
	auto right = plus<2>(nCopies<2>(any<2>(), 3));
	equivalentOnAllStrings<2>(shuffleAccept(left, right), shuffleAccept(right, left), 8, __LINE__);
	left.minimize();
	right.minimize();
	equivalentOnAllStrings<2>(shuffleAccept(left, right), shuffleAccept(right, left), 8, __LINE__);
}

TEST_CASE("AutomatonTest_ShuffleAcceptInvariantToDuplication") {
	auto left = cat<2>({star<2>(cat<2>({lit<2>(0), lit<2>(1)})), lit<2>(1)});
	equivalentOnAllStrings<2>(shuffleAccept(left, left), shuffleAccept(left, alt(left, left)), 8, __LINE__);
	equivalentOnAllStrings<2>(shuffleAccept(left, left), shuffleAccept(alt(left, left), left), 8, __LINE__);
	equivalentOnAllStrings<2>(shuffleAccept(left, left), shuffleAccept(alt(left, left), alt(left, left)), 8, __LINE__);
}

TEST_CASE("AutomatonTest_ShuffleAcceptComposeMinimize") {
	auto left = cat<2>({star<2>(cat<2>({lit<2>(0), lit<2>(1)})), lit<2>(1)});
	auto right = plus<2>(nCopies<2>(any<2>(), 3));
	auto shuf = shuffleAccept(left, right);
	shuf.minimize();
	left.minimize();
	right.minimize();
	auto minshuf = shuffleAccept(left, right);
	equivalentOnAllStrings<2>(shuf, minshuf, 8, __LINE__);
}

TEST_CASE("AutomatonTest_ShuffleAcceptSymmetry2") {
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

TEST_CASE("AutomatonTest_ShuffleAcceptComposeMinimize2") {
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

namespace {
template<class T>
void shuffleAcceptPolymorphicEquivalence(T&& left, T&& right) {
	auto shuf = shuffleAccept(left, right);
	const WorkingAutomaton& left_ref = left;
	const WorkingAutomaton& right_ref = right;
	auto shuf_ref = shuffleAccept(left_ref, right_ref, left_ref.alphabet_size());
	//So we can use compare_languages.  An implementation of compare_languages
	//on WorkingAutomaton& would require conj on WorkingAutomaton&.
	auto downcast = dynamic_cast<T&>(*shuf_ref);
	CHECK_UNARY(compare_languages(shuf, downcast).equal());
}
}

TEST_CASE("AutomatonTest_ShuffleAcceptPolymorphic01") {
	auto left = lit<2>(0, 0), right = lit<2>(1, 1);
	shuffleAcceptPolymorphicEquivalence(left, right);
	shuffleAcceptPolymorphicEquivalence(right, left);
}

TEST_CASE("AutomatonTest_ShuffleAcceptPolymorphic02") {
	auto left = star(lit<2>(0, 0)), right = lit<2>(1, 1);
	shuffleAcceptPolymorphicEquivalence(left, right);
	shuffleAcceptPolymorphicEquivalence(right, left);
}

TEST_CASE("AutomatonTest_ShuffleAcceptPolymorphicWithEmpty") {
	auto left = star(lit<2>(0, 0)), right = empty<2>();
	shuffleAcceptPolymorphicEquivalence(left, right);
	shuffleAcceptPolymorphicEquivalence(right, left);
}

TEST_CASE("AutomatonTest_ShuffleAcceptPolymorphicComposeMinimize2") {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	shuffleAcceptPolymorphicEquivalence(noop, parallelToggleBase);
	shuffleAcceptPolymorphicEquivalence(parallelToggleBase, noop);
	noop.minimize();
	parallelToggleBase.minimize();
	shuffleAcceptPolymorphicEquivalence(noop, parallelToggleBase);
	shuffleAcceptPolymorphicEquivalence(parallelToggleBase, noop);
}

namespace {
Automaton<8> make11149738326866() {
	Automaton<8> a;
	a.reserve(2);
	for (AutomatonBase::state_type s = 0; s < 2; ++s)
			a.addState();
	for (AutomatonBase::state_type s : {0, })
			a.setAccept(s);
	a.addTrans(0, 0, 1);
	a.addTrans(0, 1, 1);
	a.addTrans(0, 2, 1);
	a.addTrans(1, 0, 0);
	a.addTrans(1, 1, 0);
	a.addTrans(1, 2, 0);
	return a;
}
Automaton<8> make11204764538191() {
	Automaton<8> a;
	a.reserve(4);
	for (AutomatonBase::state_type s = 0; s < 4; ++s)
			a.addState();
	for (AutomatonBase::state_type s : {0, })
			a.setAccept(s);
	a.addTrans(0, 0, 1);
	a.addTrans(0, 1, 2);
	a.addTrans(0, 2, 3);
	a.addTrans(1, 0, 0);
	a.addTrans(2, 1, 0);
	a.addTrans(3, 2, 0);
	return a;
}
}

TEST_CASE("AutomatonTest_ShuffleAcceptPolymorphicSplitNop") {
	shuffleAcceptPolymorphicEquivalence(make11149738326866(), make11204764538191());
	shuffleAcceptPolymorphicEquivalence(make11204764538191(), make11149738326866());
}

TEST_CASE("AutomatonTest_MinimizationPreservesLanguage") {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto mnoop = noop;
	mnoop.minimize();
	equivalentOnAllStrings(noop, mnoop, 4, __LINE__);
}

TEST_CASE("AutomatonTest_MinimizationPreservesLanguage2") {
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto mp = parallelToggleBase;
	mp.minimize();
	equivalentOnAllStrings(parallelToggleBase, mp, 4, __LINE__);
}

TEST_CASE("AutomatonTest_MinimizationPreservesLanguage3") {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	auto mshuf = shuf;
	mshuf.minimize();
	equivalentOnAllStrings<4>(shuf, mshuf, 4, __LINE__);
}

TEST_CASE("AutomatonTest_MinimizationPreservesLanguage4") {
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

TEST_CASE("AutomatonTest_MinimizationPreservesLanguage5") {
	auto ltr = lit<4>(0, 1), rtl = lit<4>(1, 0);
	auto parallelToggleBase = alt(ltr, cat(ltr, rtl));
	auto mp = parallelToggleBase;
	mp.minimize();
	equivalentOnAllStrings(parallelToggleBase, mp, 4, __LINE__);
}

TEST_CASE("AutomatonTest_ShuffleAcceptComposeMinimize3") {
	auto noop = lit<4>(0);
	auto ltr = lit<4>(0, 1), rtl = lit<4>(1, 0);
	auto parallelToggleBase = alt(ltr, cat(ltr, rtl));
	auto shuf = shuffleAccept(noop, parallelToggleBase), rshuf = shuffleAccept(parallelToggleBase, noop);
	parallelToggleBase.minimize();
	auto mshuf = shuffleAccept(noop, parallelToggleBase), rmshuf = shuffleAccept(parallelToggleBase, noop);
	equivalentOnAllStrings<4>(shuf, mshuf, 4, __LINE__);
	equivalentOnAllStrings<4>(rshuf, rmshuf, 4, __LINE__);
}

TEST_CASE("AutomatonTest_TrivialTarjan0") {
	SCCs sccs = find_components(empty<2>());
	CHECK_EQ(sccs.size(), 1);
	CHECK_EQ(*sccs.begin(0), 0);
}

TEST_CASE("AutomatonTest_TrivialTarjan1") {
	SCCs sccs = find_components(lit<2>(0, 1, 0, 1, 1));
	CHECK_EQ(sccs.size(), 6);
	CHECK_EQ(*sccs.begin(0), 5);
	CHECK_EQ(*sccs.begin(1), 4);
	CHECK_EQ(*sccs.begin(2), 3);
	CHECK_EQ(*sccs.begin(3), 2);
	CHECK_EQ(*sccs.begin(4), 1);
	CHECK_EQ(*sccs.begin(5), 0);
}

TEST_CASE("AutomatonTest_ConnectedTarjan") {
	auto a = cat(minimize(star(lit<2>(0, 0, 0, 0))), minimize(star(lit<2>(1, 1, 1, 1, 1))));
	SCCs sccs = find_components(a);
	CHECK_EQ(sccs.size(), 2);
	CHECK_UNARY(unordered_equal(sccs.begin(1), sccs.end(1), {0, 1, 2, 3}));
	CHECK_UNARY(unordered_equal(sccs.begin(0), sccs.end(0), {4, 5, 6, 7, 8}));
}

TEST_CASE("AutomatonTest_DisconnectedTarjan") {
	auto a = minimize(star(lit<2>(0, 0, 0, 0)));
	a.append(minimize(star(lit<2>(1, 1, 1, 1, 1))));
	SCCs sccs = find_components(a);
	CHECK_EQ(sccs.size(), 2);
	CHECK_UNARY(unordered_equal(sccs.begin(0), sccs.end(0), {0, 1, 2, 3}));
	CHECK_UNARY(unordered_equal(sccs.begin(1), sccs.end(1), {4, 5, 6, 7, 8}));
}

TEST_CASE("AutomatonTest_ActiveAlphabet") {
	auto a = lit<4>(2, 3, 2, 3, 2, 3);
	auto active = a.activeAlphabet();
	CHECK_UNARY(unordered_equal(active.begin(), active.end(), {2, 3}));
	CHECK_EQ(a.active_alphabet_size(), 2);
	CHECK_EQ(active.size(), 2);
}

TEST_CASE("AutomatonTest_WorkingHash") {
	auto a = alt(lit<4>(0, 1, 2, 3), lit<4>(3, 1, 2, 0));
	CHECK_EQ(a.working_hash(), std::hash<decltype(a)>()(a));
}

TEST_CASE("AutomatonTest_MakeWorking") {
	CHECK_EQ(make_working(2)->alphabet_size(), 2);
	CHECK_EQ(make_working(4)->alphabet_size(), 4);
	CHECK_EQ(make_working(8)->alphabet_size(), 8);
}

TEST_CASE("AutomatonTest_Optimize01") {
	auto opt = optimizePreservesLanguage(pathologicalZeroZeroAlt(), OptimizeKind::RIGHT);
	CHECK_EQ(opt.state_size(), 3);
}

TEST_CASE("AutomatonTest_Optimize02") {
	Automaton<2> none;
	none.addState();
	none.addState();
	none.addState();
	none.addTrans(0, 0, 1);
	none.addTrans(0, 0, 2);
	auto opt = optimizePreservesLanguage(none, OptimizeKind::RIGHT);
	CHECK_EQ(opt.state_size(), 1);
	CHECK_UNARY_FALSE(opt.accept(0));
}

TEST_CASE("AutomatonTest_Optimize03") {
	Automaton<4> none;
	none.addState();
	none.addState();
	none.addState();
	none.addTrans(0, 0, 1);
	none.addTrans(0, 0, 2);
	auto opt = optimizePreservesLanguage(none, OptimizeKind::RIGHT);
	CHECK_EQ(opt.state_size(), 1);
	CHECK_UNARY_FALSE(opt.accept(0));
}

TEST_CASE("AutomatonTest_Optimize04") {
	Automaton<2> every;
	every.addState();
	every.addState();
	every.setAccept(0);
	every.setAccept(1);
	every.addTrans(0, 0, 0);
	every.addTrans(0, 1, 0);
	every.addTrans(0, 0, 1);
	every.addTrans(0, 1, 1);
	every.addTrans(1, 0, 1);
	every.addTrans(1, 1, 1);
	auto opt = optimizePreservesLanguage(every, OptimizeKind::RIGHT);
	CHECK_EQ(opt.state_size(), 1);
	CHECK_UNARY(opt.accept(0));
}

TEST_CASE("AutomatonTest_Optimize05") {
	Automaton<4> every;
	every.addState();
	every.addState();
	every.setAccept(0);
	every.setAccept(1);
	every.addTrans(0, 0, 0);
	every.addTrans(0, 1, 0);
	every.addTrans(0, 0, 1);
	every.addTrans(0, 1, 1);
	every.addTrans(1, 0, 1);
	every.addTrans(1, 1, 1);
	auto opt = optimizePreservesLanguage(every, OptimizeKind::RIGHT);
	CHECK_EQ(opt.state_size(), 1);
	CHECK_UNARY(opt.accept(0));
}

TEST_CASE("AutomatonTest_Optimize06") {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	optimizePreservesLanguage(shuf);
}

TEST_CASE("AutomatonTest_Optimize07") {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	Automaton<8> enlarged(shuf);
	optimizePreservesLanguage(enlarged);
}

TEST_CASE("AutomatonTest_Optimize08") {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	Automaton<8> enlarged(shuf);
	constexpr auto MISS = std::numeric_limits<AutomatonBase::state_type>::max();
	auto renumbering = {MISS, MISS, 0u, 1u, MISS, 2u, 3u, MISS};
	enlarged.renumberAlphabet(renumbering.begin());
	optimizePreservesLanguage(enlarged);
}

TEST_CASE("AutomatonTest_Optimize09") {
	//This is the automaton from the Ilie/etc papers that can be reduced left
	//or right, but not both.
	Automaton<4> a;
	for (int i = 0; i < 5; ++i)
		a.addState();
	a.setAccept(4);
	a.addTrans(0, 0, 1);
	a.addTrans(0, 0, 2);
	a.addTrans(0, 1, 3);
	a.addTrans(1, 2, 4);
	a.addTrans(2, 3, 4);
	a.addTrans(3, 2, 4);
	auto opt = optimizePreservesLanguage(a, OptimizeKind::RIGHT);
	CHECK_EQ(opt.state_size(), 4);
}