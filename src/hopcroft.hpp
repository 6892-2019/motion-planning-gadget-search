#ifndef HOPCROFT_HPP
#define HOPCROFT_HPP

#include "dynarray.hpp"
#include "automatonbase.hpp"

namespace automaton {
namespace detail {

struct HopcroftResult {
	state_type newSize;
	dynarray<state_type> survivorsFrom, remap;
};
HopcroftResult hopcroft(WorkingAutomaton& a);
dynarray<state_type> hopcroft(ExplodedAutomaton& a);

} //namespace detail
} //namespace automaton

#endif /* HOPCROFT_HPP */

