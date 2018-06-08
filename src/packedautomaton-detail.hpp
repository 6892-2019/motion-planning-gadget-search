#ifndef PACKEDAUTOMATON_DETAIL_HPP
#define PACKEDAUTOMATON_DETAIL_HPP

/**
 * Contains details exposed for testing purposes only.
 */

namespace automaton {
namespace detail {

struct PackStats {
	PackStats(const AutomatonBase& a) : alphabet_size(a.alphabet_size()),
			state_size(a.state_size()), transition_size(a.transition_size()) {}
	symbol_type alphabet_size;
	state_type state_size;
	std::size_t transition_size;
};

/**
 * @return the number of set bits in x below and including position pos
 */
inline unsigned int count_set_lowbits(unsigned int x, unsigned int pos) {
	return __builtin_popcount(x & ((1 << pos) - 1));
}

template<typename OutgoingMaskType>
[[gnu::pure]] OutgoingMaskType set_to_mask(SymbolSet set) {
	std::size_t mask = 0;
	for (symbol_type s : set)
		mask |= 1u << s;
	return numeric_cast<OutgoingMaskType>(mask);
}

template<typename T>
using limits = std::numeric_limits<T>;

struct ReinterpretWriter {
	unsigned char* p;
	//Because we're required to specify T at the call site, we can't name this
	//operator().  (Well, we could if we called it as w.operator()<T>(arg).)
	template<typename T, typename V>
	void write(V value) {
		T source = numeric_cast<T>(value);
		memcpy(p, &source, sizeof(source));
		p += sizeof(source);
	}
};

template<typename T>
T load(const T* ptr) {
	//dance around alignment restrictions
	T thing;
	memcpy(&thing, ptr, sizeof(T));
	return thing;
}

/**
 * Stores the accept bit in the high bit of the offset.
 */
template<typename OutgoingMaskType, typename StateSizeType, typename OffsetType>
class OffsetAcceptAutomaton final : public PackedAutomaton {
public:
	static bool can_represent(const AutomatonBase& a, PackStats s) {
		return s.alphabet_size <= limits<OutgoingMaskType>::digits &&
				s.state_size <= limits<StateSizeType>::max() &&
				s.transition_size <= (limits<OffsetType>::max() >> 1);
	}
private:
	static std::size_t extra_storage(const AutomatonBase& a) {
		state_type state_size = a.state_size();
		return 1 //alphabet size
				+ sizeof(StateSizeType)
				+ sizeof(OutgoingMaskType) * state_size
				+ sizeof(OffsetType) * state_size
				+ sizeof(StateSizeType) * a.transition_size();
	}
public:
	static std::size_t extra_storage(const AutomatonBase& a, PackStats s) {
		return 1 //alphabet size
				+ sizeof(StateSizeType)
				+ sizeof(OutgoingMaskType) * s.state_size
				+ sizeof(OffsetType) * s.state_size
				+ sizeof(StateSizeType) * s.transition_size;
	}

	OffsetAcceptAutomaton(const AutomatonBase& a, PackStats stats) {
		assert(can_represent(a, stats));
		ReinterpretWriter maskWriter = {storage_begin()};
		maskWriter.write<StateSizeType>(stats.state_size);
		maskWriter.write<unsigned char>(stats.alphabet_size);
		ReinterpretWriter offsetWriter = {maskWriter.p + stats.state_size * sizeof(OutgoingMaskType)};
		ReinterpretWriter destinationWriter = {offsetWriter.p + stats.state_size * sizeof(OffsetType)};

		OffsetType offset = 0;
		for (state_type s = 0; s < stats.state_size; ++s) {
			auto out = a.outgoing(s);
			out.sort();
			auto mask = set_to_mask<OutgoingMaskType>(out);
			maskWriter.write<OutgoingMaskType>(mask);
			offsetWriter.write<OffsetType>(offset | (a.accept(s) ? 1 << (limits<OffsetType>::digits - 1) : 0));
			//TODO: want an overflow-checked add here, I guess
			offset = numeric_cast<OffsetType>(offset + out.size());
			for (symbol_type c : out) {
				auto next = a.stepDeterministic(s, c);
				assert(next);
				destinationWriter.write<StateSizeType>(*next);
			}
		}
	}

