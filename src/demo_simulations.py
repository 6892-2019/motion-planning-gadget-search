#!/usr/bin/env python3
"""
demo_simulations.py — Concrete simulation and equivalence examples.

Run from the repo root:
    python3 src/demo_simulations.py

All text output is written to  demo_output/<name>.txt
All diagrams are written to    demo_output/<name>.png

The gadgets used are taken directly from the YAML definitions in
src/toggles/gadgets.yaml and src/toggles/tunnels.yaml; only those
whose transitions have distinct entry and exit locations are usable
with the Python model (same-location transitions are not supported).

Gadgets demonstrated
--------------------
toggle          — basic 2-state, 2-loc symmetric (undirected) toggle
dicrumbler      — directed half-toggle: only one traversal direction
seven           — toggle that "opens up" completely once in the open state
one_way_crumbler— one-way: can traverse in reverse while open, but
                  permanently closes after a forward traversal
a2t             — alternating-toggle-toggle-antiparallel (4s 4l), directed

Equivalence and simulation demonstrated
---------------------------------------
Two relations, kept carefully distinct (see gadget_simulation.py):
  * EQUIVALENCE — do two gadgets admit exactly the same traversals? (decidable)
  * SIMULATION  — can a *system* of one gadget reproduce another? (we only
                  soundly *refute* it here, via composition invariants)

1. toggle does NOT simulate dicrumbler   (reversibility invariant)
2. dicrumbler does NOT simulate toggle   (DAG / boundedness invariant)
3. seven ≠ toggle, and no invariant rules out simulation either way
4. toggle is self-equivalent             (sanity check)
5. closure of a 3-state directed chain   (closure adds shortcut)
6. a2t  traversal modes + diagram
"""

import os, sys, io
sys.path.insert(0, os.path.dirname(__file__))

from gadget import Gadget
from gadget_simulation import (
    get_traversal_modes,
    is_reversible,
    is_dag,
    behaviorally_equivalent,
    refute_simulation,
    print_traversal_table,
)
from gadget_viz import visualize_gadget_automaton

OUT = os.path.join(os.path.dirname(__file__), '..', 'demo_output')
os.makedirs(OUT, exist_ok=True)


# ── helpers ────────────────────────────────────────────────────────────────

def capture(fn):
    """Run fn(), capturing stdout to a string."""
    buf = io.StringIO()
    old = sys.stdout
    sys.stdout = buf
    fn()
    sys.stdout = old
    return buf.getvalue()

def write(filename, text):
    path = os.path.join(OUT, filename)
    with open(path, 'w') as f:
        f.write(text)
    print(f'  wrote {os.path.relpath(path)}')

def diagram(gadget, filename, **kw):
    path = os.path.join(OUT, filename.removesuffix('.png'))
    visualize_gadget_automaton(gadget, path, format='png', **kw)
    print(f'  wrote {os.path.relpath(path)}.png')


# ── gadget definitions (match YAML exactly) ────────────────────────────────
#
# Format: uedges add BOTH directions; dedges add only the listed direction.
# All entries here have from_loc ≠ to_loc (Python model requirement).

# toggle: uedges [[0,0,1,1]]
toggle = Gadget(2, 2)
toggle.add_transition(0, 0, 1, 1)  # uedge → also add reverse
toggle.add_transition(1, 1, 0, 0)
TOGGLE_S = {0: 'closed', 1: 'open'}
TOGGLE_L = {0: 'L',      1: 'R'}

# dicrumbler: dedges [[0,0,1,1]]
dicrumbler = Gadget(2, 2)
dicrumbler.add_transition(0, 0, 1, 1)   # directed only
DICRUM_S = {0: 'intact', 1: 'crumbled'}
DICRUM_L = {0: 'L', 1: 'R'}

# seven: uedges [[0,0,1,1],[1,0,1,1]]   state-names {0:'closed',1:'open'}
# In state 1 (open) the robot may traverse freely in both directions.
seven = Gadget(2, 2)
seven.add_transition(0, 0, 1, 1)  # closed → open
seven.add_transition(1, 1, 0, 0)  # open   → closed (reverse of above)
seven.add_transition(1, 0, 1, 1)  # open   → open,  L→R  (free traverse)
seven.add_transition(1, 1, 1, 0)  # open   → open,  R→L  (free traverse, reverse)
SEVEN_S = {0: 'closed', 1: 'open'}
SEVEN_L = {0: 'L', 1: 'R'}

# one_way_crumbler: dedges [[0,0,1,1],[0,1,0,0]]
# Permanently closes after a forward traversal; reverse is free while open.
one_way_crumbler = Gadget(2, 2)
one_way_crumbler.add_transition(0, 0, 1, 1)  # L→R: closes permanently
one_way_crumbler.add_transition(0, 1, 0, 0)  # R→L: stays open (directed)
OWC_S = {0: 'open', 1: 'closed'}
OWC_L  = {0: 'L',   1: 'R'}

# alternating-toggle-toggle-antiparallel (A2T):
# dedges [[0,0,1,1],[1,1,0,2],[2,2,3,3],[3,3,2,0]]
# Four states: states 0+1 form one toggle cycle, states 2+3 form another;
# they share locations in a chain.
a2t = Gadget(4, 4)
a2t.add_transition(0, 0, 1, 1)
a2t.add_transition(1, 1, 0, 2)
a2t.add_transition(2, 2, 3, 3)
a2t.add_transition(3, 3, 2, 0)
A2T_S = {0: 'A0', 1: 'A1', 2: 'B0', 3: 'B1'}
A2T_L = {0: 'p0', 1: 'p1', 2: 'p2', 3: 'p3'}


