#include "precompiled.hpp"
#include "packedautomaton.hpp"

using namespace automaton;
using state_type = AutomatonBase::state_type;
using symbol_type = AutomatonBase::symbol_type;
using StateSet = AutomatonBase::StateSet;
using SymbolSet = AutomatonBase::SymbolSet;

namespace {

class DimunitivePackedAutomaton : public PackedAutomaton {
public:
	static bool can_represent(const AutomatonBase& a) {
		return a.alphabet_size() <= CHAR_BIT &&
				a.state_size() <= std::numeric_limits<unsigned char>::max() &&
				a.transition_size() <= (std::numeric_limits<unsigned char>::max() >> 1);
	}
	static std::size_t extra_storage(const AutomatonBase& a) {
		return 2 //state and alphabet size
				+ 1 * a.state_size() //bitmasks
				+ 1 * a.state_size() //offsets
				+ 1 * a.transition_size() //transitions
				;
	}

	DimunitivePackedAutomaton(const AutomatonBase& a) {
		assert(can_represent(a));
		unsigned char* p = storage_begin();
		*p++ = numeric_cast<unsigned char>(a.state_size());
		*p++ = numeric_cast<unsigned char>(a.alphabet_size());

		for (state_type s = 0; s < a.state_size(); ++s)
			*p++ = set_to_mask(a.outgoing(s));
		unsigned char offset = 0;
		for (state_type s = 0; s < a.state_size(); ++s) {
			*p++ = a.accept(s) ? (offset | 1 << 7) : offset;
			offset = numeric_cast<unsigned char>(offset + __builtin_popcount(outgoing_mask(s)));
		}
		for (state_type s = 0; s < a.state_size(); ++s) {
			SymbolSet syms = a.outgoing(s);
			syms.sort();
			for (symbol_type c : syms) {
				auto next = a.stepDeterministic(s, c);
				assert(next);
				*p++ = numeric_cast<unsigned char>(*next);
			}
		}
		assert(p == storage_end());
	}
	state_type state_size() const override {
		return *storage_begin();
	}
	symbol_type alphabet_size() const override {
		return *(storage_begin() + 1);
	}
	bool deterministic() const override {return true;}
	bool minimal() const override {return true;}
	bool canonical() const override {return true;}
	bool accept(state_type state) const override {
		return offset(state) & (1 << 7);
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
		unsigned char outgoing = outgoing_mask(state);
		if (!(outgoing & (1 << symbol))) return std::nullopt;
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
	const unsigned char* outgoing_begin() const {
		return storage_begin() + 2;
	}
	const unsigned char* outgoing_end() const {
		return outgoing_begin() + state_size();
	}
	unsigned char outgoing_mask(state_type state) const {
		return outgoing_begin()[state];
	}

	const unsigned char* offsets_begin() const {
		return outgoing_end();
	}
	const unsigned char* offsets_end() const {
		return offsets_begin() + state_size();
	}
	unsigned char offset(state_type state) const {
		return offsets_begin()[state];
	}

	const unsigned char* destinations_begin(state_type state) const {
		return offsets_end() + (offset(state) & ~(1 << 7));
	}
	const unsigned char* destinations_end(state_type state) const {
		return destinations_begin(state) + __builtin_popcount(outgoing_mask(state));
	}

	static unsigned char set_to_mask(SymbolSet set) {
		unsigned int mask = 0;
		for (symbol_type s : set)
			mask |= 1u << s;
		return numeric_cast<unsigned char>(mask);
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

template<class A>
std::unique_ptr<const PackedAutomaton> make_pack(const AutomatonBase& a) {
	void* storage = operator new(sizeof(A) + A::extra_storage(a));
	auto* p = new (storage) A(a);
	return std::unique_ptr<const PackedAutomaton>(p);
}

} //anonymous namespace

namespace automaton {

bool operator==(const PackedAutomaton& left, const PackedAutomaton& right) {
	auto lb = left.storage_begin(), le = left.storage_end(), rb = right.storage_begin(), re = right.storage_end();
	return (le - lb) == (re - rb) && std::memcmp(lb, rb, le - lb) == 0;
}

std::unique_ptr<const PackedAutomaton> pack(const AutomatonBase& a) {
	assert(a.canonical());

	//Ideally we'd walk through all of them and pick the smallest valid one, but
	//for now we'll assume they're ordered by size.
	if (DimunitivePackedAutomaton::can_represent(a))
		return make_pack<DimunitivePackedAutomaton>(a);
	std::cout << "couldn't pack" << std::endl;
	std::terminate();
}
} //namespace automaton