	state_type state_size() const override {
		return load(reinterpret_cast<const StateSizeType*>(storage_begin()));
	}
	symbol_type alphabet_size() const override {
		return load(storage_begin() + sizeof(StateSizeType));
	}
	AutomatonBase::symbol_type active_alphabet_size() const override {
		OutgoingMaskType mask = 0;
		for (auto p = outgoing_begin(), q = outgoing_end(); p != q; ++p)
			mask = numeric_cast<OutgoingMaskType>(mask | load(p)); //in theory, we could short-circuit if all bits are set
		return __builtin_popcount(mask);
	}
	AutomatonBase::state_type accept_size() const override {
		state_type count = 0;
		for (auto p = offsets_begin(), q = offsets_end(); p != q; ++p)
			count += (load(p) >> (limits<OffsetType>::digits - 1)); //1 if the top bit is set, else 0
		return count;
	}
	std::size_t transition_size() const override {
		std::size_t count = 0;
		//TODO: make this vectorizable/unrollable, not byte-at-a-time
		for (auto p = outgoing_begin(), q = outgoing_end(); p != q; ++p)
			count += __builtin_popcount(load(p));
		return count;
	}
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
		auto suboffset = count_set_lowbits(outgoing, symbol);
		return load(destinations_begin(state) + suboffset);
	}
	AutomatonBase::SymbolSet outgoing(state_type state) const override {
		SymbolSet ret;
		auto alphabet = alphabet_size();
		OutgoingMaskType mask = outgoing_mask(state);
		for (unsigned int i = 0; i < alphabet; ++i)
			if (mask & (1 << i))
				ret.insert_absent(i);
		return ret;
	}
	AutomatonBase::StateSet destinations(state_type state) const override {
		StateSet ret;
		for (auto p = destinations_begin(state), q = destinations_end(state); p != q; ++p)
			ret.insert(load(p));
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
		return load(outgoing_begin() + state);
	}

	const OffsetType* offsets_begin() const {
		return reinterpret_cast<const OffsetType*>(outgoing_end());
	}
	const OffsetType* offsets_end() const {
		return offsets_begin() + state_size();
	}
	OffsetType offset(state_type state) const {
		return load(offsets_begin() + state);
	}