# ── Example 1: Traversal mode tables ───────────────────────────────────────
print('\n=== Traversal Mode Tables ===')

def ex1():
    print_traversal_table(toggle,          state_labels=TOGGLE_S, loc_labels=TOGGLE_L,
                          title='toggle  (tunnels.yaml)')
    print_traversal_table(dicrumbler,      state_labels=DICRUM_S, loc_labels=DICRUM_L,
                          title='dicrumbler  (tunnels.yaml)')
    print_traversal_table(seven,           state_labels=SEVEN_S,  loc_labels=SEVEN_L,
                          title='seven  (tunnels.yaml)')
    print_traversal_table(one_way_crumbler, state_labels=OWC_S,   loc_labels=OWC_L,
                          title='one_way_crumbler  (tunnels.yaml)')
    print_traversal_table(a2t,             state_labels=A2T_S,    loc_labels=A2T_L,
                          title='alternating-toggle-toggle-antiparallel  (gadgets.yaml)')

write('1_traversal_tables.txt', capture(ex1))


# ── Example 2: Automaton diagrams ──────────────────────────────────────────
print('\n=== Automaton Diagrams ===')
diagram(toggle,          '2a_toggle.png',          state_labels=TOGGLE_S, loc_labels=TOGGLE_L, title='toggle')
diagram(dicrumbler,      '2b_dicrumbler.png',      state_labels=DICRUM_S, loc_labels=DICRUM_L, title='dicrumbler')
diagram(seven,           '2c_seven.png',           state_labels=SEVEN_S,  loc_labels=SEVEN_L,  title='seven')
diagram(one_way_crumbler,'2d_one_way_crumbler.png',state_labels=OWC_S,    loc_labels=OWC_L,    title='one_way_crumbler')
diagram(a2t,             '2e_a2t.png',             state_labels=A2T_S,    loc_labels=A2T_L,    title='A2T')


# ── Example 3: toggle does NOT simulate dicrumbler ─────────────────────────
# A toggle is reversible; a dicrumbler is not.  It is tempting to argue "the
# toggle has every transition the dicrumbler has, so it simulates it" — but
# that is exactly the trap.  A simulation must reproduce the target's behavior
# with an *if and only if*: it may not add reachable behavior the target
# forbids.  The toggle's reverse traversal lets the agent undo the crumble,
# which the dicrumbler forbids.  And because reversibility is preserved under
# system composition, this holds even for an arbitrary *system* of toggles.
print('\n=== Example 3: toggle does NOT simulate dicrumbler ===')

def ex3():
    print('─' * 60)
    print('toggle      reversible? ', is_reversible(toggle),
          '   DAG (bounded)? ', is_dag(toggle))
    print('dicrumbler  reversible? ', is_reversible(dicrumbler),
          '   DAG (bounded)? ', is_dag(dicrumbler))
    print()
    print('Are they the same gadget?')
    print('  behaviorally_equivalent(toggle, dicrumbler) =',
          behaviorally_equivalent(toggle, dicrumbler))
    print('  → No: the toggle has the reverse traversal the dicrumbler lacks.')
    print()
    print('Can a system of toggles simulate a dicrumbler?')
    refute_simulation(dicrumbler, toggle)
    print()
    print('Conclusion: NO.  A system of reversible gadgets is reversible, so it')
    print('cannot simulate the irreversible dicrumbler — no construction exists.')

write('3_toggle_cannot_simulate_dicrumbler.txt', capture(ex3))


# ── Example 4: dicrumbler does NOT simulate toggle ─────────────────────────
# The other direction fails for a different reason: the dicrumbler is a DAG
# (it crumbles once and is then dead), and a system of DAG gadgets can only be
# traversed a bounded number of times, whereas a toggle cycles forever.
print('\n=== Example 4: dicrumbler does NOT simulate toggle ===')

def ex4():
    print('─' * 60)
    print('Are they the same gadget?')
    print('  behaviorally_equivalent(dicrumbler, toggle) =',
          behaviorally_equivalent(dicrumbler, toggle))
    print()
    print('Can a system of dicrumblers simulate a toggle?')
    refute_simulation(toggle, dicrumbler)
    print()
    print('Conclusion: NO.  A system of bounded (DAG) gadgets is bounded and')
    print('cannot reproduce the toggle\'s unbounded back-and-forth cycling.')

write('4_dicrumbler_cannot_simulate_toggle.txt', capture(ex4))


# ── Example 5: seven vs toggle ─────────────────────────────────────────────
# seven and toggle are both reversible and both cyclic (non-DAG), so neither
# composition invariant rules out a simulation in either direction.  They are
# nonetheless not the same gadget: seven has extra free-traverse modes.  This
# is the honest situation — the invariants are silent, so deciding simulation
# would require exhibiting (or ruling out) an explicit construction.
print('\n=== Example 5: seven vs toggle ===')

def ex5():
    print('─' * 60)
    print('seven   reversible? ', is_reversible(seven),
          '   DAG (bounded)? ', is_dag(seven))
    print('toggle  reversible? ', is_reversible(toggle),
          '   DAG (bounded)? ', is_dag(toggle))
    print()
    print('Same gadget?  behaviorally_equivalent(seven, toggle) =',
          behaviorally_equivalent(seven, toggle))
    print('  → No: seven adds two free-traverse modes in the open state.')
    print()
    print('Does any invariant refute "seven simulates toggle"?')
    refute_simulation(toggle, seven)
    print('Does any invariant refute "toggle simulates seven"?')
    refute_simulation(seven, toggle)
    print()
    print('Conclusion: the necessary conditions are silent here.  Whether')
    print('either simulates the other is not settled by these invariants and')
    print('would require an explicit construction (out of scope for this demo).')

