#include "precompiled.hpp"
#include "automaton.hpp"
#include "packedautomaton.hpp"
#include "packedautomaton-detail.hpp"
#include "automaton-io.hpp"
#include "util.hpp"
#include <doctest.h>

using namespace automaton;
using state_type = AutomatonBase::state_type;
using symbol_type = AutomatonBase::symbol_type;

namespace {
using MakePackPtr = std::unique_ptr<const PackedAutomaton>(*)(const AutomatonBase& a);
void pack_impl_test(const AutomatonBase& a, MakePackPtr make) {
	assert(a.canonical());
	std::unique_ptr<const PackedAutomaton> packed = make(a);
	CHECK_EQ(packed->state_size(), a.state_size());
	CHECK_EQ(packed->alphabet_size(), a.alphabet_size());
	CHECK_EQ(packed->accept_size(), a.accept_size());
	CHECK_EQ(packed->edge_size(), a.edge_size());
	CHECK_EQ(packed->transition_size(), a.transition_size());
	CHECK_EQ(packed->active_alphabet_size(), a.active_alphabet_size());
	CHECK_EQ(packed->activeAlphabet(), a.activeAlphabet());
	for (state_type s = 0; s < a.state_size(); ++s) {
		CHECK_EQ(packed->accept(s), a.accept(s)); //OLDTEST: s;
		CHECK_EQ(packed->outgoing(s), a.outgoing(s)); //OLDTEST: s;
		CHECK_EQ(packed->destinations(s), a.destinations(s)); //OLDTEST: s;
		for (symbol_type c = 0; c < a.alphabet_size(); ++c) {
			CHECK_EQ(packed->step(s, c), a.step(s, c)); //OLDTEST: s << " " << c;
			CHECK_EQ(packed->stepDeterministic(s, c), a.stepDeterministic(s, c)); //OLDTEST: s << " " << c;
		}
		for (state_type t = 0; t < a.state_size(); ++t)
			CHECK_EQ(packed->labels(s, t), a.labels(s, t)); //OLDTEST: s << " " << t;
	}
	CHECK_EQ(packed->hash(), a.hash());
	CHECK_EQ(*packed, a);

	std::unique_ptr<const PackedAutomaton> repacked = make(*packed);
	CHECK_EQ(*repacked, *packed);
	CHECK_EQ(repacked->packed_hash(), packed->packed_hash());

	packed = pack(*packed); //allow type to be freely chosen
	repacked = pack(*repacked);
	CHECK_EQ(*repacked, *packed);
	CHECK_EQ(repacked->packed_hash(), packed->packed_hash());
}

void test_pack(WorkingAutomaton& a) {
	a.canonicalize();
#define TEST_PACK(IMPL) pack_impl_test(a, &detail::make_pack<IMPL>);
	TEST_PACK(detail::Diminutive8OffsetPackedAutomaton)
	TEST_PACK(detail::Tiny8OffsetPackedAutomaton)
	TEST_PACK(detail::Small8OffsetPackedAutomaton)
	TEST_PACK(detail::Medium8OffsetPackedAutomaton)
	TEST_PACK(detail::Large8OffsetPackedAutomaton)
	TEST_PACK(detail::Diminutive8OutgoingPackedAutomaton)
	TEST_PACK(detail::Tiny8OutgoingPackedAutomaton)
	TEST_PACK(detail::Small8OutgoingPackedAutomaton)
	TEST_PACK(detail::Medium8OutgoingPackedAutomaton)
	TEST_PACK(detail::Large8OutgoingPackedAutomaton)
	TEST_PACK(detail::Diminutive8BitmaskPackedAutomaton)
	TEST_PACK(detail::Tiny8BitmaskPackedAutomaton)
	TEST_PACK(detail::Small8BitmaskPackedAutomaton)
	TEST_PACK(detail::Medium8BitmaskPackedAutomaton)
	TEST_PACK(detail::Large8BitmaskPackedAutomaton)

	TEST_PACK(detail::Diminutive16OffsetPackedAutomaton)
	TEST_PACK(detail::Tiny16OffsetPackedAutomaton)
	TEST_PACK(detail::Small16OffsetPackedAutomaton)
	TEST_PACK(detail::Medium16OffsetPackedAutomaton)
	TEST_PACK(detail::Large16OffsetPackedAutomaton)
	TEST_PACK(detail::Diminutive16OutgoingPackedAutomaton)
	TEST_PACK(detail::Tiny16OutgoingPackedAutomaton)
	TEST_PACK(detail::Small16OutgoingPackedAutomaton)
	TEST_PACK(detail::Medium16OutgoingPackedAutomaton)
	TEST_PACK(detail::Large16OutgoingPackedAutomaton)
	TEST_PACK(detail::Diminutive16BitmaskPackedAutomaton)
	TEST_PACK(detail::Tiny16BitmaskPackedAutomaton)
	TEST_PACK(detail::Small16BitmaskPackedAutomaton)
	TEST_PACK(detail::Medium16BitmaskPackedAutomaton)
	TEST_PACK(detail::Large16BitmaskPackedAutomaton)
}
void test_pack(WorkingAutomaton&& a) {
	//Only const lvalue refs can bind rvalues, but in this case we really do
	//want to mutate temporaries (to canonicalize them), so this overload binds
	//them and delegates to the lvalue ref overload.
	return test_pack(a);
}

void test_pack_if_representable(WorkingAutomaton& a) {
	a.canonicalize();
	detail::PackStats stats{a};
#define TEST_PACK_IF_REPRESENTABLE(IMPL) if (IMPL::can_represent(a, stats)) \
											TEST_PACK(IMPL)
	TEST_PACK_IF_REPRESENTABLE(detail::Diminutive8OffsetPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Tiny8OffsetPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Small8OffsetPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Medium8OffsetPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Large8OffsetPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Diminutive8OutgoingPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Tiny8OutgoingPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Small8OutgoingPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Medium8OutgoingPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Large8OutgoingPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Diminutive8BitmaskPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Tiny8BitmaskPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Small8BitmaskPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Medium8BitmaskPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Large8BitmaskPackedAutomaton)

	TEST_PACK_IF_REPRESENTABLE(detail::Diminutive16OffsetPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Tiny16OffsetPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Small16OffsetPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Medium16OffsetPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Large16OffsetPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Diminutive16OutgoingPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Tiny16OutgoingPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Small16OutgoingPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Medium16OutgoingPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Large16OutgoingPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Diminutive16BitmaskPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Tiny16BitmaskPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Small16BitmaskPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Medium16BitmaskPackedAutomaton)
	TEST_PACK_IF_REPRESENTABLE(detail::Large16BitmaskPackedAutomaton)
#undef TEST_PACK_IF_REPRESENTABLE
#undef TEST_PACK
}
MAYBE_UNUSED void test_pack_if_representable(WorkingAutomaton&& a) {
	//See comment in test_pack overload above.
	return test_pack_if_representable(a);
}
} //anonymous namespace

