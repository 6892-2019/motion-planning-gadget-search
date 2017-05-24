#ifndef PACKEDAUTOMATON_DETAIL_HPP
#define PACKEDAUTOMATON_DETAIL_HPP

/**
 * Contains details exposed for testing purposes only.
 */

namespace automaton {
namespace detail {

template<typename T>
using limits = std::numeric_limits<T>;

struct ReinterpretWriter {
	unsigned char* p;
	//Because we're required to specify T at the call site, we can't name this
	//operator().  (Well, we could if we called it as w.operator()<T>(arg).)
	template<typename T, typename V>
	void write(V value) {
		*reinterpret_cast<T*>(p) = numeric_cast<T>(value);
		p += sizeof(T);
	}
};

/**
 * Stores the accept bit in the high bit of the offset.
 */
template<typename OutgoingMaskType, typename StateSizeType, typename OffsetType>
class OffsetAcceptAutomaton final : public PackedAutomaton {
public:
	static bool can_represent(const AutomatonBase& a) {
		return a.alphabet_size() <= limits<OutgoingMaskType>::digits &&
				a.state_size() <= limits<StateSizeType>::max() &&
				a.transition_size() <= (limits<OffsetType>::max() >> 1);
	}
	static std::size_t extra_storage(const AutomatonBase& a) {
		return 1 //alphabet size
				+ sizeof(StateSizeType)
				+ sizeof(OutgoingMaskType) * a.state_size()
				+ sizeof(OffsetType) * a.state_size()
				+ sizeof(StateSizeType) * a.transition_size();
	}

	OffsetAcceptAutomaton(const AutomatonBase& a) {
		assert(can_represent(a));
		ReinterpretWriter p = {storage_begin()};
		p.write<StateSizeType>(a.state_size());
		p.write<unsigned char>(a.alphabet_size());

		//TODO: if we know where offsets start, we can make this one big loop,
		//so we only call a.outgoing(s) once
		for (state_type s = 0; s < a.state_size(); ++s)
			p.write<OutgoingMaskType>(set_to_mask(a.outgoing(s)));
		OffsetType offset = 0;
		for (state_type s = 0; s < a.state_size(); ++s) {
			p.write<OffsetType>(offset | (a.accept(s) ? 1 << (limits<OffsetType>::digits - 1) : 0));
			//TODO: want an overflow-checked add here, I guess
			offset = numeric_cast<OffsetType>(offset + __builtin_popcount(outgoing_mask(s)));
		}
		for (state_type s = 0; s < a.state_size(); ++s) {
			SymbolSet syms = a.outgoing(s);
			syms.sort();
			for (symbol_type c : syms) {
				auto next = a.stepDeterministic(s, c);
				assert(next);
				p.write<StateSizeType>(*next);
			}
		}
		assert(p.p == storage_end());
	}