write('5_seven_vs_toggle.txt', capture(ex5))


# ── Example 6: Closure on a 3-state directed chain ─────────────────────────
# This illustrates the closure operation: a chain A→B→C through loc 0 gains
# a shortcut A→C.  No such shortcut exists in the original gadget.
print('\n=== Example 6: Closure — 3-state directed chain ===')

chain = Gadget(3, 2)
chain.add_transition(0, 0, 1, 1)   # state A → B via loc 0
chain.add_transition(1, 0, 2, 1)   # state B → C via loc 0

def ex6():
    print('─' * 60)
    print('Original chain (before closure):')
    print_traversal_table(chain,
                          state_labels={0:'A', 1:'B', 2:'C'},
                          loc_labels={0:'entry', 1:'exit'})

    closed = chain.closure()
    print('After closure (shortcut A→C added):')
    print_traversal_table(closed,
                          state_labels={0:'A', 1:'B', 2:'C'},
                          loc_labels={0:'entry', 1:'exit'})

    print('Idempotency check (closure of closure = closure):',
          set(closed.closure().transitions) == set(closed.transitions))

    print()
    print('The shortcut the closure adds:')
    raw_chain  = get_traversal_modes(chain,  take_closure=False)
    raw_closed = get_traversal_modes(closed, take_closure=False)
    shortcut = raw_closed - raw_chain
    print(f'  Modes in closure but not in raw chain: {shortcut}')
    print(f'  i.e. the shortcut (A→entry→C→exit) exists only after closure.')
    print()
    print('NOTE: the closure records reachability through repeated re-entry at')
    print('the SAME location; it is a derived view, not a new gadget.  Adding')
    print('the shortcut as a raw transition makes the gadget nondeterministic')
    print('from (A, entry), which is why equivalence/minimization operate on')
    print('the raw single-step transitions, not the closure.')

write('6_closure_chain.txt', capture(ex6))

# Diagram both
diagram(chain,         '6a_chain_before_closure.png',
        state_labels={0:'A',1:'B',2:'C'}, loc_labels={0:'entry',1:'exit'},
        title='Chain (before closure)')
diagram(chain.closure(),'6b_chain_after_closure.png',
        state_labels={0:'A',1:'B',2:'C'}, loc_labels={0:'entry',1:'exit'},
        title='Chain (after closure)')
print('  wrote demo_output/6a_chain_before_closure.png')
print('  wrote demo_output/6b_chain_after_closure.png')


# ── Example 7: A2T — trace + traversal modes ───────────────────────────────
print('\n=== Example 7: A2T traversal modes + trace ===')

def ex7():
    print('─' * 60)
    print_traversal_table(a2t, state_labels=A2T_S, loc_labels=A2T_L,
                          title='A2T — alternating-toggle-toggle-antiparallel (gadgets.yaml)')
    print()
    print('Closure of A2T (expect: no new modes — all entry locations appear once):')
    a2t_closed = a2t.closure()
    print_traversal_table(a2t_closed, state_labels=A2T_S, loc_labels=A2T_L,
                          title='A2T closure')
    print()

    print('Tracing a full cycle  A0 → A1 → A0  (using first two transitions):')
    a2t.trace(0, [(0, 1), (1, 2)], state_labels=A2T_S, loc_labels=A2T_L)
    print()
    print('Tracing a full cycle  B0 → B1 → B0  (using last two transitions):')
    a2t.trace(2, [(2, 3), (3, 0)], state_labels=A2T_S, loc_labels=A2T_L)

write('7_a2t_modes_trace.txt', capture(ex7))


# ── Example 8: isomorphism vs equivalence ──────────────────────────────────
print('\n=== Example 8: is_equivalent_to vs is_isomorphic_to ===')

def ex8():
    print('─' * 60)
    print('Two gadgets with the same structure but swapped location labels:')
    print()
    g1 = Gadget(2, 2)
    g1.add_transition(0, 0, 1, 1)   # enter loc 0 → exit loc 1

    g2 = Gadget(2, 2)
    g2.add_transition(0, 1, 1, 0)   # enter loc 1 → exit loc 0 (locs swapped)

    print_traversal_table(g1, title='g1')
    print_traversal_table(g2, title='g2 (same structure, locs 0↔1 swapped)')

    print(f'is_equivalent_to  (holds locs fixed): {g1.is_equivalent_to(g2)}')
    print('  → False: g1 enters at loc 0; g2 enters at loc 1.')
    print()
    print(f'is_isomorphic_to  (tries all loc permutations): {g1.is_isomorphic_to(g2)}')
    print('  → True: the abstract traversal-mode structure is the same.')

write('8_isomorphism.txt', capture(ex8))


# ── Example 9: clean per-gadget state & state-location diagrams ─────────────
print('\n=== Example 9: clean state / state-location diagrams ===')
from gadget_viz import (visualize_state_diagram,
                        visualize_state_location_diagram,
                        visualize_gadget_box,
                        visualize_system)

def col_positions(system):
    """Stack a system's instances in a single column (good for parallel bundles)."""
    return {n: (0, i) for i, n in enumerate(system.instance_names)}

def sl_diagrams(g, stem, S, L, title):
    visualize_gadget_box(g, os.path.join(OUT, stem + '_box'),
                         format='png', state_labels=S, loc_labels=L, title=title)
    visualize_state_diagram(g, os.path.join(OUT, stem + '_state'),
                            format='png', state_labels=S, loc_labels=L,
                            title=title + ' — state diagram')
    visualize_state_location_diagram(g, os.path.join(OUT, stem + '_stateloc'),
                                     format='png', state_labels=S, loc_labels=L,
                                     title=title + ' — state-location diagram')
    print(f'  wrote demo_output/{stem}_box.png, {stem}_state.png, {stem}_stateloc.png')

