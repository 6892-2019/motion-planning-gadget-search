#include "precompiled.hpp"
#include "pack.hpp"
#include "pack-detail.hpp"

namespace automaton {

using state_type = AutomatonBase::state_type;
using symbol_type = AutomatonBase::symbol_type;

namespace detail {
	constexpr unsigned int coding_active_alpha = 1 << 23;
	constexpr unsigned int coding_accept_in_outgoing = 1 << 22;
	constexpr unsigned int coding_all = coding_active_alpha | coding_accept_in_outgoing;
}

Pack* pack(const AutomatonBase& a, Pack* first, Pack* last) {
	const state_type state_size = a.state_size();
	const symbol_type alphabet_size = a.alphabet_size();
	constexpr auto FENCE = std::numeric_limits<state_type>::max();
	detail::PackWriter w(first, last);
	//3-byte size/flags at start, but we don't know the size yet
	w.seek(first+3);

	bool use_active_alpha = false;
	SymbolSet active = a.activeAlphabet();
	unsigned int outgoing_mask_bytes = (alphabet_size+7)/8; //round up
	if ((active.size()+7)/8 < outgoing_mask_bytes) {
		//Saves a byte per state, and maybe more if we can put the accept there.
		use_active_alpha = true;
		outgoing_mask_bytes = (active.size()+7)/8;
	} else if (alphabet_size % 8 == 0 && active.size() % 8 != 0)
		//Saves a byte per eight states because we can put accept in the mask.
		use_active_alpha = true;

	unsigned int bits_in_outgoing_mask = use_active_alpha ? active.size() : alphabet_size;
	//Store the accept bit in the outgoing mask if we have any spare bits (even
	//if we didn't use active alpha -- say, if the alphabet size is 6).
	bool accept_in_outgoing_mask = bits_in_outgoing_mask % 8 != 0;

	//TODO: if there are at least as many transitions as states and the state
	//size is less than 128, encode one bit of accept in each transition.  The
	//accept bit isn't necessarily related to the state it's encoded in.  We
	//could also try to use two or more bits for small automata, but they are
	//likely not to have enough transitions.

	bool accept_trailing = !accept_in_outgoing_mask;
	//So we know where transition lists end and the accept bitmask begins.  Also
	//lets us reserve when unpacking.
	if (accept_trailing)
		w.writeVarint(state_size);

	if (use_active_alpha) {
		active.sort(); //Don't need it now, but necessary for later.
		unsigned int mask = 0;
		for (symbol_type q : active)
			mask |= (1 << q);
		//This is the full alphabet size so unpack knows how big it is.
		w.writeBytes(mask, (alphabet_size+7)/8);
	}

	//If pack becomes a template, this could be std::array.
	boost::container::small_vector<state_type, 16> dests;
	dests.assign(alphabet_size, FENCE);
	for (state_type s = 0; s < state_size; ++s) {
		std::fill(dests.begin(), dests.end(), FENCE);
		a.for_each_transition(s, [&](symbol_type a, state_type t) {
			assert(dests[a] == FENCE && "nondeterministic?");
			dests[a] = t;
		});

		auto list_begin = w.tell();
		w.seek(list_begin + outgoing_mask_bytes);
		unsigned int mask = 0;
		if (use_active_alpha) {
			for (symbol_type q : active) {
				if (dests[q] != FENCE) {
					mask |= (1 << q);
					w.writeVarint(dests[q]);
				}
			}
		} else {
			//We could build a SymbolSet with all the symbols and merge this
			//with the above loop.  Better yet, we could use PEXT with one of
			//two masks so we don't loop at all (#34).
			for (symbol_type q = 0; q < alphabet_size; ++q) {
				if (dests[q] != FENCE) {
					mask |= (1 << q);
					w.writeVarint(dests[q]);
				}
			}
		}
		if (accept_in_outgoing_mask && a.accept(s))
			mask |= (1 << bits_in_outgoing_mask);
		auto list_end = w.tell();
		w.seek(list_begin);
		w.writeBytes(mask, outgoing_mask_bytes);
		w.seek(list_end);
	}

	if (accept_trailing) {
		unsigned int acceptmask = 0;
		for (state_type s = 0; s < state_size;) {
			for (unsigned int i = 0; i < 8 && s < state_size; ++i, ++s) {
				//we can safely shift on the first iteration because we start at 0
				acceptmask |= (a.accept(s) << i);
			}
			w.write8(acceptmask);
			acceptmask = 0;
		}
	}

	if (w.overflow())
		return nullptr;
	auto end = w.tell();
	auto size = numeric_cast<unsigned int>(end - first);
	if (use_active_alpha)
		size |= detail::coding_active_alpha;
	if (accept_in_outgoing_mask)
		size |= detail::coding_accept_in_outgoing;
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
	unsigned int size = r.read24();
	bool use_active_alpha = size & detail::coding_active_alpha;
	bool accept_in_outgoing_mask = size & detail::coding_accept_in_outgoing;
	size &= ~detail::coding_all;

	bool accept_trailing = !accept_in_outgoing_mask;
	unsigned int state_size = std::numeric_limits<unsigned int>::max();
	if (accept_trailing) {
		state_size = r.readVarint();
		a.reserve(state_size);
		for (state_type s = 0; s < state_size; ++s)
			a.addState(); //consider addState(unsigned int) to add many states
	}
	//If we don't know the state size to start with, we have to add them as
	//demanded, either when we come to the nth transition list or when state n
	//appears as a destination.
	//TODO: GCC can't see through a.state_size(), so it may be worth tracking it
	//ourselves to save some virtual calls.
	auto ensure_enough_states = [&](state_type s) {
		if (accept_trailing) return s; //already handled above
		while (a.state_size() <= s)
			a.addState();
		return s;
	};

	unsigned int outgoing_mask_bytes = (alphabet_size+7)/8; //round up
	SymbolSet active; //TODO: if constructing this costs us, wrap it in std::optional
	if (use_active_alpha) {
		unsigned int alphamask = r.readBytes(outgoing_mask_bytes);
		for (symbol_type q = 0; q < alphabet_size; ++q)
			if (alphamask & (1 << q))
				active.insert_absent(q);
		outgoing_mask_bytes = (active.size()+7)/8; //round up
	}
	const unsigned int bits_in_outgoing_mask = use_active_alpha ? active.size() : alphabet_size;

	for (state_type s = 0; (!accept_trailing || s < state_size) && !r.eof(); ++s) {
		//TODO: ditch accept_trailing and interleave bytes of accept bits before
		//every (s % 8)th state (including before the first state).  That will
		//save the cost of writing the state size, plus simplify this loop's condition.
		ensure_enough_states(s);
		unsigned int mask = r.readBytes(outgoing_mask_bytes);
		if (use_active_alpha) {
			for (symbol_type q : active)
				if (mask & (1 << q))
					a.addTrans(s, q, ensure_enough_states(r.readVarint()));
		} else {
			for (symbol_type q = 0; q < alphabet_size; ++q)
				if (mask & (1 << q))
					a.addTrans(s, q, ensure_enough_states(r.readVarint()));
		}
		if (accept_in_outgoing_mask && mask & (1 << bits_in_outgoing_mask))
			//It's false if we don't set it, so we save a virtual call by not
			//passing mask & (1 << bits_in_outgoing_mask) as a second argument.
			a.setAccept(s);
	}

	if (accept_trailing) {
		for (state_type s = 0; s < state_size;) {
			unsigned int acceptmask = r.read8();
			for (unsigned int i = 0; i < 8 && s < state_size; ++i, ++s)
				a.setAccept(s, acceptmask & (1 << i));
		}
	}

	a.setFlagsHack(true, true, true);
	return r.overflow() ? nullptr : r.tell();
}

unsigned int packed_size(const Pack* pack) {
	//There are no length-0 packs, so we can advertise four available bytes.
	//That allows a 4-byte load and mask instead of three 1-byte loads.
	return detail::PackReader(pack, pack+4).read24() & ~detail::coding_all;
}
std::size_t packed_hash(const Pack* pack) {
	return farmhash::Hash(reinterpret_cast<const char*>(pack), packed_size(pack));
}
bool packed_equal(const Pack* left, const Pack* right) {
	auto lsize = packed_size(left), rsize = packed_size(right);
	return lsize == rsize && !std::memcmp(left, right, lsize);
}

} //namespace automaton