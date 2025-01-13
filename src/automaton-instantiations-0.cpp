// SPDX-License-Identifier: MIT
// Copyright 2017 Massachusetts Institute of Technology
#include "precompiled.hpp"
#include "automaton.hpp"

#include "automaton.tcc"

namespace automaton {
#define AUTOMATON_EXTERN_TEMPLATE /* not extern */
#define AUTOMATON_SIZE 1
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 2
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 3
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 4
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 5
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 6
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 7
#include "automaton-instantiations.hpp"
#define AUTOMATON_SIZE 8
#include "automaton-instantiations.hpp"
#undef AUTOMATON_EXTERN_TEMPLATE
} //namespace automaton