	const StateSizeType* destinations_begin(state_type state) const {
		return reinterpret_cast<const StateSizeType*>(offsets_end()) + (offset(state) & ~(1 << (limits<OffsetType>::digits - 1)));
	}
	const StateSizeType* destinations_end(state_type state) const {
		return destinations_begin(state) + __builtin_popcount(outgoing_mask(state));
	}
};

using Diminutive8OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned char, unsigned char, unsigned char>;
using Tiny8OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned char, unsigned char, unsigned short>;
using Small8OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned char, unsigned short, unsigned short>;
using Medium8OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned char, unsigned short, unsigned int>;
using Large8OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned char, unsigned int, unsigned int>;
using Diminutive16OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned short, unsigned char, unsigned char>;
using Tiny16OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned short, unsigned char, unsigned short>;
using Small16OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned short, unsigned short, unsigned short>;
using Medium16OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned short, unsigned short, unsigned int>;
using Large16OffsetPackedAutomaton = OffsetAcceptAutomaton<unsigned short, unsigned int, unsigned int>;
extern template class OffsetAcceptAutomaton<unsigned char, unsigned char, unsigned char>;
extern template class OffsetAcceptAutomaton<unsigned char, unsigned char, unsigned short>;
extern template class OffsetAcceptAutomaton<unsigned char, unsigned short, unsigned short>;
extern template class OffsetAcceptAutomaton<unsigned char, unsigned short, unsigned int>;
extern template class OffsetAcceptAutomaton<unsigned char, unsigned int, unsigned int>;
extern template class OffsetAcceptAutomaton<unsigned short, unsigned char, unsigned char>;
extern template class OffsetAcceptAutomaton<unsigned short, unsigned char, unsigned short>;
extern template class OffsetAcceptAutomaton<unsigned short, unsigned short, unsigned short>;
extern template class OffsetAcceptAutomaton<unsigned short, unsigned short, unsigned int>;
extern template class OffsetAcceptAutomaton<unsigned short, unsigned int, unsigned int>;

/**
 * Stores the accept bit in the high bit of the outgoing mask.
 */
template<typename OutgoingMaskType, typename StateSizeType, typename OffsetType>
class OutgoingAcceptAutomaton final : public PackedAutomaton {
public:
	static bool can_represent(const AutomatonBase& a, PackStats s) {
		return s.alphabet_size <= (limits<OutgoingMaskType>::digits - 1) &&
				s.state_size <= limits<StateSizeType>::max() &&
				s.transition_size <= limits<OffsetType>::max();
	}
private:
	static std::size_t extra_storage(const AutomatonBase& a) {
		state_type state_size = a.state_size();
		return 1 //alphabet size
				+ sizeof(StateSizeType)
				+ sizeof(OutgoingMaskType) * state_size
				+ sizeof(OffsetType) * state_size
				+ sizeof(StateSizeType) * a.transition_size();
	}
public:
	static std::size_t extra_storage(const AutomatonBase& a, PackStats s) {
		return 1 //alphabet size
				+ sizeof(StateSizeType)
				+ sizeof(OutgoingMaskType) * s.state_size
				+ sizeof(OffsetType) * s.state_size
				+ sizeof(StateSizeType) * s.transition_size;
	}

	OutgoingAcceptAutomaton(const AutomatonBase& a, PackStats stats) {
		assert(can_represent(a, stats));
		ReinterpretWriter maskWriter = {storage_begin()};
		maskWriter.write<StateSizeType>(stats.state_size);
		maskWriter.write<unsigned char>(stats.alphabet_size);
		ReinterpretWriter offsetWriter = {maskWriter.p + stats.state_size * sizeof(OutgoingMaskType)};
		ReinterpretWriter destinationWriter = {offsetWriter.p + stats.state_size * sizeof(OffsetType)};

		OffsetType offset = 0;
		for (state_type s = 0; s < stats.state_size; ++s) {
			auto out = a.outgoing(s);
			out.sort();
			auto mask = set_to_mask<OutgoingMaskType>(out);
			maskWriter.write<OutgoingMaskType>(mask | (a.accept(s) ? 1 << (limits<OutgoingMaskType>::digits - 1) : 0));
			offsetWriter.write<OffsetType>(offset);
			//TODO: want an overflow-checked add here, I guess
			offset = numeric_cast<OffsetType>(offset + out.size());
			for (symbol_type c : out) {
				auto next = a.stepDeterministic(s, c);
				assert(next);
				destinationWriter.write<StateSizeType>(*next);
			}
		}
	}

