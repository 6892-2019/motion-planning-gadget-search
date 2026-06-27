#!/usr/bin/env python3
"""
Equivalence and simulation reasoning for motion planning gadgets.

Two distinct relations live here; keeping them distinct is the whole point.

1.  EQUIVALENCE (decidable, exact).  Two gadgets are *behaviorally
    equivalent* if they admit exactly the same traversal sequences — i.e.,
    viewed as automata (states, alphabet = (entry, exit) traversals, every
    state accepting because the agent may stop at any time) they accept the
    same language.  This is computed by `behaviorally_equivalent`, which
    minimizes both gadgets (merging indistinguishable states) and then tests
    for isomorphism.  This mirrors the C++ pipeline `minimize()` then
    `canonicalize()` (see automaton.hpp), which the project treats as the
    authoritative notion of gadget identity.

2.  SIMULATION (semidecidable in general).  Gadget G' *simulates* G if some
    *system* of G' gadgets reproduces G's behavior (Ani–Demaine–Hendrickson–
    Lynch, "Trains, Games, and Complexity", arXiv:2005.03192).  This is much
    stronger than "G' has all of G's transitions" — extra reachable behavior
    *breaks* a simulation rather than helping it, because a one-player
    simulation must match G's traversal sequences with an *if and only if*.
    We do NOT try to decide simulation positively here (that needs an explicit
    construction).  What we provide is `refute_simulation`, which applies
    *invariants preserved under system composition* to soundly rule it out.
    The canonical example: a toggle (reversible) cannot simulate a dicrumbler
    (irreversible), because any system of reversible gadgets is itself
    reversible.  See the project note `reversibility-and-simulation`.
"""

from typing import Dict, List, Optional, Set, Tuple
from gadget import Gadget


def print_traversal_table(
    gadget: Gadget,
    take_closure: bool = False,
    state_labels: Optional[Dict[int, str]] = None,
    loc_labels: Optional[Dict[int, str]] = None,
    title: str = '',
) -> None:
    """
    Print all traversal modes as a plain-text table for hand-checking.

    Each row is one mode: (from_state, entry_loc) → (to_state, exit_loc).
    Rows are sorted by (from_state, entry_loc, to_state, exit_loc) so the
    table is stable across runs and easy to scan.

    Args:
        gadget: The gadget to inspect.
        take_closure: If True, include shortcut modes added by the closure.
        state_labels: Display names for states, e.g. {0: 'open', 1: 'closed'}.
        loc_labels: Display names for locations, e.g. {0: 'in', 1: 'out'}.
        title: Optional heading printed above the table.
    """
    def fs(s: int) -> str:
        return state_labels[s] if state_labels and s in state_labels else str(s)

    def fl(l: int) -> str:
        return loc_labels[l] if loc_labels and l in loc_labels else str(l)

    modes = sorted(get_traversal_modes(gadget, take_closure=take_closure))
    suffix = ' (with closure)' if take_closure else ''
    heading = title or f'Traversal modes{suffix} — {gadget.num_states} state(s), {gadget.num_locations} location(s)'
    print(heading)

    if not modes:
        print('  (no modes)')
        return

    # Column widths
    from_s_w = max(len(fs(m[0])) for m in modes)
    entry_w  = max(len(fl(m[1])) for m in modes)
    to_s_w   = max(len(fs(m[2])) for m in modes)
    exit_w   = max(len(fl(m[3])) for m in modes)

    hdr = (f"  {'from_state':<{from_s_w}}  {'entry_loc':<{entry_w}}"
           f"  {'to_state':<{to_s_w}}  {'exit_loc':<{exit_w}}")
    print(hdr)
    print('  ' + '-' * (len(hdr) - 2))

    prev_from = None
    for fs_, el, ts_, xl in modes:
        if prev_from is not None and (fs_, el) != prev_from:
            print()  # blank line between entry-groups
        print(f"  {fs(fs_):<{from_s_w}}  {fl(el):<{entry_w}}"
              f"  {fs(ts_):<{to_s_w}}  {fl(xl):<{exit_w}}")
        prev_from = (fs_, el)
    print()


