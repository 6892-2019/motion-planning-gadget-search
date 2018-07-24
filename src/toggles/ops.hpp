#ifndef OPS_HPP
#define OPS_HPP

#include "automaton.hpp"
#include "provenance.hpp"
#include "canonicalize.hpp"
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>

bool acceptingClosure(automaton::WorkingAutomaton& connected, unsigned int locations);

template<class AutomatonType, class FinishAction>
void combine(const AutomatonType& la, uint32_t l, bool leftMirror, typename AutomatonType::state_type leftLocations,
		const AutomatonType& ra, uint32_t r, bool rightMirror, typename AutomatonType::state_type rightLocations,
		FinishAction& finish) {
	using symbol_type = typename AutomatonType::symbol_type;
	std::vector<symbol_type> slide(AutomatonType::alphabet_size_v);
	std::vector<symbol_type> sliderotate(AutomatonType::alphabet_size_v);
	std::fill(slide.begin(), slide.begin()+rightLocations, std::numeric_limits<symbol_type>::max());
	std::iota(slide.begin()+rightLocations, slide.begin()+rightLocations+leftLocations, 0);
	std::fill(slide.begin()+rightLocations+leftLocations, slide.end(), std::numeric_limits<symbol_type>::max());
	for (decltype(leftLocations) ll = 0; ll < leftLocations; ++ll) {
		std::fill(sliderotate.begin(), sliderotate.end(), std::numeric_limits<symbol_type>::max());
		std::iota(sliderotate.begin()+ll, sliderotate.begin()+ll+rightLocations, 0);
		AutomatonType lm = la;
		lm.renumberAlphabet(slide);
		for (decltype(rightLocations) rl = 0; rl < rightLocations; ++rl) {
			AutomatonType rm = ra;
			rm.renumberAlphabet(sliderotate);
			AutomatonType combined = automaton::shuffleAccept(lm, rm);
			finish(std::move(combined), Provenance(l, ll, leftMirror, r, rl, rightMirror));
			std::rotate(sliderotate.begin()+ll, sliderotate.begin()+ll+rightLocations-1, sliderotate.begin()+ll+rightLocations);
		}
		std::swap(slide[ll], slide[ll+rightLocations]);
	}
}