	state_type state_size() const override {
		return load(reinterpret_cast<const StateSizeType*>(storage_begin()));
	}
	symbol_type alphabet_size() const override {
		return load(storage_begin() + sizeof(StateSizeType));
	}
	AutomatonBase::symbol_type active_alphabet_size() const override {
		OutgoingMaskType mask = 0;
		for (auto p = outgoing_begin(), q = outgoing_end(); p != q; ++p)
			mask = numeric_cast<OutgoingMaskType>(mask | load(p)); //in theory, we could short-circuit if all bits are set
		return __builtin_popcount(mask & ~(1 << (limits<OutgoingMaskType>::digits - 1)));
	}
	AutomatonBase::state_type accept_size() const override {
		state_type count = 0;
		for (auto p = outgoing_begin(), q = outgoing_end(); p != q; ++p)
			count += (load(p) >> (limits<OutgoingMaskType>::digits - 1)); //1 if the top bit is set, else 0
		return count;
	}
	std::size_t transition_size() const override {
		std::size_t count = 0;
		for (auto p = outgoing_begin(), q = outgoing_end(); p != q; ++p)
			count += __builtin_popcount(load(p) & ~(1 << (limits<OutgoingMaskType>::digits - 1)));
		return count;
	}
	bool accept(state_type state) const override {
		return load(outgoing_begin() + state) & (1 << (limits<OutgoingMaskType>::digits - 1));
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
		auto suboffset = count_set_lowbits(outgoing, symbol);
		return load(destinations_begin(state) + suboffset);
	}
	AutomatonBase::SymbolSet outgoing(state_type state) const override {
		SymbolSet ret;
		auto alphabet = alphabet_size();
		OutgoingMaskType mask = outgoing_mask(state);
		for (unsigned int i = 0; i < alphabet; ++i)
			if (mask & (1 << i))
				ret.insert_absent(i);
		return ret;
	}
	AutomatonBase::StateSet destinations(state_type state) const override {
		StateSet ret;
		for (auto p = destinations_begin(state), q = destinations_end(state); p != q; ++p)
			ret.insert(load(p));
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
		return numeric_cast<OutgoingMaskType>(load(outgoing_begin() + state) & ~(1 << (limits<OutgoingMaskType>::digits - 1)));
	}

	const OffsetType* offsets_begin() const {
		return reinterpret_cast<const OffsetType*>(outgoing_end());
	}
	const OffsetType* offsets_end() const {
		return offsets_begin() + state_size();
	}
	OffsetType offset(state_type state) const {
		return load(offsets_begin() + state);
	}

	const StateSizeType* destinations_begin(state_type state) const {
		return reinterpret_cast<const StateSizeType*>(offsets_end()) + offset(state);
	}
	const StateSizeType* destinations_end(state_type state) const {
		return destinations_begin(state) + __builtin_popcount(outgoing_mask(state));
	}
};

using Diminutive8OutgoingPackedAutomaton = OutgoingAcceptAutomaton<unsigned char, unsigned char, unsigned char>;
using Tiny8OutgoingPackedAutomaton = OutgoingAcceptAutomaton<unsigned char, unsigned char, unsigned short>;
using Small8OutgoingPackedAutomaton = OutgoingAcceptAutomaton<unsigned char, unsigned short, unsigned short>;
using Medium8OutgoingPackedAutomaton = OutgoingAcceptAutomaton<unsigned char, unsigned short, unsigned int>;
using Large8OutgoingPackedAutomaton = OutgoingAcceptAutomaton<unsigned char, unsigned int, unsigned int>;
using Diminutive16OutgoingPackedAutomaton = OutgoingAcceptAutomaton<unsigned short, unsigned char, unsigned char>;
using Tiny16OutgoingPackedAutomaton = OutgoingAcceptAutomaton<unsigned short, unsigned char, unsigned short>;
using Small16OutgoingPackedAutomaton = OutgoingAcceptAutomaton<unsigned short, unsigned short, unsigned short>;
using Medium16OutgoingPackedAutomaton = OutgoingAcceptAutomaton<unsigned short, unsigned short, unsigned int>;
using Large16OutgoingPackedAutomaton = OutgoingAcceptAutomaton<unsigned short, unsigned int, unsigned int>;
extern template class OutgoingAcceptAutomaton<unsigned char, unsigned char, unsigned char>;
extern template class OutgoingAcceptAutomaton<unsigned char, unsigned char, unsigned short>;
extern template class OutgoingAcceptAutomaton<unsigned char, unsigned short, unsigned short>;
extern template class OutgoingAcceptAutomaton<unsigned char, unsigned short, unsigned int>;
extern template class OutgoingAcceptAutomaton<unsigned char, unsigned int, unsigned int>;
extern template class OutgoingAcceptAutomaton<unsigned short, unsigned char, unsigned char>;
extern template class OutgoingAcceptAutomaton<unsigned short, unsigned char, unsigned short>;
extern template class OutgoingAcceptAutomaton<unsigned short, unsigned short, unsigned short>;
extern template class OutgoingAcceptAutomaton<unsigned short, unsigned short, unsigned int>;
extern template class OutgoingAcceptAutomaton<unsigned short, unsigned int, unsigned int>;

/**
 * Stores the accept bit in a bitmask between the outgoing masks and the offset.
 */
template<typename OutgoingMaskType, typename StateSizeType, typename OffsetType>
class BitmaskAcceptAutomaton final : public PackedAutomaton {
public:
	static bool can_represent(const AutomatonBase& a, PackStats s) {
		return s.alphabet_size <= limits<OutgoingMaskType>::digits &&
				s.state_size <= limits<StateSizeType>::max() &&
				s.transition_size <= limits<OffsetType>::max();
	}
private:
	static std::size_t extra_storage(const AutomatonBase& a) {
		state_type state_size = a.state_size();
		return 1 //alphabet size
				+ sizeof(StateSizeType)
				+ sizeof(OutgoingMaskType) * state_size
				+ sizeof(unsigned char) * div8roundup(state_size) //accept bitmask
				+ sizeof(OffsetType) * state_size
				+ sizeof(StateSizeType) * a.transition_size();
	}
public:
	static std::size_t extra_storage(const AutomatonBase& a, PackStats s) {
		return 1 //alphabet size
				+ sizeof(StateSizeType)
				+ sizeof(OutgoingMaskType) * s.state_size
				+ sizeof(unsigned char) * div8roundup(s.state_size) //accept bitmask
				+ sizeof(OffsetType) * s.state_size
				+ sizeof(StateSizeType) * s.transition_size;
	}