def get_traversal_modes(gadget: Gadget,
                        take_closure: bool = True) -> Set[Tuple[int, int, int, int]]:
    """
    Return all traversal modes of a gadget.

    A traversal mode is a (from_state, entry_loc, to_state, exit_loc) tuple
    representing: robot enters at entry_loc when gadget is in from_state, and
    exits at exit_loc leaving the gadget in to_state.

    With take_closure=True (default), shortcut transitions via intermediate
    locations are included.
    """
    g = gadget.closure() if take_closure else gadget
    return set(g.transitions)


# ── Structural invariants (properties of the raw transition relation) ────────

def is_reversible(gadget: Gadget) -> bool:
    """
    True if the gadget is reversible: every traversal can be immediately undone.

    Formally (Demaine–Hendrickson–Lynch, ITCS 2020), the transition relation
    contains the reverse of every edge.  The reverse of the traversal
    (s, a, s', b) — "in state s, enter a, exit b, end in state s'" — is
    (s', b, s, a).  We test the *raw* transitions (not the closure), since
    single-step symmetry already implies every multi-step traversal can be
    undone step by step.

    This inspects the actual transitions, NOT the `reversible` construction
    flag: a gadget built from directed edges can still happen to be reversible,
    and vice versa.
    """
    T = set(gadget.transitions)
    return all((ts, tl, fs, fl) in T for (fs, fl, ts, tl) in T)


def is_dag(gadget: Gadget) -> bool:
    """
    True if the gadget's *state-transition* graph is acyclic.

    Such a gadget can be traversed only a bounded number of times (no sequence
    of traversals repeats a state), so it is "bounded".  A self-loop
    (a transition with from_state == to_state) counts as a cycle.  Reversible
    gadgets with any edge are never DAGs.
    """
    # Build the state graph: an edge s -> s' for each transition.
    succ: Dict[int, Set[int]] = {s: set() for s in range(gadget.num_states)}
    for fs, _fl, ts, _tl in gadget.transitions:
        succ[fs].add(ts)

    WHITE, GRAY, BLACK = 0, 1, 2
    color = {s: WHITE for s in range(gadget.num_states)}

    def has_cycle(u: int) -> bool:
        color[u] = GRAY
        for v in succ[u]:
            if color[v] == GRAY:          # back edge (incl. self-loop) → cycle
                return True
            if color[v] == WHITE and has_cycle(v):
                return True
        color[u] = BLACK
        return False

    return not any(color[s] == WHITE and has_cycle(s)
                   for s in range(gadget.num_states))


# ── Behavioral equivalence (the authoritative notion of gadget identity) ─────

def minimize(gadget: Gadget) -> Gadget:
    """
    Return a behaviorally-equivalent gadget with indistinguishable states
    merged, via DFA partition refinement.

    The gadget is viewed as a deterministic automaton: the symbol of a
    traversal is its (entry_loc, exit_loc) pair, and *every* state is
    accepting (the agent may stop at any time).  Two states are merged iff,
    for every entry location, they either both have no traversal or both have
    a traversal with the same exit location leading to (recursively) merged
    states.  All states are retained as legitimate starting configurations;
    only indistinguishable ones are coalesced.

    Raises ValueError if the gadget is nondeterministic (some (state, entry)
    has two distinct (exit, to_state) outcomes), since language minimization of
    nondeterministic gadgets is not handled here.
    """
    # Deterministic transition function: (state, entry) -> (exit, to_state).
    delta: Dict[Tuple[int, int], Tuple[int, int]] = {}
    for fs, fl, ts, tl in gadget.transitions:
        key = (fs, fl)
        outcome = (tl, ts)
        if key in delta and delta[key] != outcome:
            raise ValueError(
                "minimize() requires a deterministic gadget; "
                f"state {fs} entry {fl} has multiple outcomes "
                f"{delta[key]} and {outcome}")
        delta[key] = outcome

    n = gadget.num_states
    locs = range(gadget.num_locations)

    # Partition refinement.  All states start in one block (all accepting);
    # split until stable.
    block = {s: 0 for s in range(n)}
    while True:
        signatures = {}
        for s in range(n):
            sig = []
            for entry in locs:
                key = (s, entry)
                if key in delta:
                    exit_loc, to_state = delta[key]
                    sig.append((entry, exit_loc, block[to_state]))
                # An undefined (state, entry) contributes nothing, which is
                # distinct from any defined entry.
            signatures[s] = (block[s], tuple(sig))

        relabel: Dict[Tuple, int] = {}
        new_block = {}
        for s in range(n):
            relabel.setdefault(signatures[s], len(relabel))
            new_block[s] = relabel[signatures[s]]

        if new_block == block:
            break
        block = new_block

    # Renumber blocks by first appearance so the result is deterministic.
    order: List[int] = []
    seen: Set[int] = set()
    for s in range(n):
        if block[s] not in seen:
            seen.add(block[s])
            order.append(block[s])
    block_id = {b: i for i, b in enumerate(order)}

    result = Gadget(len(order), gadget.num_locations)
    added: Set[Tuple[int, int, int, int]] = set()
    for fs, fl, ts, tl in gadget.transitions:
        t = (block_id[block[fs]], fl, block_id[block[ts]], tl)
        if t not in added:
            added.add(t)
            result.transitions.append(t)
    return result