	state_type state_size() const override {
		return *reinterpret_cast<const StateSizeType*>(storage_begin());
	}
	symbol_type alphabet_size() const override {
		return *(storage_begin() + sizeof(StateSizeType));
	}
	AutomatonBase::symbol_type active_alphabet_size() const override {
		OutgoingMaskType mask = 0;
		for (OutgoingMaskType m : make_range_for_pair(outgoing_begin(), outgoing_end()))
			mask |= m; //in theory, we could short-circuit if all bits are set
		return __builtin_popcount(mask);
	}
	AutomatonBase::state_type accept_size() const override {
		state_type count = 0;
		for (OffsetType o : make_range_for_pair(offsets_begin(), offsets_end()))
			count += (o >> (limits<OffsetType>::digits - 1)); //1 if the top bit is set, else 0
		return count;
	}
	std::size_t transition_size() const override {
		std::size_t count = 0;
		//TODO: make this vectorizable/unrollable, not byte-at-a-time
		for (OutgoingMaskType m : make_range_for_pair(outgoing_begin(), outgoing_end()))
			count += __builtin_popcount(m);
		return count;
	}
	bool deterministic() const override {return true;}
	bool minimal() const override {return true;}
	bool canonical() const override {return true;}
	bool accept(state_type state) const override {
		return offset(state) & (1 << (limits<OffsetType>::digits - 1));
	}
	StateSet step(state_type state, symbol_type symbol) const override {
		if (auto next = stepDeterministic(state, symbol)) {
			StateSet set;
			set.insert_absent(*next);
			return set;
		}
		return {};
	}
	std::optional<state_type> stepDeterministic(state_type state, symbol_type symbol) const override {
		OutgoingMaskType outgoing = outgoing_mask(state);
		if (!(outgoing & (1u << symbol))) return std::nullopt;
		//how many symbols came before
		auto suboffset = count_set_left(outgoing, symbol);
		return *(destinations_begin(state) + suboffset);
	}
	AutomatonBase::SymbolSet outgoing(state_type state) const override {
		SymbolSet ret;
		OutgoingMaskType mask = outgoing_mask(state);
		for (unsigned int i = 0; i < alphabet_size(); ++i)
			if (mask & (1 << i))
				ret.insert_absent(i);
		return ret;
	}
	AutomatonBase::StateSet destinations(state_type state) const override {
		StateSet ret;
		for (StateSizeType s : make_range_for_pair(destinations_begin(state), destinations_end(state)))
			ret.insert_absent(s);
		return ret;
	}

private:
	unsigned char* storage_begin() {
		return reinterpret_cast<unsigned char*>(this) + sizeof(*this);
	}
	const unsigned char* storage_begin() const override {
		return reinterpret_cast<const unsigned char*>(this) + sizeof(*this);
	}
	const unsigned char* storage_end() const override {
		return storage_begin() + extra_storage(*this); //TODO: slow computation
	}
	const OutgoingMaskType* outgoing_begin() const {
		return reinterpret_cast<const OutgoingMaskType*>(storage_begin() + sizeof(StateSizeType) + 1);
	}
	const OutgoingMaskType* outgoing_end() const {
		return outgoing_begin() + state_size();
	}
	OutgoingMaskType outgoing_mask(state_type state) const {
		return outgoing_begin()[state];
	}

	const OffsetType* offsets_begin() const {
		return reinterpret_cast<const OffsetType*>(outgoing_end());
	}
	const OffsetType* offsets_end() const {
		return offsets_begin() + state_size();
	}
	OffsetType offset(state_type state) const {
		return offsets_begin()[state];
	}

	const StateSizeType* destinations_begin(state_type state) const {
		return reinterpret_cast<const StateSizeType*>(offsets_end()) + (offset(state) & ~(1 << (limits<OffsetType>::digits - 1)));
	}
	const StateSizeType* destinations_end(state_type state) const {
		return destinations_begin(state) + __builtin_popcount(outgoing_mask(state));
	}

	static OutgoingMaskType set_to_mask(SymbolSet set) {
		std::size_t mask = 0;
		for (symbol_type s : set)
			mask |= 1u << s;
		return numeric_cast<OutgoingMaskType>(mask);
	}
	static unsigned int high_zeroes(unsigned int x) {
		//fxtbook 1.6.2, page 17
		x |= x >> 1;
		x |= x >> 2;
		x |= x >> 4;
		x |= x >> 8;
		x |= x >> 16;
		return ~x;
	}
	/**
	 * @return the number of set bits in x to the left of and including position pos
	 */
	static unsigned int count_set_left(unsigned int x, unsigned int pos) {
		return __builtin_popcount(x & ~(high_zeroes(1u << pos) | 1 << pos));
	}
};

using Diminutive8OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned char, unsigned char, unsigned char>;
using Tiny8OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned char, unsigned char, unsigned short>;
using Small8OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned char, unsigned short, unsigned short>;
using Medium8OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned char, unsigned short, unsigned int>;
using Large8OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned char, unsigned int, unsigned int>;

/**
 * Stores the accept bit in a bitmask between the outgoing masks and the offset.
 */
template<typename OutgoingMaskType, typename StateSizeType, typename OffsetType>
class BitmaskAcceptAutomaton final : public PackedAutomaton {
public:
	static bool can_represent(const AutomatonBase& a) {
		return a.alphabet_size() <= limits<OutgoingMaskType>::digits &&
				a.state_size() <= limits<StateSizeType>::max() &&
				a.transition_size() <= limits<OffsetType>::max();
	}
	static std::size_t extra_storage(const AutomatonBase& a) {
		return 1 //alphabet size
				+ sizeof(StateSizeType)
				+ sizeof(OutgoingMaskType) * a.state_size()
				+ sizeof(unsigned char) * div8roundup(a.state_size()) //accept bitmask
				+ sizeof(OffsetType) * a.state_size()
				+ sizeof(StateSizeType) * a.transition_size();
	}

