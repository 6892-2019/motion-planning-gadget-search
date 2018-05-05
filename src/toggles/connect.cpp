#include "precompiled.hpp"
#include "ops.hpp"
#include "registry.hpp"

using namespace automaton;

bool acceptingClosure(automaton_type& connected, unsigned int locations) {
	//Transitive closure.
	//TODO: move to Automaton? (minus only being on non-accept states)
	//If we renumbered l to m, transitive-closed, then deleted m, that would be enough (?).
	using state_type = automaton_type::state_type;
	using symbol_type = automaton_type::symbol_type;
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
						progress |= connected.addEpsilon(s, e);
				}
			}
		}
		changed |= progress;
	} while (progress);
	return changed;
}