def _alphabet(num_locations: int) -> List[Tuple[int, int]]:
    """All possible traversal symbols (entry, exit) with entry != exit."""
    return [(a, b) for a in range(num_locations)
            for b in range(num_locations) if a != b]


def _canonical_language(gadget: Gadget, start_state: int) -> str:
    """
    Canonical string for the language of traversal sequences executable from
    `start_state` — i.e., the behavior of the gadget when started in that
    state, with the internal state hidden and every state "accepting" (the
    agent may stop at any time).

    Handles nondeterministic gadgets by subset construction (determinization),
    then minimizes and canonically renumbers the resulting DFA.  Two states
    have equal behavior iff this string matches, so it is a faithful invariant
    of the per-state language.  This mirrors the C++ minimize()+canonicalize()
    pipeline (automaton.hpp), specialized to one start state.
    """
    symbols = _alphabet(gadget.num_locations)

    # NFA transition relation: nfa[state][(entry, exit)] = set of to_states.
    nfa: Dict[int, Dict[Tuple[int, int], Set[int]]] = {}
    for fs, fl, ts, tl in gadget.transitions:
        nfa.setdefault(fs, {}).setdefault((fl, tl), set()).add(ts)

    # Subset construction.  A subset is reachable only if nonempty; the empty
    # subset is the (omitted) dead sink, so every DFA state is accepting and
    # the language is exactly the set of sequences that avoid the sink.
    start = frozenset([start_state])
    ids: Dict[frozenset, int] = {start: 0}
    dfa: Dict[Tuple[int, Tuple[int, int]], int] = {}
    queue = [start]
    while queue:
        S = queue.pop()
        sid = ids[S]
        for sym in symbols:
            T = frozenset(t for s in S for t in nfa.get(s, {}).get(sym, ()))
            if not T:
                continue
            if T not in ids:
                ids[T] = len(ids)
                queue.append(T)
            dfa[(sid, sym)] = ids[T]

    states = list(range(len(ids)))

    # Minimize: partition refinement, all states accepting, missing edge = dead.
    block = {s: 0 for s in states}
    while True:
        sig = {}
        for s in states:
            row = tuple((sym, block[dfa[(s, sym)]])
                        for sym in symbols if (s, sym) in dfa)
            sig[s] = (block[s], row)
        relabel: Dict[Tuple, int] = {}
        new_block = {}
        for s in states:
            relabel.setdefault(sig[s], len(relabel))
            new_block[s] = relabel[sig[s]]
        if new_block == block:
            break
        block = new_block

    # Block-level transitions, then canonically renumber by BFS from the start
    # block over the fixed symbol order.
    btrans: Dict[Tuple[int, Tuple[int, int]], int] = {}
    for (s, sym), t in dfa.items():
        btrans[(block[s], sym)] = block[t]

    canon = {block[0]: 0}
    order = [block[0]]
    i = 0
    while i < len(order):
        b = order[i]
        i += 1
        for sym in symbols:
            if (b, sym) in btrans:
                nb = btrans[(b, sym)]
                if nb not in canon:
                    canon[nb] = len(canon)
                    order.append(nb)

    edges = sorted((canon[b], sym, canon[btrans[(b, sym)]])
                   for (b, sym) in btrans if b in canon)
    return f"{len(canon)}|{edges}"