sl_diagrams(toggle,     '9a_toggle',     TOGGLE_S, TOGGLE_L, 'toggle')
sl_diagrams(dicrumbler, '9b_dicrumbler', DICRUM_S, DICRUM_L, 'dicrumbler')
sl_diagrams(seven,      '9c_seven',      SEVEN_S,  SEVEN_L,  'seven')


# ── Example 10: a VERIFIED construction — two toggles in series ⇒ toggle ────
# This is a real simulation construction.  We wire two toggles in series, ask
# the system what gadget it induces, verify it against a toggle, draw the
# wiring (with external ports labelled), and trace every allowed traversal —
# both as text and as a highlighted diagram.
print('\n=== Example 10: verified construction (two toggles in series) ===')
from gadget_system import GadgetSystem

series = GadgetSystem()
series.add_instance('T1', toggle, 0).add_instance('T2', toggle, 0)
series.connect(('T1', 1), ('T2', 0))            # T1.R — T2.L (internal wire)
series.expose('L', ('T1', 0)).expose('R', ('T2', 1))   # external interface

def ex10():
    print('─' * 60)
    print('Claim: two toggles wired in series simulate a single toggle.')
    print('  T1.R is wired to T2.L; external ports L = T1.L, R = T2.R.')
    print()
    series.verify_simulates(toggle, label_to_target_loc={'L': 0, 'R': 1})
    print()
    print('Traces of every allowed traversal through the network:')
    print()
    series.print_all_traces()

write('10_series_toggle_construction.txt', capture(ex10))

# Wiring diagram + one highlighted trace diagram per allowed traversal.
visualize_system(series, os.path.join(OUT, '10a_series_system'),
                 format='png', title='two toggles in series')
print('  wrote demo_output/10a_series_system.png')

_ind, _cfgs, _labels, _ = series.induced_gadget()
for n, (cfg, entry, exit_) in enumerate([((0, 0), 'L', 'R'),
                                         ((1, 1), 'R', 'L')], start=1):
    mv = series.trace(cfg, entry, exit_)
    visualize_system(series, os.path.join(OUT, f'10b_series_trace{n}_{entry}{exit_}'),
                     format='png', highlight_entry=entry, highlight_moves=mv,
                     title=f'trace {entry}→{exit_} from config {series.fmt_config(cfg)}')
    print(f'  wrote demo_output/10b_series_trace{n}_{entry}{exit_}.png')


# ── Example 11: a construction that FAILS — toggles cannot make a dicrumbler ─
# The same series-toggle network cannot simulate a dicrumbler: the verifier
# reports the induced gadget is reversible while the dicrumbler is not.
print('\n=== Example 11: failed construction (toggles ⇏ dicrumbler) ===')

def ex11():
    print('─' * 60)
    print('Claim (FALSE): the series-toggle network simulates a dicrumbler.')
    print()
    ok = series.verify_simulates(dicrumbler, label_to_target_loc={'L': 0, 'R': 1})
    print()
    print(f'Verified: {ok}.  The induced gadget is a toggle (reversible); the')
    print('dicrumbler is irreversible, so no wiring of toggles can produce it.')
    print('This agrees with refute_simulation in Examples 3–4.')

write('11_failed_dicrumbler_construction.txt', capture(ex11))


# ── Targets for the non-trivial constructions ──────────────────────────────
# "k-use" gadget: a one-way path that may be traversed L→R exactly k times,
# advancing through k+1 states, then is permanently dead.
def k_use_gadget(k: int) -> Gadget:
    g = Gadget(k + 1, 2)
    for s in range(k):
        g.add_transition(s, 0, s + 1, 1)
    return g

double_use = k_use_gadget(2)
triple_use = k_use_gadget(3)
KUSE_L = {0: 'L', 1: 'R'}


def induced_state_labels(system) -> dict:
    """Label induced-gadget states by the configuration they represent."""
    _, configs, _, _ = system.induced_gadget()
    return {i: system.fmt_config(c) for i, c in enumerate(configs)}


def parallel_system(name_prefix, block, count, init=0):
    """`count` copies of `block`, all sharing both endpoints (a parallel bundle).
    External L is at the first copy's loc 0, R at its loc 1."""
    s = GadgetSystem()
    names = [f'{name_prefix}{i+1}' for i in range(count)]
    for n in names:
        s.add_instance(n, block, init)
    for n in names[1:]:                       # wire all loc-0 together, all loc-1 together
        s.connect((names[0], 0), (n, 0))
        s.connect((names[0], 1), (n, 1))
    s.expose('L', (names[0], 0)).expose('R', (names[0], 1))
    return s


# ── Example 12: two dicrumblers in parallel ⇒ a two-use gadget ─────────────
# A genuinely non-trivial simulation: the agent CHOOSES which dicrumbler to
# crumble, so a single L→R traversal is nondeterministic; the two "one crumbled"
# configurations behave identically and are merged by the equivalence check.
print('\n=== Example 12: two dicrumblers in parallel ⇒ two-use gadget ===')

par2 = parallel_system('D', dicrumbler, 2)

