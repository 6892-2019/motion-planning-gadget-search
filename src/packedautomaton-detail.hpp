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
class OffsetAcceptAutomaton : public PackedAutomaton {
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

template<class A>
std::unique_ptr<const PackedAutomaton> make_pack(const AutomatonBase& a) {
	void* storage = operator new(sizeof(A) + A::extra_storage(a));
	auto* p = new (storage) A(a);
	return std::unique_ptr<const PackedAutomaton>(p);
}

} //namespace detail
} //namespace automaton

#endif /* PACKEDAUTOMATON_DETAIL_HPP */