def behaviorally_equivalent(
    A: Gadget,
    B: Gadget,
    fixed_locations: bool = False,
    verbose: bool = False,
) -> bool:
    """
    True if A and B are the same gadget: started in corresponding states they
    admit exactly the same traversal sequences (same automaton language).

    This is the authoritative equality test.  For each gadget, the language
    from every state is reduced to a canonical form (`_canonical_language`,
    which determinizes, minimizes, and canonically renumbers); two gadgets are
    equivalent iff the *set* of per-state canonical languages matches under
    some relabeling.  States with identical behavior collapse automatically, so
    this is robust to redundant states, nondeterminism, and state relabeling.

    With fixed_locations=False (default) all location relabelings are tried
    (locations are abstract ports); with fixed_locations=True the location
    indices are held fixed (use this when ports have a fixed physical identity,
    e.g. an ordering along a corridor).

    Note: this is *equivalence*, not *simulation*.  A reversible gadget is
    never equivalent to an irreversible one, and a gadget with extra reachable
    behavior is not equivalent to one without it.
    """
    from itertools import permutations

    if A.num_locations != B.num_locations:
        if verbose:
            print("Not equivalent: different number of locations.")
        return False

    sig_A = frozenset(_canonical_language(A, q) for q in range(A.num_states))

    n = A.num_locations
    loc_perms = ([tuple(range(n))] if fixed_locations
                 else list(permutations(range(n))))
    for perm in loc_perms:
        loc_map = {orig: perm[orig] for orig in range(n)}
        B_relabeled = Gadget(B.num_states, B.num_locations)
        B_relabeled.transitions = [
            (fs, loc_map[fl], ts, loc_map[tl])
            for fs, fl, ts, tl in B.transitions
        ]
        sig_B = frozenset(_canonical_language(B_relabeled, q)
                          for q in range(B.num_states))
        if sig_A == sig_B:
            if verbose:
                kind = ("with fixed locations" if fixed_locations
                        else "up to relabeling")
                print(f"Behaviorally equivalent ({kind}).")
            return True

    if verbose:
        print("Not behaviorally equivalent.")
    return False


# ── Simulation: sound refutation via composition-invariant properties ────────

def refute_simulation(
    target: Gadget,
    block: Gadget,
    verbose: bool = True,
) -> List[str]:
    """
    Try to *soundly* prove that no system of `block` gadgets can simulate
    `target`, using invariants preserved under system composition.

    Returns a list of reasons.  A non-empty list is a proof that `block`
    cannot simulate `target` (not even with an arbitrary system of copies).
    An empty list means *no obstruction was found* — it is NOT a proof that a
    simulation exists; confirming simulation requires exhibiting an explicit
    construction.

    Invariants checked (each is preserved when gadgets are wired into a
    system, so the target must share it with the building block):
      * Reversibility.  A system of reversible gadgets is reversible (every
        traversal can be undone), so a reversible block cannot simulate an
        irreversible target.
      * DAG / boundedness.  A system of DAG gadgets can be traversed only a
        bounded number of times, so a DAG block cannot simulate a non-DAG
        (cyclic) target such as a toggle.
    """
    reasons: List[str] = []

    if not is_reversible(target) and is_reversible(block):
        reasons.append(
            "reversibility: the block is reversible but the target is not; "
            "any system of reversible gadgets is itself reversible, so it can "
            "never simulate an irreversible gadget")

    if not is_dag(target) and is_dag(block):
        reasons.append(
            "DAG/boundedness: the block has an acyclic state graph (bounded "
            "number of traversals) but the target's state graph has a cycle; "
            "a system of bounded gadgets is bounded and cannot simulate an "
            "unbounded target")

    if verbose:
        if reasons:
            print("Simulation is IMPOSSIBLE. Sound obstruction(s):")
            for r in reasons:
                print(f"  - {r}")
        else:
            print("No composition-invariant obstruction found "
                  "(this does NOT prove a simulation exists).")

    return reasons