TEST_CASE("AutomatonTest_Pack0") {
	test_pack(lit<7>(0, 1, 2, 3));
}
TEST_CASE("AutomatonTest_Pack1") {
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
	CHECK_EQ(*pack(shuf), *pack(rshuf));
}
TEST_CASE("AutomatonTest_PackDistinguishesAlphabetSize") {
	CHECK_NE(*pack(canonicalize(lit<2>(0))), *pack(canonicalize(lit<8>(0))));
}

TEST_CASE("AutomatonTest_Roundtrip") {
	auto noop = star(alt(lit<4>(0, 0), lit<4>(1, 1), lit<4>(2, 2), lit<4>(3, 3)));
	auto ltr = alt(lit<4>(0, 1), lit<4>(3, 2)), rtl = alt(lit<4>(1, 0), lit<4>(2, 3));
	auto parallelToggleBase = alt(epsilon<4>(), ltr, star(cat(ltr, rtl)), cat(ltr, star(cat(rtl, ltr))));
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	shuf.canonicalize();
	std::unique_ptr<const PackedAutomaton> packed = pack(shuf);
	Automaton<4> inflated(*packed);
	REQUIRE_EQ(inflated, shuf);
}

TEST_CASE("AutomatonTest_PackDestinations") {
	Automaton<4> a;
	a.addState();
	a.addState();
	a.setAccept(1);
	a.addTrans(0, 0, 1);
	a.addTrans(0, 1, 1);
	a.addTrans(0, 3, 1);
	a.canonicalize();
	test_pack(a);
}

TEST_CASE("AutomatonTest_Pack2719215598098816079") {
	test_pack_if_representable(*deserialize<16>("data/test/16-351-864-972-det-2719215598098816079.auto"));
}