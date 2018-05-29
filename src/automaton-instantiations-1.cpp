#include "precompiled.hpp"
#include "automaton.hpp"

#include "automaton.tcc"

namespace automaton {
#define AUTOMATON_EXTERN_TEMPLATE /* not extern */
#define AUTOMATON_SIZE 9
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 10
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 11
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 12
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 13
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 14
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 15
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 16
#include "automaton-instantiations.hpp"
#undef AUTOMATON_EXTERN_TEMPLATE
} //namespace automaton