	BitmaskAcceptAutomaton(const AutomatonBase& a) {
		assert(can_represent(a));
		ReinterpretWriter p = {storage_begin()};
		p.write<StateSizeType>(a.state_size());
		p.write<unsigned char>(a.alphabet_size());

		//TODO: if we know where offsets start, we can make this one big loop,
		//so we only call a.outgoing(s) once
		for (state_type s = 0; s < a.state_size(); ++s)
			p.write<OutgoingMaskType>(set_to_mask(a.outgoing(s)));
		unsigned int acceptmask = 0;
		for (state_type s = 0; s < a.state_size();) {
			for (unsigned int i = 0; i < 8 && s < a.state_size(); ++i, ++s) {
				//we can safely shift on the first iteration because we start at 0
				acceptmask |= (a.accept(s) << i);
			}
			p.write<unsigned char>(acceptmask);
			acceptmask = 0;
		}
		OffsetType offset = 0;
		for (state_type s = 0; s < a.state_size(); ++s) {
			p.write<OffsetType>(offset);
			//TODO: want an overflow-checked add here, I guess
			offset = numeric_cast<OffsetType>(offset + __builtin_popcount(outgoing_mask(s)));
		}
		for (state_type s = 0; s < a.state_size(); ++s) {
			SymbolSet syms = a.outgoing(s);
			syms.sort();
			for (symbol_type c : syms) {
				auto next = a.stepDeterministic(s, c);
				assert(next);
				p.write<StateSizeType>(*next);
			}
		}
		assert(p.p == storage_end());
	}

	state_type state_size() const override {
		return *reinterpret_cast<const StateSizeType*>(storage_begin());
	}
	symbol_type alphabet_size() const override {
		return *(storage_begin() + sizeof(StateSizeType));
	}
	AutomatonBase::symbol_type active_alphabet_size() const override {
		OutgoingMaskType mask = 0;
		for (OutgoingMaskType m : make_range_for_pair(outgoing_begin(), outgoing_end()))
			mask |= m; //in theory, we could short-circuit if all bits are set
		return __builtin_popcount(mask);
	}
	AutomatonBase::state_type accept_size() const override {
		state_type count = 0;
		for (unsigned char o : make_range_for_pair(accepts_begin(), accepts_end()))
			count += __builtin_popcount(o);
		return count;
	}
	std::size_t transition_size() const override {
		std::size_t count = 0;
		//TODO: make this vectorizable/unrollable, not byte-at-a-time
		for (OutgoingMaskType m : make_range_for_pair(outgoing_begin(), outgoing_end()))
			count += __builtin_popcount(m);
		return count;
	}
	bool deterministic() const override {return true;}
	bool minimal() const override {return true;}
	bool canonical() const override {return true;}
	bool accept(state_type state) const override {
		return accepts_begin()[state / 8] & (1u << (state % 8));
	}
	StateSet step(state_type state, symbol_type symbol) const override {
		if (auto next = stepDeterministic(state, symbol)) {
			StateSet set;
			set.insert_absent(*next);
			return set;
		}
		return {};
	}
	std::optional<state_type> stepDeterministic(state_type state, symbol_type symbol) const override {
		OutgoingMaskType outgoing = outgoing_mask(state);
		if (!(outgoing & (1u << symbol))) return std::nullopt;
		//how many symbols came before
		auto suboffset = count_set_left(outgoing, symbol);
		return *(destinations_begin(state) + suboffset);
	}
	AutomatonBase::SymbolSet outgoing(state_type state) const override {
		SymbolSet ret;
		OutgoingMaskType mask = outgoing_mask(state);
		for (unsigned int i = 0; i < alphabet_size(); ++i)
			if (mask & (1 << i))
				ret.insert_absent(i);
		return ret;
	}
	AutomatonBase::StateSet destinations(state_type state) const override {
		StateSet ret;
		for (StateSizeType s : make_range_for_pair(destinations_begin(state), destinations_end(state)))
			ret.insert_absent(s);
		return ret;
	}

private:
	unsigned char* storage_begin() {
		return reinterpret_cast<unsigned char*>(this) + sizeof(*this);
	}
	const unsigned char* storage_begin() const override {
		return reinterpret_cast<const unsigned char*>(this) + sizeof(*this);
	}
	const unsigned char* storage_end() const override {
		return storage_begin() + extra_storage(*this); //TODO: slow computation
	}
	const OutgoingMaskType* outgoing_begin() const {
		return reinterpret_cast<const OutgoingMaskType*>(storage_begin() + sizeof(StateSizeType) + 1);
	}
	const OutgoingMaskType* outgoing_end() const {
		return outgoing_begin() + state_size();
	}
	OutgoingMaskType outgoing_mask(state_type state) const {
		return outgoing_begin()[state];
	}

