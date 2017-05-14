#ifndef OPS_HPP
#define OPS_HPP

#include "registry.hpp"

//methods used by ops, defined elsewhere
void finish(Gadget&& gadget, Provenance provenance, Result& finishArg);

//the ops themselves
void combine(Registry::index_type l, Registry::index_type r, const Registry& registry, Result& finishArg);
void connect(Registry::index_type gadgetIndex, const Registry& registry, Result& finishArg);

//ops used during problem setup, not otherwise used outside of the ops themselves
bool acceptingClosure(automaton_type& connected, unsigned int locations);
void setInitialStates(automaton_type& a, const StateSet& initialStates);

#endif /* OPS_HPP */

