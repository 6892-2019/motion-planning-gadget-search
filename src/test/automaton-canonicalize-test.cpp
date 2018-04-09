#include "precompiled.hpp"
#include "automaton.hpp"
#include "util.hpp"
#include <doctest.h>

using namespace automaton;
using namespace automaton::impl;

TEST_CASE("AutomatonTest_Canonicalize") {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	CHECK_UNARY(same_language(noop, canonicalize(noop)));
	auto redundant = alt(noop, noop, noop);
	CHECK_UNARY(same_language(redundant, canonicalize(redundant)));
}

TEST_CASE("AutomatonTest_Canonicalize1") {
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
		CHECK_UNARY(tshuf.canonical());
		CHECK_UNARY(same_language(tshuf, cshuf));
		REQUIRE_EQ(tshuf, cshuf);
	} while (std::next_permutation(renumbering.begin()+1, renumbering.end()));
}

TEST_CASE("AutomatonTest_Canonicalize2") {
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
		CHECK_UNARY(same_language(tshuf, cshuf));
		tshuf.canonicalize();
		CHECK_UNARY(tshuf.canonical());
		CHECK_UNARY(same_language(tshuf, cshuf));
		REQUIRE_EQ(tshuf, cshuf);
	}
}

TEST_CASE("AutomatonTest_CanonicalizeRenumberIdentity") {
	std::initializer_list<std::initializer_list<AutomatonBase::state_type>> perm = {{0, 1, 2, 3}};
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	CHECK_UNARY(same_language(shuf, canonicalizeRenumber(shuf, perm.begin(), perm.end())));
	auto redundant = alt(shuf, shuf, shuf);
	CHECK_UNARY(same_language(redundant, canonicalizeRenumber(redundant, perm.begin(), perm.end())));
}

TEST_CASE("AutomatonTest_CanonicalizeRenumberIsAComposition") {
	std::initializer_list<std::initializer_list<AutomatonBase::state_type>> perm = {{1, 0, 3, 2}};
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	auto cshuf = minimize(shuf);
	cshuf.renumberAlphabet(perm.begin()[0].begin());
	cshuf.canonicalize();
	auto crshuf = canonicalizeRenumber(shuf, perm.begin(), perm.end());
	CHECK_UNARY(same_language(cshuf, crshuf));
	CHECK_EQ(cshuf, crshuf);
}

TEST_CASE("AutomatonTest_CanonicalizeRenumber1") {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);

	std::initializer_list<std::initializer_list<AutomatonBase::state_type>> perms = {
		{0, 1, 2, 3},
		{1, 0, 3, 2},
		{3, 0, 1, 2},
		{3, 2, 1, 0},
	};
	auto cshuf = canonicalizeRenumber(shuf, perms.begin(), perms.end());
	std::vector<AutomatonBase::state_type> renumbering(cshuf.state_size()), work(cshuf.state_size());
	std::iota(renumbering.begin(), renumbering.end(), 0);
	do {
		auto tshuf = cshuf;
		std::copy(renumbering.begin(), renumbering.end(), work.begin());
		tshuf.renumberStates(work.begin());
		tshuf.canonicalizeRenumber(perms.begin(), perms.end());
		CHECK_UNARY(tshuf.canonical());
		CHECK_UNARY(same_language(tshuf, cshuf));
		REQUIRE_EQ(tshuf, cshuf); //OLDTEST: to_string(renumbering);
	} while (std::next_permutation(renumbering.begin()+1, renumbering.end()));
}

TEST_CASE("AutomatonTest_CanonicalizeRenumber5") {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);

	std::initializer_list<std::initializer_list<AutomatonBase::state_type>> perms = {
		{0, 1, 2, 3}, {1, 2, 3, 0}, {2, 3, 0, 1}, {3, 0, 1, 2},
		{3, 2, 1, 0}, {2, 1, 0, 3}, {1, 0, 3, 2}, {0, 3, 2, 1},
	};
	auto cshuf = canonicalizeRenumber(shuf, perms.begin(), perms.end());
	std::vector<AutomatonBase::state_type> renumbering(cshuf.state_size()), work(cshuf.state_size());
	std::iota(renumbering.begin(), renumbering.end(), 0);
	do {
		auto tshuf = cshuf;
		std::copy(renumbering.begin(), renumbering.end(), work.begin());
		tshuf.renumberStates(work.begin());
		tshuf.canonicalizeRenumber(perms.begin(), perms.end());
		CHECK_UNARY(tshuf.canonical());
		CHECK_UNARY(same_language(tshuf, cshuf));
		REQUIRE_EQ(tshuf, cshuf); //OLDTEST: to_string(renumbering);
	} while (std::next_permutation(renumbering.begin()+1, renumbering.end()));
}