def ex12():
    print('─' * 60)
    print('Claim: two dicrumblers sharing both endpoints simulate a gadget that')
    print('crumbles after exactly TWO L→R traversals (a "two-use" gadget).')
    print('  D1.L,D2.L are joined as external L; D1.R,D2.R as external R.')
    print()
    par2.verify_simulates(double_use, label_to_target_loc={'L': 0, 'R': 1})
    print()
    print('The induced gadget has FOUR configurations, but (D1=1,D2=0) and')
    print('(D1=0,D2=1) are behaviorally identical ("one use left"), so the')
    print('equivalence check merges them — the simulated gadget has 3 states.')
    print()
    print('Traces of every allowed traversal (note the agent\'s choice from the')
    print('fresh configuration):')
    print()
    par2.print_all_traces()

write('12_two_dicrumblers_parallel.txt', capture(ex12))

visualize_state_diagram(double_use, os.path.join(OUT, '12a_two_use_target_state'),
                        format='png', loc_labels=KUSE_L,
                        title='two-use gadget (target) — state diagram')
visualize_system(par2, os.path.join(OUT, '12b_two_dicrumblers_system'),
                 format='png', positions=col_positions(par2),
                 title='two dicrumblers in parallel')
visualize_state_diagram(par2.induced_gadget()[0], os.path.join(OUT, '12c_two_use_induced_state'),
                        format='png', state_labels=induced_state_labels(par2), loc_labels=KUSE_L,
                        title='induced gadget (states = configurations)')
for n, (cfg, end) in enumerate([((0, 0), (1, 0)), ((0, 0), (0, 1))], start=1):
    mv = par2.trace(cfg, 'L', 'R', end_config=end)
    visualize_system(par2, os.path.join(OUT, f'12d_two_dicrumblers_trace{n}'),
                     format='png', positions=col_positions(par2),
                     highlight_entry='L', highlight_moves=mv,
                     title=f'trace L→R from fresh: crumble {"D1" if n==1 else "D2"}')
print('  wrote demo_output/12a..12d two-dicrumbler diagrams')


# ── Example 13: three dicrumblers in parallel ⇒ a three-use gadget ─────────
# The construction generalizes: k parallel dicrumblers give a k-use gadget.
print('\n=== Example 13: three dicrumblers in parallel ⇒ three-use gadget ===')

par3 = parallel_system('D', dicrumbler, 3)

def ex13():
    print('─' * 60)
    print('Claim: three dicrumblers in parallel simulate a THREE-use gadget.')
    print('  The pattern generalizes: k parallel dicrumblers ⇒ a k-use gadget.')
    print()
    par3.verify_simulates(triple_use, label_to_target_loc={'L': 0, 'R': 1})
    print()
    print('Here the induced gadget has 2^3 = 8 configurations, which collapse to')
    print('the 4 states of the three-use gadget (grouped by how many dicrumblers')
    print('have crumbled).')

write('13_three_dicrumblers_parallel.txt', capture(ex13))
visualize_state_diagram(triple_use, os.path.join(OUT, '13a_three_use_target_state'),
                        format='png', loc_labels=KUSE_L,
                        title='three-use gadget (target) — state diagram')
visualize_system(par3, os.path.join(OUT, '13b_three_dicrumblers_system'),
                 format='png', positions=col_positions(par3),
                 title='three dicrumblers in parallel')
print('  wrote demo_output/13a, 13b three-dicrumbler diagrams')


# ── Example 14: three toggles in series ⇒ toggle (longer traces) ────────────
print('\n=== Example 14: three toggles in series ⇒ toggle ===')

series3 = GadgetSystem()
for _n in ['T1', 'T2', 'T3']:
    series3.add_instance(_n, toggle, 0)
series3.connect(('T1', 1), ('T2', 0)).connect(('T2', 1), ('T3', 0))
series3.expose('L', ('T1', 0)).expose('R', ('T3', 1))

def ex14():
    print('─' * 60)
    print('Claim: three toggles in series simulate a single toggle.')
    print('  Each traversal now threads through all three toggles (5 steps).')
    print()
    series3.verify_simulates(toggle, label_to_target_loc={'L': 0, 'R': 1})
    print()
    series3.print_all_traces()

write('14_three_toggles_series.txt', capture(ex14))
visualize_system(series3, os.path.join(OUT, '14a_three_toggles_system'),
                 format='png', title='three toggles in series')
_mv = series3.trace((0, 0, 0), 'L', 'R')
visualize_system(series3, os.path.join(OUT, '14b_three_toggles_traceLR'),
                 format='png', highlight_entry='L', highlight_moves=_mv,
                 title='trace L→R through three toggles')
print('  wrote demo_output/14a, 14b three-toggle diagrams')


# ── Example 15: a REJECTED construction — two toggles in parallel ───────────
# Sharing both endpoints of two toggles looks like it should give a toggle, but
# the agent can enter L, flip one toggle, and walk back out L with the state
# changed — a same-port traversal with state change that no gadget can express.
# The verifier detects this defect and rejects the construction.
print('\n=== Example 15: rejected construction (two toggles in parallel) ===')

badpar = parallel_system('T', toggle, 2)

def ex15():
    print('─' * 60)
    print('Claim (PLAUSIBLE BUT FALSE): two toggles sharing both endpoints')
    print('simulate a single toggle.')
    print()
    badpar.verify_simulates(toggle, label_to_target_loc={'L': 0, 'R': 1})
    print()
    print('The construction is unfaithful: at the shared L junction the agent can')
    print('enter L, traverse one toggle to R, walk along the shared R junction back')
    print('into the OTHER toggle, and return to L — exiting where it entered with')
    print('the configuration changed.  No gadget has an "enter L, exit L, change')
    print('state" traversal, so the verifier reports these as defects.')

write('15_two_toggles_parallel_rejected.txt', capture(ex15))
visualize_system(badpar, os.path.join(OUT, '15a_two_toggles_parallel_system'),
                 format='png', positions=col_positions(badpar),
                 title='two toggles in parallel (rejected)')
