#include "precompiled.hpp"
#include "packedautomaton.hpp"
#include "packedautomaton-detail.hpp"

using namespace automaton;
using namespace automaton::detail;
using state_type = AutomatonBase::state_type;
using symbol_type = AutomatonBase::symbol_type;
using StateSet = AutomatonBase::StateSet;
using SymbolSet = AutomatonBase::SymbolSet;
template<typename T>
using limits = std::numeric_limits<T>;

namespace automaton {

bool operator==(const PackedAutomaton& left, const PackedAutomaton& right) {
	auto lb = left.storage_begin(), le = left.storage_end(), rb = right.storage_begin(), re = right.storage_end();
	return (le - lb) == (re - rb) && std::memcmp(lb, rb, le - lb) == 0;
}
std::size_t PackedAutomaton::packed_hash() const {
	auto begin = storage_begin(), end = storage_end();
	return farmhash::Hash(reinterpret_cast<const char*>(begin), std::distance(begin, end));
}

namespace detail {

template class OffsetAcceptAutomaton<unsigned char, unsigned char, unsigned char>;
template class OffsetAcceptAutomaton<unsigned char, unsigned char, unsigned short>;
template class OffsetAcceptAutomaton<unsigned char, unsigned short, unsigned short>;
template class OffsetAcceptAutomaton<unsigned char, unsigned short, unsigned int>;
template class OffsetAcceptAutomaton<unsigned char, unsigned int, unsigned int>;
template class OffsetAcceptAutomaton<unsigned short, unsigned char, unsigned char>;
template class OffsetAcceptAutomaton<unsigned short, unsigned char, unsigned short>;
template class OffsetAcceptAutomaton<unsigned short, unsigned short, unsigned short>;
template class OffsetAcceptAutomaton<unsigned short, unsigned short, unsigned int>;
template class OffsetAcceptAutomaton<unsigned short, unsigned int, unsigned int>;
template class OutgoingAcceptAutomaton<unsigned char, unsigned char, unsigned char>;
template class OutgoingAcceptAutomaton<unsigned char, unsigned char, unsigned short>;
template class OutgoingAcceptAutomaton<unsigned char, unsigned short, unsigned short>;
template class OutgoingAcceptAutomaton<unsigned char, unsigned short, unsigned int>;
template class OutgoingAcceptAutomaton<unsigned char, unsigned int, unsigned int>;
template class OutgoingAcceptAutomaton<unsigned short, unsigned char, unsigned char>;
template class OutgoingAcceptAutomaton<unsigned short, unsigned char, unsigned short>;
template class OutgoingAcceptAutomaton<unsigned short, unsigned short, unsigned short>;
template class OutgoingAcceptAutomaton<unsigned short, unsigned short, unsigned int>;
template class OutgoingAcceptAutomaton<unsigned short, unsigned int, unsigned int>;
template class BitmaskAcceptAutomaton<unsigned char, unsigned char, unsigned char>;
template class BitmaskAcceptAutomaton<unsigned char, unsigned char, unsigned short>;
template class BitmaskAcceptAutomaton<unsigned char, unsigned short, unsigned short>;
template class BitmaskAcceptAutomaton<unsigned char, unsigned short, unsigned int>;
template class BitmaskAcceptAutomaton<unsigned char, unsigned int, unsigned int>;
template class BitmaskAcceptAutomaton<unsigned short, unsigned char, unsigned char>;
template class BitmaskAcceptAutomaton<unsigned short, unsigned char, unsigned short>;
template class BitmaskAcceptAutomaton<unsigned short, unsigned short, unsigned short>;
template class BitmaskAcceptAutomaton<unsigned short, unsigned short, unsigned int>;
template class BitmaskAcceptAutomaton<unsigned short, unsigned int, unsigned int>;

//TODO: I'd love to make this a variadic template (over the impl types), but I don't know how
std::unique_ptr<const PackedAutomaton> make_best_pack(const AutomatonBase& a) {
	std::size_t best_extra = std::numeric_limits<std::size_t>::max();
	std::unique_ptr<const PackedAutomaton> (*make_fn)(const AutomatonBase&) = nullptr;
#define MAKE_BEST_PACK_ATTEMPT(IMPL) if (IMPL::can_represent(a)) { \
										std::size_t wanted = IMPL::extra_storage(a); \
										if (wanted < best_extra) { \
											best_extra = wanted; \
											make_fn = make_pack<IMPL>; \
										} \
									}
	MAKE_BEST_PACK_ATTEMPT(Diminutive8OffsetPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Tiny8OffsetPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Small8OffsetPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Medium8OffsetPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Large8OffsetPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Diminutive8OutgoingPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Tiny8OutgoingPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Small8OutgoingPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Medium8OutgoingPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Large8OutgoingPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Diminutive8BitmaskPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Tiny8BitmaskPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Small8BitmaskPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Medium8BitmaskPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Large8BitmaskPackedAutomaton)

	MAKE_BEST_PACK_ATTEMPT(Diminutive16OffsetPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Tiny16OffsetPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Small16OffsetPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Medium16OffsetPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Large16OffsetPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Diminutive16OutgoingPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Tiny16OutgoingPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Small16OutgoingPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Medium16OutgoingPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Large16OutgoingPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Diminutive16BitmaskPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Tiny16BitmaskPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Small16BitmaskPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Medium16BitmaskPackedAutomaton)
	MAKE_BEST_PACK_ATTEMPT(Large16BitmaskPackedAutomaton)
	return make_fn(a);
}
}

std::unique_ptr<const PackedAutomaton> pack(const AutomatonBase& a) {
	assert(a.canonical());
	return detail::make_best_pack(a);
}
} //namespace automaton