TEST_CASE("AutomatonTest_CanonicalizeRenumber2") {
	Automaton<4> a;
	a.reserve(5);
	for (AutomatonBase::state_type s = 0; s < 5; ++s)
		a.addState();
	for (AutomatonBase::state_type s : {2, 3, 4, })
		a.setAccept(s);
	a.addTrans(0, 0, 1);
	a.addTrans(0, 3, 1);
	a.addTrans(1, 3, 4);
	a.addTrans(2, 1, 3);
	a.addTrans(3, 0, 4);
	a.addTrans(4, 0, 2);
	a.addTrans(4, 1, 3);

	std::initializer_list<std::initializer_list<AutomatonBase::state_type>> perms = {
		{0, 1, 2, 3}, {1, 2, 3, 0}, {2, 3, 0, 1}, {3, 0, 1, 2},
		{3, 2, 1, 0}, {2, 1, 0, 3}, {1, 0, 3, 2}, {0, 3, 2, 1},
	};
	auto golden = canonicalizeRenumber(a, perms.begin(), perms.end());
	std::vector<AutomatonBase::state_type> renumbering(golden.state_size()), work(golden.state_size());
	std::iota(renumbering.begin(), renumbering.end(), 0);
	do {
		auto silver = golden;
		std::copy(renumbering.begin(), renumbering.end(), work.begin());
		silver.renumberStates(work.begin());
		silver.canonicalizeRenumber(perms.begin(), perms.end());
		CHECK_UNARY(silver.canonical());
		CHECK_UNARY(same_language(silver, golden));
		REQUIRE_EQ(silver, golden); //OLDTEST: to_string(renumbering);
	} while (std::next_permutation(renumbering.begin()+1, renumbering.end()));
}

TEST_CASE("AutomatonTest_CanonicalizeRenumber3") {
	Automaton<4> a;
	a.reserve(5);
	for (AutomatonBase::state_type s = 0; s < 5; ++s)
		a.addState();
	for (AutomatonBase::state_type s : {2, 3, 4, })
		a.setAccept(s);
	a.addTrans(0, 0, 1);
	a.addTrans(0, 3, 1);
	a.addTrans(1, 3, 4);
	a.addTrans(2, 1, 3);
	a.addTrans(3, 0, 4);
	a.addTrans(4, 0, 2);
	a.addTrans(4, 1, 3);

	std::initializer_list<std::initializer_list<AutomatonBase::state_type>> perms = {
		{0, 1, 2, 3}, {1, 2, 3, 0}, {2, 3, 0, 1}, {3, 0, 1, 2},
		{3, 2, 1, 0}, {2, 1, 0, 3}, {1, 0, 3, 2}, {0, 3, 2, 1},
	};
	auto golden = canonicalizeRenumber(a, perms.begin(), perms.end());
	for (const std::initializer_list<AutomatonBase::state_type>& p : perms) {
		auto silver = golden;
		silver.renumberAlphabet(p.begin());
		silver.canonicalizeRenumber(perms.begin(), perms.end());
		CHECK_UNARY(silver.canonical());
		CHECK_UNARY(same_language(silver, golden));
		REQUIRE_EQ(silver, golden);
	}
}

TEST_CASE("AutomatonTest_CanonicalizeRenumber4") {
	Automaton<4> a;
	a.reserve(5);
	for (AutomatonBase::state_type s = 0; s < 5; ++s)
		a.addState();
	for (AutomatonBase::state_type s : {2, 3, 4, })
		a.setAccept(s);
	a.addTrans(0, 0, 1);
	a.addTrans(0, 3, 1);
	a.addTrans(1, 3, 4);
	a.addTrans(2, 1, 3);
	a.addTrans(3, 0, 4);
	a.addTrans(4, 0, 2);
	a.addTrans(4, 1, 3);

	std::initializer_list<std::initializer_list<AutomatonBase::state_type>> perms = {
		{0, 1, 2, 3}, {1, 2, 3, 0}, {2, 3, 0, 1}, {3, 0, 1, 2},
		{3, 2, 1, 0}, {2, 1, 0, 3}, {1, 0, 3, 2}, {0, 3, 2, 1},
	};
	auto golden = canonicalizeRenumber(a, perms.begin(), perms.end());
	std::vector<AutomatonBase::state_type> renumbering(golden.state_size()), work(golden.state_size());
	std::iota(renumbering.begin(), renumbering.end(), 0);
	for (const std::initializer_list<AutomatonBase::state_type>& p : perms) {
		do {
			auto silver = golden;
			std::copy(renumbering.begin(), renumbering.end(), work.begin());
			silver.renumberAlphabet(p.begin());
			silver.renumberStates(work.begin());
			silver.canonicalizeRenumber(perms.begin(), perms.end());
			CHECK_UNARY(silver.canonical());
			CHECK_UNARY(same_language(silver, golden));
			REQUIRE_EQ(silver, golden); //OLDTEST: to_string(renumbering);
		} while (std::next_permutation(renumbering.begin()+1, renumbering.end()));
	}
}