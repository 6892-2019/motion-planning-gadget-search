#ifndef OPS_HPP
#define OPS_HPP

#include "automaton.hpp"
#include "canonicalize.hpp"
#include "gadget-encoding.hpp"
#include "provenance.hpp"
#include "farmhash-util.hpp"
#include "tsl/ordered_set.h"

template<class Provenance>
struct Finisher {
	//I'm assuming we aren't generating so many rows as to need the PageHolder
	//machinery to reduce fragmentation.
	tsl::ordered_set<std::vector<std::byte>, farmhash_hash, std::equal_to<std::vector<std::byte>>,
			std::allocator<std::vector<std::byte>>, std::vector<std::vector<std::byte>>> rows_;
	std::vector<Provenance> prov_;
	std::size_t pruned_ = 0, skipped_ = 0;
	bool operator()(automaton::WorkingAutomaton&& a, Provenance prov) {
		//TODO: calling active_alphabet_size again here is wasteful, should pass it in instead
		prov.canonicalizePermutation = numeric_cast<std::uint8_t>(canonicalize(a, a.active_alphabet_size(), false));
		return (*this)(encoding::encode(a), prov);
	}
	bool operator()(std::vector<std::byte>&& r, Provenance prov) {
		//caller is responsible for setting canonicalizePermutation
		auto pair = rows_.insert(std::move(r));
		if (!pair.second) ++pruned_;
		//When we successfully insert, we know the index is size()-1, but vector
		//operator- is cheap enough that it's not worth branching on .second.
		prov.output1 = numeric_cast<decltype(prov.output1)>(std::distance(rows_.begin(), pair.first));
		prov_.push_back(std::move(prov));
		return pair.second;
	}
	void skip(std::size_t count = 1) {
		skipped_ += count;
	}
};

bool acceptingClosure(automaton::WorkingAutomaton& connected, unsigned int locations);
void connect(const automaton::AutomatonBase& a, std::uint64_t input1, Finisher<ConnectProvenance>& finisher);
Finisher<CombineProvenance> do_combine(std::vector<std::pair<std::uint64_t, std::vector<std::byte>>>&& left_data,
		std::vector<std::pair<std::uint64_t, std::vector<std::byte>>>&& right_data, unsigned int precision);

automaton::AutomatonBase::SymbolSet connect_deleted_symbols(const automaton::AutomatonBase& a, unsigned int connectPoint);

#endif /* OPS_HPP */

