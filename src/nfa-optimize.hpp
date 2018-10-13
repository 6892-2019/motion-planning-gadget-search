#ifndef AUTOMATON_NFA_OPTIMIZE_HPP_INCLUDED
#define AUTOMATON_NFA_OPTIMIZE_HPP_INCLUDED

#include "dynarray.hpp"
#include "automatonbase.hpp"
#include <limits>

namespace automaton {
namespace detail {

struct OptimizeResult {
	constexpr static state_type ALL = std::numeric_limits<state_type>::max();
	constexpr static state_type EMPTY = std::numeric_limits<state_type>::max()-1;
	state_type newSize;
	dynarray<state_type> survivorsFrom, remap;
};
/**
 * Returns an OptimizeResult that can be used to renumber the given automaton.
 */
OptimizeResult optimize_for_renumber(const AutomatonBase& a);
//TODO: I think we just use OptimizeResult::remap, and discovering ALL/EMPTY early is also handy.
//We can revisit if also computing survivorsFrom turns out to be a problem.
///**
// * Returns an array mapping states in the given automaton to a representative
// * state in their equivalence class.  This array can be used in determinize() to
// * reduce the size of NFA state sets.
// */
//dynarray<state_type> optimize_for_compress(const AutomatonBase& a);

} //namespace detail
} //namespace automaton

#endif /* AUTOMATON_NFA_OPTIMIZE_HPP_INCLUDED */