print('  wrote demo_output/15a two-toggles-parallel diagram')


# ── Multi-port building blocks and targets ─────────────────────────────────
# The 2-toggle: two tunnels (a,b) and (c,d) that SHARE one state; traversing
# either tunnel flips the shared state (so both tunnels reverse direction).
two_toggle = Gadget(2, 4)
for _t in [(0, 0, 1, 1), (0, 2, 1, 3), (1, 1, 0, 0), (1, 3, 0, 2)]:
    two_toggle.add_transition(*_t)
TWOT_S = {0: 's0', 1: 's1'}
TWOT_L = {0: 'a', 1: 'b', 2: 'c', 3: 'd'}
TWOT_SIDES = {0: 'L', 1: 'R', 2: 'L', 3: 'R'}     # tunnels laid out horizontally

# The distributor: enter A; the first traversal exits B or C (agent's choice),
# the second exits whichever remains, then it is dead.  3 ports, irreversible.
distributor = Gadget(4, 3)
for _t in [(0, 0, 1, 1), (0, 0, 2, 2), (1, 0, 3, 2), (2, 0, 3, 1)]:
    distributor.add_transition(*_t)
DIST_L = {0: 'A', 1: 'B', 2: 'C'}
DIST_SIDES = {0: 'L', 1: 'R', 2: 'R'}


# ── Example 16: a 2-toggle (4 ports) simulates a (1-)toggle ────────────────
# A multi-port gadget standing in for a simpler one: expose only one of the
# 2-toggle's two tunnels; the other is never reached, so what is left behaves
# exactly like a single toggle.
print('\n=== Example 16: a 2-toggle simulates a toggle ===')

sim16 = GadgetSystem()
sim16.add_instance('X', two_toggle, 0)
sim16.expose('L', ('X', 0)).expose('R', ('X', 1))   # tunnel (a,b); (c,d) unused

def ex16():
    print('─' * 60)
    print('Claim: a 2-toggle simulates a toggle, using only its (a,b) tunnel.')
    print('  The (c,d) tunnel is left unconnected, so the agent never reaches it.')
    print()
    sim16.verify_simulates(toggle, label_to_target_loc={'L': 0, 'R': 1})
    print()
    sim16.print_all_traces()

write('16_2toggle_simulates_toggle.txt', capture(ex16))
visualize_gadget_box(two_toggle, os.path.join(OUT, '16a_2toggle_box'),
                     format='png', state_labels=TWOT_S, loc_labels=TWOT_L,
                     title='2-toggle', port_sides=TWOT_SIDES)
visualize_system(sim16, os.path.join(OUT, '16b_2toggle_as_toggle_system'),
                 format='png', port_sides={'X': TWOT_SIDES},
                 title='2-toggle with one tunnel exposed')
print('  wrote demo_output/16a, 16b two-toggle-as-toggle diagrams')


# ── Example 17: two 2-toggles in series ⇒ a 2-toggle (4-port network) ───────
# A more complex network: two 4-port gadgets joined by TWO internal wires (one
# per tunnel).  Both 2-toggles stay synchronized, so the pair behaves as one
# 2-toggle.
print('\n=== Example 17: two 2-toggles in series ⇒ a 2-toggle ===')

sim17 = GadgetSystem()
sim17.add_instance('A', two_toggle, 0).add_instance('B', two_toggle, 0)
sim17.connect(('A', 1), ('B', 0))      # tunnel-1 of A → tunnel-1 of B
sim17.connect(('A', 3), ('B', 2))      # tunnel-2 of A → tunnel-2 of B
for _l, _p in [('a', ('A', 0)), ('c', ('A', 2)),
               ('b', ('B', 1)), ('d', ('B', 3))]:
    sim17.expose(_l, _p)

def ex17():
    print('─' * 60)
    print('Claim: two 2-toggles wired tunnel-to-tunnel simulate one 2-toggle.')
    print('  Internal wires A.b–B.a and A.d–B.c; external ports a,c (left) and')
    print('  b,d (right).  Every traversal threads both gadgets, keeping them')
    print('  synchronized, so only two joint configurations are reachable.')
    print()
    sim17.verify_simulates(two_toggle,
                           label_to_target_loc={'a': 0, 'b': 1, 'c': 2, 'd': 3})
    print()
    sim17.print_all_traces()

write('17_two_2toggles_series.txt', capture(ex17))
visualize_system(sim17, os.path.join(OUT, '17a_two_2toggles_system'),
                 format='png', port_sides={'A': TWOT_SIDES, 'B': TWOT_SIDES},
                 title='two 2-toggles in series')
_mv17 = sim17.trace((0, 0), 'a', 'b')
visualize_system(sim17, os.path.join(OUT, '17b_two_2toggles_trace_ab'),
                 format='png', port_sides={'A': TWOT_SIDES, 'B': TWOT_SIDES},
                 highlight_entry='a', highlight_moves=_mv17,
                 title='trace a→b (threads both gadgets, tunnel 1)')
print('  wrote demo_output/17a, 17b two-2toggle diagrams')


# ── Example 18: a branching network ⇒ a 3-port distributor ─────────────────
# Two dicrumblers share their entry at a junction A; their exits are B and C.
# Entering A, the agent chooses which dicrumbler to crumble (so it exits B or
# C); the second visit must use the other; a third is impossible.
print('\n=== Example 18: shared-entry dicrumblers ⇒ a 3-port distributor ===')

