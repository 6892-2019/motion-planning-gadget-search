#include "precompiled.hpp"
#include "automaton.hpp"
#include "pack.hpp"
#include "pack-detail.hpp"
#include "automaton-io.hpp"
#include "util.hpp"
#include <doctest.h>

using namespace automaton;
using state_type = AutomatonBase::state_type;
using symbol_type = AutomatonBase::symbol_type;

TEST_CASE("AutomatonTest_VarintRoundtrip") {
	for (unsigned int i = 0; i < 70000; ++i) {
		std::array<std::byte, 8> data;
		std::fill(data.begin(), data.end(), std::byte{0});

		detail::PackWriter writer(data.begin(), data.end());
		writer.writeVarint(i);
		CHECK_UNARY_FALSE(writer.overflow());

		detail::PackReader reader(data.begin(), data.end());
		unsigned int recovered = reader.readVarint();
		CHECK_EQ(recovered, i);
		CHECK_UNARY_FALSE(reader.overflow());
		CHECK_EQ(writer.tell(), reader.tell());
	}
}

namespace {
void test_pack(WorkingAutomaton& a) {
	a.canonicalize();
	dynarray<std::byte> data(16*1024*1024);
	Pack* pack_end = pack(a, data.begin(), data.end());
	auto worker = make_working(a.alphabet_size());
	const Pack* unpack_end = unpack(*worker, data.begin(), pack_end);
	CHECK_EQ(pack_end, unpack_end);
	CHECK_EQ(*worker, a);
}
void test_pack(WorkingAutomaton&& a) {
	//Only const lvalue refs can bind rvalues, but in this case we really do
	//want to mutate temporaries (to canonicalize them), so this overload binds
	//them and delegates to the lvalue ref overload.
	test_pack(a);
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
	auto shuf = shuffleAccept(noop, parallelToggleBase);
	test_pack(shuf);
}

TEST_CASE("AutomatonTest_PackDestinations") {
	Automaton<4> a;
	a.addState();
	a.addState();
	a.setAccept(1);
	a.addTrans(0, 0, 1);
	a.addTrans(0, 1, 1);
	a.addTrans(0, 3, 1);
	test_pack(a);
}

TEST_CASE("AutomatonTest_Pack2719215598098816079") {
	test_pack(*deserialize<16>("data/test/16-351-864-972-det-2719215598098816079.auto"));
}