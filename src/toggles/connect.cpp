#include "precompiled.hpp"
#include "ops.hpp"

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

void setInitialStates(automaton_type& a, const StateSet& initialStates) {
	using state_type = automaton_type::state_type;
	state_type s = a.addState();
	for (state_type t : initialStates)
		a.addEpsilon(s, t);
	a.swapStateNumbers(0, s);
}

namespace {
template<class Iter>
void setInitialStatesToAcceptingStatesInRange(automaton_type& a, Iter first, Iter last) {
	using state_type = automaton_type::state_type;
	state_type s = a.addState();
	for (state_type t : make_range_for_pair(first, last))
		if (a.accept(t))
			a.addEpsilon(s, t);
	a.swapStateNumbers(0, s);
}
} //anonymous namespace

void connect(Registry::index_type gadgetIndex, const automaton_type& a, const Gadget& g, const Provenance& p, Result& finishArg) {
	using state_type = automaton_type::state_type;
	using symbol_type = automaton_type::symbol_type;
	auto enjoin = [](automaton_type& a, symbol_type l, symbol_type m) -> bool {
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

	std::vector<symbol_type> alphamap(automaton_type::alphabet_size_v);
	for (unsigned int l = 0; l < g.locations_; ++l) {
		unsigned int m = (l+1) % g.locations_;
		auto connected = std::make_shared<automaton_type>(a);
		enjoin(*connected, l, m);
		acceptingClosure(*connected, g.locations_);

		std::iota(alphamap.begin(), alphamap.begin()+g.locations_, 0);
		std::fill(alphamap.begin()+g.locations_, alphamap.end(), std::numeric_limits<symbol_type>::max());
		//remove larger first to avoid off-by-one
		alphamap.erase(alphamap.begin()+std::max(l, m));
		alphamap.erase(alphamap.begin()+std::min(l, m));
		//pad with 0
		alphamap.push_back(std::numeric_limits<symbol_type>::max());
		alphamap.push_back(std::numeric_limits<symbol_type>::max());
		connected->renumberAlphabet(0, connected->state_size(), alphamap.begin());

		//We may have disconnected the automaton (disconnecting the
		//configuration graph of the gadget it represents).
		SCCs sccs = find_components(*connected);
		for (unsigned int c = 0; c < sccs.size(); ++c) {
			//last one can move, others have to copy
			auto op = (c == sccs.size()-1) ? std::move(connected) : std::make_shared<automaton_type>(*connected);
			setInitialStatesToAcceptingStatesInRange(*op, sccs.begin(c), sccs.end(c));
			op->minimize();
			SymbolSet active = op->activeAlphabet();
			if (active.size() <= 1) continue; //there are no interesting 1-symbol automata
			if (active.size() != g.locations_ - 2) {
				//compress the alphabet
				active.sort();
				std::copy(active.begin(), active.end(), alphamap.begin());
				std::fill(alphamap.begin()+active.size(), alphamap.end(), std::numeric_limits<symbol_type>::max());
				op->renumberAlphabet(alphamap.begin());
				//Because we're deleting unused symbols, we don't need to
				//minimize again; any two equivalent states would differ only in
				//the symbols we deleted, but those symbols were inactive.
			}
			canonicalize(*op, static_cast<std::uint32_t>(active.size()));
			//TODO: only do this immediately before offer (make Gadget::a_ non-const, or something)
			op->shrink_to_fit();
			finish(Gadget(std::move(op), static_cast<std::uint32_t>(active.size())),
					//TODO: reasoned choice for +1 generation
					Provenance(gadgetIndex, l, c, p.generation+1),
					finishArg);
		}
	}
}

void connect(Registry::index_type gadgetIndex, const Registry& registry, Result& finishArg) {
	const Gadget& g = registry.at(gadgetIndex);
	if (g.locations_ <= 3) {
//		std::cout << "Skipping connect due to size (" << g.locations_ <<")\n";
		return;
	}
	const Provenance& p = registry.provenance(gadgetIndex);

	connect(gadgetIndex, *g.a_, g, p, finishArg);
	if (g.mirror_)
		connect(gadgetIndex, *g.mirror_, g, p, finishArg);
}