	BitmaskAcceptAutomaton(const AutomatonBase& a, PackStats stats) {
		assert(can_represent(a, stats));
		ReinterpretWriter maskWriter = {storage_begin()};
		maskWriter.write<StateSizeType>(stats.state_size);
		maskWriter.write<unsigned char>(stats.alphabet_size);
		ReinterpretWriter acceptWriter = {maskWriter.p + stats.state_size * sizeof(OutgoingMaskType)};
		ReinterpretWriter offsetWriter = {acceptWriter.p + div8roundup(stats.state_size) * sizeof(unsigned char)};
		ReinterpretWriter destinationWriter = {offsetWriter.p + stats.state_size * sizeof(OffsetType)};

		OffsetType offset = 0;
		for (state_type s = 0; s < stats.state_size; ++s) {
			auto out = a.outgoing(s);
			out.sort();
			auto mask = set_to_mask<OutgoingMaskType>(out);
			maskWriter.write<OutgoingMaskType>(mask);
			offsetWriter.write<OffsetType>(offset);
			//TODO: want an overflow-checked add here, I guess
			offset = numeric_cast<OffsetType>(offset + out.size());
			for (symbol_type c : out) {
				auto next = a.stepDeterministic(s, c);
				assert(next);
				destinationWriter.write<StateSizeType>(*next);
			}
		}
		unsigned int acceptmask = 0;
		for (state_type s = 0; s < stats.state_size;) {
			for (unsigned int i = 0; i < 8 && s < stats.state_size; ++i, ++s) {
				//we can safely shift on the first iteration because we start at 0
				acceptmask |= (a.accept(s) << i);
			}
			acceptWriter.write<unsigned char>(acceptmask);
			acceptmask = 0;
		}
	}