	const unsigned char* accepts_begin() const {
		return reinterpret_cast<const unsigned char*>(outgoing_end());
	}
	const unsigned char* accepts_end() const {
		return accepts_begin() + div8roundup(state_size());
	}

	const OffsetType* offsets_begin() const {
		return reinterpret_cast<const OffsetType*>(accepts_end());
	}
	const OffsetType* offsets_end() const {
		return offsets_begin() + state_size();
	}
	OffsetType offset(state_type state) const {
		return offsets_begin()[state];
	}

	const StateSizeType* destinations_begin(state_type state) const {
		return reinterpret_cast<const StateSizeType*>(offsets_end()) + offset(state);
	}
	const StateSizeType* destinations_end(state_type state) const {
		return destinations_begin(state) + __builtin_popcount(outgoing_mask(state));
	}

	static OutgoingMaskType set_to_mask(SymbolSet set) {
		std::size_t mask = 0;
		for (symbol_type s : set)
			mask |= 1u << s;
		return numeric_cast<OutgoingMaskType>(mask);
	}
	static unsigned int high_zeroes(unsigned int x) {
		//fxtbook 1.6.2, page 17
		x |= x >> 1;
		x |= x >> 2;
		x |= x >> 4;
		x |= x >> 8;
		x |= x >> 16;
		return ~x;
	}
	/**
	 * @return the number of set bits in x to the left of and including position pos
	 */
	static unsigned int count_set_left(unsigned int x, unsigned int pos) {
		return __builtin_popcount(x & ~(high_zeroes(1u << pos) | 1 << pos));
	}
	static unsigned int div8roundup(unsigned int x) {
		//udiv by 8 is rshift by 3; remainder if any of the shifted-out bits are set
		return (x >> 3) + ((x & 0b111) > 0);
	}
};

using Diminutive8BitmaskPackedAutomaton = BitmaskAcceptAutomaton<unsigned char, unsigned char, unsigned char>;
using Tiny8BitmaskPackedAutomaton = BitmaskAcceptAutomaton<unsigned char, unsigned char, unsigned short>;
using Small8BitmaskPackedAutomaton = BitmaskAcceptAutomaton<unsigned char, unsigned short, unsigned short>;
using Medium8BitmaskPackedAutomaton = BitmaskAcceptAutomaton<unsigned char, unsigned short, unsigned int>;
using Large8BitmaskPackedAutomaton = BitmaskAcceptAutomaton<unsigned char, unsigned int, unsigned int>;

template<class A>
std::unique_ptr<const PackedAutomaton> make_pack(const AutomatonBase& a) {
	void* storage = operator new(sizeof(A) + A::extra_storage(a));
	auto* p = new (storage) A(a);
	return std::unique_ptr<const PackedAutomaton>(p);
}

} //namespace detail
} //namespace automaton

#endif /* PACKEDAUTOMATON_DETAIL_HPP */

