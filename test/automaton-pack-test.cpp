#include "precompiled.hpp"
#include "automaton.hpp"
#include "packedautomaton.hpp"
#include "packedautomaton-detail.hpp"
#include "util.hpp"
#include <gtest/gtest.h>

using namespace automaton;
using state_type = AutomatonBase::state_type;
using symbol_type = AutomatonBase::symbol_type;

namespace {
template<class PackImpl>
void pack_impl_test(const AutomatonBase& a) {
	assert(a.canonical());
	std::unique_ptr<const PackedAutomaton> packed = detail::make_pack<PackImpl>(a);
	EXPECT_EQ(packed->state_size(), a.state_size());
	EXPECT_EQ(packed->alphabet_size(), a.alphabet_size());
	EXPECT_EQ(packed->accept_size(), a.accept_size());
	EXPECT_EQ(packed->edge_size(), a.edge_size());
	EXPECT_EQ(packed->transition_size(), a.transition_size());
	for (state_type s = 0; s < a.state_size(); ++s) {
		EXPECT_EQ(packed->accept(s), a.accept(s)) << s;
		EXPECT_EQ(packed->outgoing(s), a.outgoing(s)) << s;
		EXPECT_EQ(packed->destinations(s), a.destinations(s)) << s;
		for (symbol_type c = 0; c < a.alphabet_size(); ++c) {
			EXPECT_EQ(packed->step(s, c), a.step(s, c)) << s << " " << c;
			EXPECT_EQ(packed->stepDeterministic(s, c), a.stepDeterministic(s, c)) << s << " " << c;
		}
		for (state_type t = 0; t < a.state_size(); ++t)
			EXPECT_EQ(packed->labels(s, t), a.labels(s, t)) << s << " " << t;
	}
	EXPECT_EQ(packed->hash(), a.hash());
	EXPECT_EQ(*packed, a);

	std::unique_ptr<const PackedAutomaton> repacked = detail::make_pack<PackImpl>(*packed);
	EXPECT_EQ(*repacked, *packed);
	EXPECT_EQ(repacked->packed_hash(), packed->packed_hash());

	packed = pack(*packed); //allow type to be freely chosen
	repacked = pack(*repacked);
	EXPECT_EQ(*repacked, *packed);
	EXPECT_EQ(repacked->packed_hash(), packed->packed_hash());
}

template<unsigned int N>
void test_pack(Automaton<N> a) {
	a.canonicalize();
	pack_impl_test<detail::Diminutive8OffsetPackedAutomaton>(a);
	pack_impl_test<detail::Tiny8OffsetPackedAutomaton>(a);
	pack_impl_test<detail::Small8OffsetPackedAutomaton>(a);
	pack_impl_test<detail::Medium8OffsetPackedAutomaton>(a);
	pack_impl_test<detail::Large8OffsetPackedAutomaton>(a);
}
} //anonymous namespace

TEST(AutomatonTest, Pack0) {
	test_pack(lit<8>(0, 1, 2, 3));
}
TEST(AutomatonTest, Pack1) {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	test_pack(noop);
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	test_pack(ltr);
	test_pack(rtl);
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	test_pack(parallelToggleBase);
	auto shuf = shuffleAccept(noop, parallelToggleBase), rshuf = shuffleAccept(parallelToggleBase, noop);
	test_pack(shuf);
	test_pack(rshuf);
	shuf.canonicalize();
	rshuf.canonicalize();
	EXPECT_EQ(*pack(shuf), *pack(rshuf));
}
TEST(AutomatonTest, PackDistinguishesAlphabetSize) {
	EXPECT_NE(*pack(canonicalize(lit<2>(0))), *pack(canonicalize(lit<8>(0))));
}