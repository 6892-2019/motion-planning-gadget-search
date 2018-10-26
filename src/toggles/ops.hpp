#ifndef OPS_HPP
#define OPS_HPP

#include "automaton.hpp"

template<class EmplaceRecorder>
bool acceptingClosure(automaton::WorkingAutomaton& connected, unsigned int locations, EmplaceRecorder&& emplaceRecorder) {
	//Transitive closure.
	//TODO: move to Automaton? (minus only being on non-accept states)
	//If we renumbered l to m, transitive-closed, then deleted m, that would be enough (?).
	using state_type = typename automaton::WorkingAutomaton::state_type;
	using symbol_type = typename automaton::WorkingAutomaton::symbol_type;
	bool progress, changed = false;
	do {
		//TODO: consider a worklist instead of fixpoint iteration
		progress = false;
		for (state_type s = 0; s < connected.state_size(); ++s) {
			if (connected.accept(s)) continue;
			for (symbol_type a = 0; a < locations; ++a) {
				for (state_type d : connected.step(s, a)) {
					assert(connected.accept(d));
					for (state_type e : connected.step(d, a))
						if (connected.addEpsilon(s, e)) {
							progress = true;
							emplaceRecorder(s, e);
						}
				}
			}
		}
		changed |= progress;
	} while (progress);
	return changed;
}

inline bool acceptingClosure(automaton::WorkingAutomaton& connected, unsigned int locations) {
	return acceptingClosure(connected, locations, [](auto a, auto b){});
}

#endif /* OPS_HPP */

