#include "precompiled.hpp"
#include "pack.hpp"
#include "pack-detail.hpp"

namespace automaton {

using state_type = AutomatonBase::state_type;
using symbol_type = AutomatonBase::symbol_type;

Pack* pack(const AutomatonBase& a, Pack* first, Pack* last) {
	const state_type state_size = a.state_size();
	const symbol_type alphabet_size = a.alphabet_size();
	constexpr auto FENCE = std::numeric_limits<state_type>::max();
	detail::PackWriter w(first, last);
	//3-byte size/flags at start, but we don't know the size yet
	w.seek(first+3);
	//So we know where transition lists end and the accept bitmask begins.
	//TODO: when storing the acceptness bit somewhere in the transition list,
	//we could omit this, though it also lets us reserve when unpacking.
	w.writeVarint(state_size);

	//If pack becomes a template, this could be std::array.
	boost::container::small_vector<state_type, 16> dests;
	dests.assign(alphabet_size, FENCE);
	const unsigned int outgoing_mask_bytes = (alphabet_size+7)/8; //round up
	for (state_type s = 0; s < state_size; ++s) {
		std::fill(dests.begin(), dests.end(), FENCE);
		a.for_each_transition(s, [&](symbol_type a, state_type t) {
			assert(dests[a] == FENCE && "nondeterministic?");
			dests[a] = t;
		});

		auto list_begin = w.tell();
		w.seek(list_begin + outgoing_mask_bytes);
		unsigned int mask = 0;
		for (symbol_type q = 0; q < alphabet_size; ++q) {
			//TODO: if using active alphabet compression, instead iterate over
			//the sorted active alphabet set
			if (dests[q] != FENCE) {
				mask |= (1 << q);
				w.writeVarint(dests[q]);
			}
		}
		auto list_end = w.tell();
		w.seek(list_begin);
		w.writeBytes(mask, outgoing_mask_bytes);
		w.seek(list_end);
	}

	unsigned int acceptmask = 0;
	for (state_type s = 0; s < state_size;) {
		for (unsigned int i = 0; i < 8 && s < state_size; ++i, ++s) {
			//we can safely shift on the first iteration because we start at 0
			acceptmask |= (a.accept(s) << i);
		}
		w.write8(acceptmask);
		acceptmask = 0;
	}

	if (w.overflow())
		return nullptr;
	auto end = w.tell();
	auto size = numeric_cast<unsigned int>(end - first);
	w.seek(first);
	w.write24(size);
	return end;
}

const Pack* unpack(WorkingAutomaton& a, const Pack* first, const Pack* last) {
	assert(a.state_size() == 0);
	const symbol_type alphabet_size = a.alphabet_size();
	if (!last)
		last = first + packed_size(first);
	detail::PackReader r(first, last);
	MAYBE_UNUSED const unsigned int size = r.read24();
	const unsigned int state_size = r.readVarint();
	a.reserve(state_size);
	for (state_type s = 0; s < state_size; ++s)
		a.addState(); //consider addState(unsigned int) to add many states

	const unsigned int outgoing_mask_bytes = (alphabet_size+7)/8; //round up
	for (state_type s = 0; s < state_size; ++s) {
		unsigned int mask = r.readBytes(outgoing_mask_bytes);
		for (symbol_type q = 0; q < alphabet_size; ++q)
			if (mask & (1 << q))
				a.addTrans(s, q, r.readVarint());
	}

	for (state_type s = 0; s < state_size;) {
		unsigned int acceptmask = r.read8();
		for (unsigned int i = 0; i < 8 && s < state_size; ++i, ++s)
			a.setAccept(s, acceptmask & (1 << i));
	}

	a.setFlagsHack(true, true, true);
	return r.overflow() ? nullptr : r.tell();
}

unsigned int packed_size(const Pack* pack) {
	//If we add any coding flags, we need to mask them out here.
	return detail::PackReader(pack, pack+3).read24();
}
std::size_t packed_hash(const Pack* pack) {
	return farmhash::Hash(reinterpret_cast<const char*>(pack), packed_size(pack));
}
bool packed_equal(const Pack* left, const Pack* right) {
	return std::equal(left, left+packed_size(left), right, right+packed_size(right));
}

} //namespace automaton