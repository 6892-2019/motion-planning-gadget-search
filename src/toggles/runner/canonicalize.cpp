#include "precompiled.hpp"
#include "canonicalize.hpp"

using namespace automaton;

namespace {
#include "canonicalize-perms-inl.hpp"
}

[[gnu::const]]
std::pair<const unsigned int* const*, const unsigned int* const*>
getPerms(unsigned int alphabetSize, unsigned int locations, bool normal, bool mirrored) {
	if (normal && mirrored)
		return allperms[alphabetSize][locations];
	if (normal)
		return m0perms[alphabetSize][locations];
	if (mirrored)
		return m1perms[alphabetSize][locations];
	__builtin_unreachable();
}

unsigned int canonicalize(WorkingAutomaton& a, const unsigned int locations, bool allowMirroring) {
	switch (a.alphabet_size()) {
#define GADGETDEFS_CANONICALIZE_CASE(N) case N: return canonicalize(static_cast<Automaton<N>&>(a), locations, allowMirroring); break;
		GADGETDEFS_CANONICALIZE_CASE(1)
		GADGETDEFS_CANONICALIZE_CASE(2)
		GADGETDEFS_CANONICALIZE_CASE(3)
		GADGETDEFS_CANONICALIZE_CASE(4)
		GADGETDEFS_CANONICALIZE_CASE(5)
		GADGETDEFS_CANONICALIZE_CASE(6)
		GADGETDEFS_CANONICALIZE_CASE(7)
		GADGETDEFS_CANONICALIZE_CASE(8)
		GADGETDEFS_CANONICALIZE_CASE(9)
		GADGETDEFS_CANONICALIZE_CASE(10)
		GADGETDEFS_CANONICALIZE_CASE(11)
		GADGETDEFS_CANONICALIZE_CASE(12)
		GADGETDEFS_CANONICALIZE_CASE(13)
		GADGETDEFS_CANONICALIZE_CASE(14)
		GADGETDEFS_CANONICALIZE_CASE(15)
		GADGETDEFS_CANONICALIZE_CASE(16)
#undef GADGETDEFS_CANONICALIZE_CASE
		default:
			fmt::print(stderr, "unhandled canonicalize for alphabet size {}, typeid {}\n",
					a.alphabet_size(), typeid(a).name());
			std::terminate();
	}
}

template<unsigned int N>
std::pair<std::unique_ptr<WorkingAutomaton>, unsigned int> gadgetdefs_mirror_case(const WorkingAutomaton& a) {
	auto&& b = mirror(static_cast<const Automaton<N>&>(a));
	return {std::make_unique<Automaton<N>>(std::move(b.first)), b.second};
}

std::pair<std::unique_ptr<WorkingAutomaton>, unsigned int> mirror(const WorkingAutomaton& a) {
	switch (a.alphabet_size()) {
#define GADGETDEFS_MIRROR_CASE(N) case N: return gadgetdefs_mirror_case<N>(a);
		GADGETDEFS_MIRROR_CASE(1)
		GADGETDEFS_MIRROR_CASE(2)
		GADGETDEFS_MIRROR_CASE(3)
		GADGETDEFS_MIRROR_CASE(4)
		GADGETDEFS_MIRROR_CASE(5)
		GADGETDEFS_MIRROR_CASE(6)
		GADGETDEFS_MIRROR_CASE(7)
		GADGETDEFS_MIRROR_CASE(8)
		GADGETDEFS_MIRROR_CASE(9)
		GADGETDEFS_MIRROR_CASE(10)
		GADGETDEFS_MIRROR_CASE(11)
		GADGETDEFS_MIRROR_CASE(12)
		GADGETDEFS_MIRROR_CASE(13)
		GADGETDEFS_MIRROR_CASE(14)
		GADGETDEFS_MIRROR_CASE(15)
		GADGETDEFS_MIRROR_CASE(16)
#undef GADGETDEFS_MIRROR_CASE
		default:
			fmt::print(stderr, "unhandled mirror for alphabet size {}, typeid {}\n",
					a.alphabet_size(), typeid(a).name());
			std::terminate();
	}
}

namespace {
template<unsigned int N>
bool has_nop_edge(Automaton<N>& a, WorkingAutomaton::state_type state, WorkingAutomaton::symbol_type symbol) {
	for (WorkingAutomaton::state_type p : a.step(state, symbol))
		for (WorkingAutomaton::state_type q : a.step(p, symbol))
			if (q == state)
				return true;
	return false;
}

template<unsigned int N>
bool addMaximalNops0(Automaton<N>& a, unsigned int locations) {
	//For each accept state and symbol, check if there is already a loop back to
	//that state; otherwise, add one.
	bool changed = false;
	for (WorkingAutomaton::state_type s = 0, send = a.state_size(); s < send; ++s) {
		if (!a.accept(s)) continue; //or for_each_accept?
		for (WorkingAutomaton::symbol_type p = 0; p < locations; ++p)
			if (!has_nop_edge(a, s, p)) {
				WorkingAutomaton::state_type bounce = a.addState();
				a.addTrans(s, p, bounce);
				a.addTrans(bounce, p, s);
				changed = true;
			}
	}
	return changed;
}
} //anonymous namespace

namespace automaton {
namespace detail {
bool addMaximalNops(automaton::WorkingAutomaton& a, unsigned int locations) {
	switch (a.alphabet_size()) {
#define ADDMAXIMALNOPS_CASE(N) case N: return addMaximalNops0(static_cast<Automaton<N>&>(a), locations);
		ADDMAXIMALNOPS_CASE(2)
		ADDMAXIMALNOPS_CASE(3)
		ADDMAXIMALNOPS_CASE(4)
		ADDMAXIMALNOPS_CASE(5)
		ADDMAXIMALNOPS_CASE(6)
		ADDMAXIMALNOPS_CASE(7)
		ADDMAXIMALNOPS_CASE(8)
		ADDMAXIMALNOPS_CASE(9)
		ADDMAXIMALNOPS_CASE(10)
		ADDMAXIMALNOPS_CASE(11)
		ADDMAXIMALNOPS_CASE(12)
		ADDMAXIMALNOPS_CASE(13)
		ADDMAXIMALNOPS_CASE(14)
		ADDMAXIMALNOPS_CASE(15)
		ADDMAXIMALNOPS_CASE(16)
#undef ADDMAXIMALNOPS_CASE
		default:
			fmt::print(stderr, "unhandled addMaximalNops for alphabet size {}, typeid {}, locations {}\n",
					a.alphabet_size(), typeid(a).name(), locations);
			std::terminate();
	}
}
} //namespace detail
} //namespace automaton