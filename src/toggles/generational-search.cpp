#include "precompiled.hpp"
#include "automaton.hpp"
#include "provenance.hpp"
#include "ops.hpp"
#include "gadgetdefs.hpp"
#include "packedautomaton.hpp"
#include "hopscotch/hopscotch_set.h"

using namespace automaton;
using std::vector;
using std::pair;
using std::string;
using std::unique_ptr;

typedef Automaton<8u> automaton_type;
typedef pair<automaton_type, Provenance> AutoProv;
typedef pair<unique_ptr<const PackedAutomaton>, Provenance> PackProv;
typedef unsigned int index_type;

struct Input {
	index_type index;
	automaton_type normal, mirror; //mirror is empty if the input is not chiral
	AutomatonBase::symbol_type active_alphabet_size;
};

struct Target {
	std::size_t packed_hash, mirror_packed_hash;
	std::unique_ptr<const PackedAutomaton> normal, mirror;
};

class GenerationalSearch {
public:
	GenerationalSearch(vector<automaton_type>& inputs, vector<automaton_type>& targets) {
		inputs_.reserve(inputs.size());
		for (auto i : xrange(inputs.size())) {
			automaton_type m = mirror(inputs[i]);
			if (m == inputs[i])
				m.clear();
			auto asz = inputs[i].active_alphabet_size();
			inputs_.push_back({numeric_cast<index_type>(i), std::move(inputs[i]), std::move(m), asz});
		}
		targets_.reserve(targets.size());
		for (auto& t : targets) {
			auto p = pack(t);
			auto m = pack(mirror(t));
			auto ph = p->packed_hash(), mh = m->packed_hash();
			targets_.push_back({ph, mh, std::move(p), std::move(m)});
		}
	}
	void advance() {
		vector<PackProv> nextgen;
		auto finishAction = [&](automaton_type&& a, Provenance p) {
			a.minimize();
			canonicalize(a, a.active_alphabet_size());
			auto packed = pack(a);
			auto hash = packed->packed_hash();
			//Check the closed set to deduplicate early.
			if (closed_.find(packed, hash) != closed_.end()) return;
			//We could check targets here, but we can't easily report a finding
			//and, once we go parallel, we want to ensure we get a deterministic
			//finding, so we'd have to check that an earlier thread hadn't yet.
			//TODO: local deduplication in our Expansion struct
			nextgen.emplace_back(std::move(packed), p);
		};
		if (curgen_.empty()) {
			assert(closed_.empty());
			//"combine against nothing" to get started
			for (auto& i : inputs_)
				finishAction(automaton_type{i.normal}, Provenance(i.index));
		} else {
			for (index_type i = 0, sourceIndex = numeric_cast<index_type>(provenance_.size()-curgen_.size());
					i < curgen_.size();
					i++, sourceIndex++) {
				//from toggles.cpp's Combine::operator()
				const PackedAutomaton* source = curgen_[i];
				automaton_type unpacked(*source);
				automaton_type::symbol_type leftLocations = unpacked.active_alphabet_size();
				automaton_type mirrored = mirror(unpacked);
				bool shouldmirror = unpacked == mirrored;
				for (const Input& i : inputs_) {
					if (leftLocations + i.active_alphabet_size > automaton_type::alphabet_size_v) continue;
					combine(unpacked, sourceIndex, false, leftLocations, i.normal, i.index, false, i.active_alphabet_size, finishAction);
					if (i.mirror.state_size())
						combine(unpacked, sourceIndex, false, leftLocations, i.mirror, i.index, true, i.active_alphabet_size, finishAction);
					if (shouldmirror) {
						combine(mirrored, sourceIndex, true, leftLocations, i.normal, i.index, false, i.active_alphabet_size, finishAction);
						if (i.mirror.state_size()) //TODO: the both-mirrored combine may be redundant
							combine(mirrored, sourceIndex, true, leftLocations, i.mirror, i.index, true, i.active_alphabet_size, finishAction);
					}
				}
			}
		}
		curgen_.clear();
		auto newStart = append(nextgen);
		while (curgen_.size() != newStart) {
			nextgen.clear();
			for (index_type i = numeric_cast<index_type>(newStart), sourceIndex = numeric_cast<index_type>(provenance_.size()-(curgen_.size()-newStart));
					i < curgen_.size();
					i++, sourceIndex++) {
				automaton_type inflated(*curgen_[i]);
				automaton_type mirrored = mirror(inflated);
				automaton_type::symbol_type locations = inflated.active_alphabet_size();
				connect(inflated, sourceIndex, false, locations, finishAction);
				if (inflated != mirrored)
					connect(mirrored, sourceIndex, true, locations, finishAction);
			}
			newStart = append(nextgen);
			std::cout << newStart << " " << curgen_.size() << std::endl;
		}
	}
private:
	vector<const PackedAutomaton*> curgen_; //non-owning, owned by closed_'s elements
	vector<Provenance> provenance_;
	tsl::hopscotch_set<std::unique_ptr<const PackedAutomaton>,
			indirect_hash, indirect_equal, std::allocator<std::unique_ptr<const PackedAutomaton>>,
			30, true /* store the hash */> closed_;
	vector<Input> inputs_;
	vector<Target> targets_;