sim18 = GadgetSystem()
sim18.add_instance('D1', dicrumbler, 0).add_instance('D2', dicrumbler, 0)
sim18.connect(('D1', 0), ('D2', 0))                 # shared entry junction = A
sim18.expose('A', ('D1', 0)).expose('B', ('D1', 1)).expose('C', ('D2', 1))

def ex18():
    print('─' * 60)
    print('Claim: two dicrumblers sharing an entry junction simulate a 3-port')
    print('"distributor": enter A, exit B or C (agent\'s choice); each exit can')
    print('be produced once, then the gadget is dead.')
    print()
    sim18.verify_simulates(distributor,
                           label_to_target_loc={'A': 0, 'B': 1, 'C': 2})
    print()
    sim18.print_all_traces()

write('18_distributor.txt', capture(ex18))
visualize_gadget_box(distributor, os.path.join(OUT, '18a_distributor_box'),
                     format='png', loc_labels=DIST_L, port_sides=DIST_SIDES,
                     title='distributor (target)')
_dist_sides = {'D1': {0: 'L', 1: 'R'}, 'D2': {0: 'L', 1: 'R'}}
visualize_system(sim18, os.path.join(OUT, '18b_distributor_system'),
                 format='png', positions=col_positions(sim18),
                 port_sides=_dist_sides, title='shared-entry dicrumblers')
for _lbl in ('B', 'C'):
    _mv = sim18.trace((0, 0), 'A', _lbl)
    visualize_system(sim18, os.path.join(OUT, f'18c_distributor_trace_A{_lbl}'),
                     format='png', positions=col_positions(sim18),
                     port_sides=_dist_sides, highlight_entry='A',
                     highlight_moves=_mv, title=f'trace A→{_lbl} from fresh')
print('  wrote demo_output/18a, 18b, 18c distributor diagrams')


# ── Report ──────────────────────────────────────────────────────────────────