	state_type state_size() const override {
		return load(reinterpret_cast<const StateSizeType*>(storage_begin()));
	}
	symbol_type alphabet_size() const override {
		return load(storage_begin() + sizeof(StateSizeType));
	}
	AutomatonBase::symbol_type active_alphabet_size() const override {
		OutgoingMaskType mask = 0;
		for (auto p = outgoing_begin(), q = outgoing_end(); p != q; ++p)
			mask = numeric_cast<OutgoingMaskType>(mask | load(p)); //in theory, we could short-circuit if all bits are set
		return __builtin_popcount(mask);
	}
	AutomatonBase::state_type accept_size() const override {
		state_type count = 0;
		for (auto p = accepts_begin(), q = accepts_end(); p != q; ++p)
			count += __builtin_popcount(load(p));
		return count;
	}
	std::size_t transition_size() const override {
		std::size_t count = 0;
		for (auto p = outgoing_begin(), q = outgoing_end(); p != q; ++p)
			count += __builtin_popcount(load(p));
		return count;
	}
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
		auto suboffset = count_set_lowbits(outgoing, symbol);
		return load(destinations_begin(state) + suboffset);
	}
	AutomatonBase::SymbolSet outgoing(state_type state) const override {
		SymbolSet ret;
		auto alphabet = alphabet_size();
		OutgoingMaskType mask = outgoing_mask(state);
		for (unsigned int i = 0; i < alphabet; ++i)
			if (mask & (1 << i))
				ret.insert_absent(i);
		return ret;
	}
	AutomatonBase::StateSet destinations(state_type state) const override {
		StateSet ret;
		for (auto p = destinations_begin(state), q = destinations_end(state); p != q; ++p)
			ret.insert(load(p));
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
		return load(outgoing_begin() + state);
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
		return load(offsets_begin() + state);
	}

	const StateSizeType* destinations_begin(state_type state) const {
		return reinterpret_cast<const StateSizeType*>(offsets_end()) + offset(state);
	}
	const StateSizeType* destinations_end(state_type state) const {
		return destinations_begin(state) + __builtin_popcount(outgoing_mask(state));
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
using Diminutive16BitmaskPackedAutomaton = BitmaskAcceptAutomaton<unsigned short, unsigned char, unsigned char>;
using Tiny16BitmaskPackedAutomaton = BitmaskAcceptAutomaton<unsigned short, unsigned char, unsigned short>;
using Small16BitmaskPackedAutomaton = BitmaskAcceptAutomaton<unsigned short, unsigned short, unsigned short>;
using Medium16BitmaskPackedAutomaton = BitmaskAcceptAutomaton<unsigned short, unsigned short, unsigned int>;
using Large16BitmaskPackedAutomaton = BitmaskAcceptAutomaton<unsigned short, unsigned int, unsigned int>;
extern template class BitmaskAcceptAutomaton<unsigned char, unsigned char, unsigned char>;
extern template class BitmaskAcceptAutomaton<unsigned char, unsigned char, unsigned short>;
extern template class BitmaskAcceptAutomaton<unsigned char, unsigned short, unsigned short>;
extern template class BitmaskAcceptAutomaton<unsigned char, unsigned short, unsigned int>;
extern template class BitmaskAcceptAutomaton<unsigned char, unsigned int, unsigned int>;
extern template class BitmaskAcceptAutomaton<unsigned short, unsigned char, unsigned char>;
extern template class BitmaskAcceptAutomaton<unsigned short, unsigned char, unsigned short>;
extern template class BitmaskAcceptAutomaton<unsigned short, unsigned short, unsigned short>;
extern template class BitmaskAcceptAutomaton<unsigned short, unsigned short, unsigned int>;
extern template class BitmaskAcceptAutomaton<unsigned short, unsigned int, unsigned int>;

template<class A>
std::unique_ptr<const PackedAutomaton> make_pack(const AutomatonBase& a, PackStats stats) {
	void* storage = operator new(sizeof(A) + A::extra_storage(a, stats));
	auto* p = new (storage) A(a, stats);
	//TODO: in debugging builds, allocate a few extra words, pre-fill at the end,
	//and assert that exactly the right number of bytes were modified
	return std::unique_ptr<const PackedAutomaton>(p);
}
template<class A>
std::unique_ptr<const PackedAutomaton> make_pack(const AutomatonBase& a) {
	return make_pack<A>(a, PackStats(a));
}

} //namespace detail
} //namespace automaton

#endif /* PACKEDAUTOMATON_DETAIL_HPP */