template<class AutomatonType, class FinishAction>
//TODO: consider not copying a if we aren't moving it in the loop below
void connect(AutomatonType a, std::uint32_t gadgetIndex, bool mirrored,
		unsigned int locations, FinishAction& finish) {
	using state_type = typename AutomatonType::state_type;
	using symbol_type = typename AutomatonType::symbol_type;

	auto setInitialStatesToAcceptingStatesInRange = [](AutomatonType& a, auto first, auto last) {
		using state_type = typename AutomatonType::state_type;
		state_type s = a.addState();
		for (state_type t : make_range_for_pair(first, last))
			if (a.accept(t))
				a.addEpsilon(s, t);
		a.swapStateNumbers(0, s);
	};

	auto enjoin = [](AutomatonType& a, symbol_type l, symbol_type m) -> bool {
		bool progress, changed = false;
		//TODO: instead of fixpoint iteration, we should put the changed state s
		//on a worklist and iterate until it's empty
		do {
			progress = false;
			for (state_type s = 0; s < a.state_size(); ++s) {
				if (a.accept(s)) continue;
				auto dests = a.step(s, l);
				for (state_type d : dests) {
					assert(a.accept(d));
					for (state_type e : a.step(d, m))
						progress |= a.addEpsilon(s, e);
				}

				dests = a.step(s, m);
				for (state_type d : dests) {
					assert(a.accept(d));
					for (state_type e : a.step(d, l))
						progress |= a.addEpsilon(s, e);
				}
			}
			changed |= progress;
		} while (progress);
		return changed;
	};

	//TODO: these alphamap manipulations could all be precomputed
	std::vector<symbol_type> alphamap(AutomatonType::alphabet_size_v);
	for (unsigned int l = 0; l < locations; ++l) {
		unsigned int m = (l+1) % locations;
		AutomatonType connected = a; //TODO: last one can move instead
		enjoin(connected, l, m);
		acceptingClosure(connected, locations);

		std::iota(alphamap.begin(), alphamap.begin() + locations, 0);
		std::fill(alphamap.begin() + locations, alphamap.end(), std::numeric_limits<symbol_type>::max());
		//remove larger first to avoid off-by-one
		alphamap.erase(alphamap.begin()+std::max(l, m));
		alphamap.erase(alphamap.begin()+std::min(l, m));
		//pad with 0
		alphamap.push_back(std::numeric_limits<symbol_type>::max());
		alphamap.push_back(std::numeric_limits<symbol_type>::max());
		connected.renumberAlphabet(0, connected.state_size(), alphamap.begin());

		//We may have disconnected the automaton (disconnecting the
		//configuration graph of the gadget it represents).
		automaton::SCCs sccs = automaton::find_components(connected);
//		for (unsigned int c = 0; c < sccs.size(); ++c) {
			//last one can move, others have to copy
//			AutomatonType op = (c == sccs.size()-1) ? std::move(connected) : connected;
//			setInitialStatesToAcceptingStatesInRange(op, sccs.begin(c), sccs.end(c));
//			op.minimize();
//			automaton::AutomatonBase::SymbolSet active = op.activeAlphabet();
//			if (active.size() <= 1) continue; //there are no interesting 1-symbol automata
//			if (active.size() != (locations - 2)) {
//				//compress the alphabet
//				active.sort();
//				std::copy(active.begin(), active.end(), alphamap.begin());
//				std::fill(alphamap.begin()+active.size(), alphamap.end(), std::numeric_limits<symbol_type>::max());
//				op.renumberAlphabet(alphamap.begin());
//				//Because we're deleting unused symbols, we don't need to
//				//minimize again; any two equivalent states would differ only in
//				//the symbols we deleted, but those symbols were inactive.
//			}
//			finish(std::move(op), Provenance(gadgetIndex, l, c, mirrored));
//		}

		auto resultvec = tbb::parallel_reduce(tbb::blocked_range<unsigned int>(0, sccs.size()),
				std::vector<std::pair<AutomatonType, Provenance>>(),
				[&](const tbb::blocked_range<unsigned int> r, const std::vector<std::pair<AutomatonType, Provenance>>& accum) {
					auto result = accum;
					for (unsigned int c = r.begin(); c != r.end(); ++c) {
						AutomatonType op = connected;
						setInitialStatesToAcceptingStatesInRange(op, sccs.begin(c), sccs.end(c));
						op.minimize();
						automaton::AutomatonBase::SymbolSet active = op.activeAlphabet();
						if (active.size() <= 1) continue; //there are no interesting 1-symbol automata
						if (active.size() != (locations - 2)) {
							//compress the alphabet
							active.sort();
							std::copy(active.begin(), active.end(), alphamap.begin());
							std::fill(alphamap.begin()+active.size(), alphamap.end(), std::numeric_limits<symbol_type>::max());
							op.renumberAlphabet(alphamap.begin());
							//Because we're deleting unused symbols, we don't need to
							//minimize again; any two equivalent states would differ only in
							//the symbols we deleted, but those symbols were inactive.
						}
						result.emplace_back(std::move(op), Provenance(gadgetIndex, l, c, mirrored));
					}
					return result;
				},
				[](const std::vector<std::pair<AutomatonType, Provenance>>& l, const std::vector<std::pair<AutomatonType, Provenance>>& r) {
					auto result = l;
					result.insert(result.end(), r.begin(), r.end()); //ugh copy
					return l;
				});
		for (auto& p : resultvec)
			finish(std::move(p.first), p.second);
	}
}

#endif /* OPS_HPP */