def build_report():
    """Stitch the examples into a single Markdown report in demo_output/."""
    def inc(name):
        path = os.path.join(OUT, name)
        if not os.path.exists(path):
            return f'_(missing: {name})_\n'
        with open(path) as f:
            return '```\n' + f.read().rstrip('\n') + '\n```\n'

    def img(filename, caption):
        return f'![{caption}]({filename})\n\n*{caption}*\n'

    L = []
    w = L.append

    w('# Motion-Planning Gadget Simulation — Examples Report\n')
    w('_Auto-generated by `src/demo_simulations.py`. All figures and text below '
      'are produced by the verified tooling in `gadget.py`, `gadget_simulation.py`, '
      '`gadget_system.py`, and `gadget_viz.py`._\n')

    w('## 1. Background\n')
    w('Two relations are kept carefully distinct:\n')
    w('- **Equivalence** — do two gadgets admit exactly the same traversal '
      'sequences? Decided by `behaviorally_equivalent` (determinize → minimize → '
      'canonicalize, then compare), the authoritative notion of gadget identity.\n')
    w('- **Simulation** — can a *system* of one gadget reproduce another? We '
      '*refute* it soundly with composition invariants (`refute_simulation`), and '
      'we *confirm* it by building an explicit construction (`GadgetSystem`), '
      'computing the gadget it induces, and checking that against the target. A '
      'PASS is a genuine one-player simulation (the "if and only if"), because the '
      'induced gadget records **every** reachable external-to-external traversal — '
      'not a one-directional containment.\n')

    w('## 2. Reading the diagrams\n')
    w('- **State diagram**: one node per state; an edge `entry→exit` per traversal. '
      'A traversal whose exact reverse exists is drawn once as a blue `entry⇄exit` '
      'bidirectional edge (so reversible gadgets read cleanly).\n')
    w('- **State-location diagram**: the graph the agent literally walks — vertices '
      'are `(state, location)`, one arrow per traversal mode. The most faithful '
      'picture for hand-checking.\n')
    w('- **System diagram**: each instance is a boxed cluster of its ports; grey '
      'edges are wires; bold green boxes are the exposed external interface. A red '
      'overlay with numbered steps shows one traced traversal.\n')

    w('## 3. Single-gadget reference\n')
    w('The three building blocks used below. The **box** view is the paper-style '
      'picture (ports on the boundary, transitions inside, one line style per '
      'state); the state and state-location views are the abstract graphs.\n')
    w('### toggle (reversible)\n')
    w(img('9a_toggle_box.png', 'toggle — box (ports on perimeter, transitions inside)'))
    w(img('9a_toggle_state.png', 'toggle — state diagram'))
    w(img('9a_toggle_stateloc.png', 'toggle — state-location diagram'))
    w('### dicrumbler (irreversible, one-shot)\n')
    w(img('9b_dicrumbler_box.png', 'dicrumbler — box'))
    w(img('9b_dicrumbler_state.png', 'dicrumbler — state diagram'))
    w(img('9b_dicrumbler_stateloc.png', 'dicrumbler — state-location diagram'))
    w('### seven (reversible, nondeterministic)\n')
    w(img('9c_seven_box.png', 'seven — box'))
    w(img('9c_seven_state.png', 'seven — state diagram'))
    w(img('9c_seven_stateloc.png', 'seven — state-location diagram'))

    w('## 4. Equivalence & impossibility results\n')
    w('### 4.1 A toggle cannot simulate a dicrumbler (reversibility)\n')
    w(inc('3_toggle_cannot_simulate_dicrumbler.txt'))
    w('### 4.2 A dicrumbler cannot simulate a toggle (DAG / boundedness)\n')
    w(inc('4_dicrumbler_cannot_simulate_toggle.txt'))
    w('### 4.3 seven vs toggle — invariants are silent\n')
    w(inc('5_seven_vs_toggle.txt'))

    w('## 5. Verified constructions\n')

    w('### 5.1 Two toggles in series ⇒ a toggle\n')
    w(img('10a_series_system.png', 'two toggles in series — wiring'))
    w(inc('10_series_toggle_construction.txt'))
    w(img('10b_series_trace1_LR.png', 'trace L→R from (closed, closed)'))
    w(img('10b_series_trace2_RL.png', 'trace R→L from (open, open)'))

    w('### 5.2 Three toggles in series ⇒ a toggle\n')
    w('The same idea scales; each traversal now threads all three toggles.\n')
    w(img('14a_three_toggles_system.png', 'three toggles in series — wiring'))
    w(img('14b_three_toggles_traceLR.png', 'trace L→R through three toggles'))
    w(inc('14_three_toggles_series.txt'))

    w('### 5.3 Two dicrumblers in parallel ⇒ a two-use gadget\n')
    w('A genuinely non-trivial simulation. Both dicrumblers share the L and R '
      'junctions, so a single L→R traversal lets the agent **choose** which one to '
      'crumble (nondeterminism); the two "one crumbled" configurations behave '
      'identically and are merged by the equivalence check. The result is a gadget '
      'that may be traversed exactly twice.\n')
    w(img('12a_two_use_target_state.png', 'two-use gadget (the target)'))
    w(img('12b_two_dicrumblers_system.png', 'two dicrumblers in parallel — wiring'))
    w(img('12c_two_use_induced_state.png', 'induced gadget, states labelled by configuration'))
    w(inc('12_two_dicrumblers_parallel.txt'))
    w('The two ways to make the first traversal (the agent\'s choice):\n')
    w(img('12d_two_dicrumblers_trace1.png', 'first traversal: crumble D1'))
    w(img('12d_two_dicrumblers_trace2.png', 'first traversal: crumble D2'))

    w('### 5.4 Three dicrumblers in parallel ⇒ a three-use gadget\n')
    w('The construction generalizes: `k` parallel dicrumblers give a `k`-use gadget.\n')
    w(img('13a_three_use_target_state.png', 'three-use gadget (the target)'))
    w(img('13b_three_dicrumblers_system.png', 'three dicrumblers in parallel — wiring'))
    w(inc('13_three_dicrumblers_parallel.txt'))

    w('## 6. Multi-port gadgets and more complex networks\n')
    w('Examples with three or more ports and richer topology than series/parallel '
      'bundles. The box view shows ports on the perimeter (cyclic order) with '
      'transitions inside; tunnels are laid out left/right.\n')

    w('### 6.1 A 2-toggle (4 ports) simulates a toggle\n')
    w('The 2-toggle has two tunnels sharing one state. Exposing only one tunnel '
      'leaves the other unreachable, so the result behaves as a single toggle.\n')
    w(img('16a_2toggle_box.png', '2-toggle — box (two tunnels, shared state)'))
    w(img('16b_2toggle_as_toggle_system.png', 'only the (a,b) tunnel is exposed'))
    w(inc('16_2toggle_simulates_toggle.txt'))

    w('### 6.2 Two 2-toggles in series ⇒ a 2-toggle\n')
    w('A 4-port network joined by two internal wires (one per tunnel). The two '
      'gadgets stay synchronized, so the pair acts as a single 2-toggle.\n')
    w(img('17a_two_2toggles_system.png', 'two 2-toggles in series — wiring'))
    w(img('17b_two_2toggles_trace_ab.png', 'trace a→b threads both gadgets'))
    w(inc('17_two_2toggles_series.txt'))

    w('### 6.3 A branching network ⇒ a 3-port distributor\n')
    w('Two dicrumblers share an entry junction A with separate exits B and C. '
      'Entering A, the agent chooses which to crumble; each exit is available '
      'once, then the gadget is dead.\n')
    w(img('18a_distributor_box.png', 'distributor — box (3 ports)'))
    w(img('18b_distributor_system.png', 'shared-entry dicrumblers — wiring'))
    w(img('18c_distributor_trace_AB.png', 'trace A→B (crumble D1)'))
    w(img('18c_distributor_trace_AC.png', 'trace A→C (route to D2)'))
    w(inc('18_distributor.txt'))

    w('## 7. A rejected construction\n')
    w('### 6.1 Two toggles in parallel — unfaithful\n')
    w('Sharing both endpoints of two toggles looks like it should give a toggle, '
      'but it does not: the verifier detects same-port traversals that change state '
      '(enter L, flip a toggle, return out L), which no gadget can express.\n')
    w(img('15a_two_toggles_parallel_system.png', 'two toggles in parallel (rejected)'))
    w(inc('15_two_toggles_parallel_rejected.txt'))

    w('## 8. Reproduce\n')
    w('```\n/usr/bin/python3 src/demo_simulations.py\n```\n')
    w('(The project `.venv` lacks graphviz; the system Python has it plus the '
      '`dot` binary. Tests: `python3 -m unittest test_gadget_simulation '
      'test_gadget_system` etc.)\n')

    path = os.path.join(OUT, 'REPORT.md')
    with open(path, 'w') as f:
        f.write('\n'.join(L))
    print(f'  wrote {os.path.relpath(path)}')


build_report()


# ── Summary ─────────────────────────────────────────────────────────────────
print()
print('All outputs written to demo_output/.')
print()
print('TEXT FILES (open in any editor, cross-check against the diagrams):')
for f in sorted(os.listdir(OUT)):
    if f.endswith('.txt'):
        print(f'  demo_output/{f}')
print()
print('PNG DIAGRAMS:')
for f in sorted(os.listdir(OUT)):
    if f.endswith('.png'):
        print(f'  demo_output/{f}')