	std::size_t append(std::vector<PackProv>& next) {
		auto newStart = curgen_.size();
		for (PackProv& p : next) {
			const PackedAutomaton* observer = p.first.get();
			if (closed_.insert(std::move(p.first)).second) {
				auto hash = observer->packed_hash();
				for (const Target& t : targets_)
					if (t.packed_hash == hash || t.mirror_packed_hash == hash) {
						print_provenance_backtrace(p.second);
						//TODO: maybe put it in some member variable to be checked when convenient?
					}
				//TODO: add insert overload taking the hash so we only compute it once
				curgen_.push_back(observer);
				provenance_.push_back(p.second);
			}
			//otherwise unique_ptr cleans it up somewhere, possibly in the guts
			//of closed_.insert.  TODO: we might prefer to release memory in a
			//large batch at the end of the loop rather than during each
			//iteration, for better locality (both data and code).
		}
		return newStart;
	}

	[[gnu::cold]]
	void print_provenance_backtrace(Provenance& provenance) {
		circular_deque<std::uint32_t, 32> queue;
		linear_set<std::uint32_t> printed;

		std::cout << "<found> = " << provenance << '\n';
		for (auto p : provenance.parents())
			if (printed.insert(p).second)
				queue.push_back(p);

		while (!queue.empty()) {
			auto idx = queue.pop_front();
			auto prov = provenance_.at(idx);
			std::cout << idx << " = " << prov << '\n';
			//Ideally we'd print the automaton here, but we're no longer
			//maintaining an id->automaton map.  We'll have to replay the
			//log this code is printing out.
			for (auto p : prov.parents())
				if (printed.insert(p).second)
					queue.push_back(p);
		}

		std::cout << std::flush;
	}
};

//struct Expansion;
//
//Expansion map(const automaton_type& a, index_type index, const vector<AutoProv>& combinables /* inputs + more? */) {
//	throw std::logic_error("");
//}
//
//Expansion map(const PackedAutomaton& a, index_type index, const vector<AutoProv>& combinables) {
//
//}
//
//Expansion reduce(const Expansion& left, const Expansion& right) {
//
//}

int main(int argc, char* argv[]) { //genbuild entrypoint
	vector<automaton_type> inputs, outputs;

	std::vector<std::string> tokens;
	boost::algorithm::split(tokens, argv[1], boost::algorithm::is_any_of(","));
	for (unsigned int i = 0; i < tokens.size(); ++i) {
		std::cout << "input " << i << ": " << tokens[i] << "\n";
		inputs.push_back(*known_gadget(tokens[i], automaton_type::alphabet_size_v));
	}

	tokens.clear();
	boost::algorithm::split(tokens, argv[2], boost::algorithm::is_any_of(","));
	for (unsigned int i = 0; i < tokens.size(); ++i) {
		std::cout << "output " << i << ": " << tokens[i] << "\n";
		outputs.push_back(*known_gadget(tokens[i], automaton_type::alphabet_size_v));
	}

	GenerationalSearch gs(inputs, outputs);
	gs.advance();
	std::cout << 1 << std::endl;
	gs.advance();
	std::cout << 2 << std::endl;
	gs.advance();
	std::cout << 3 << std::endl;

	return 0;
}