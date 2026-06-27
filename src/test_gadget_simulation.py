#!/usr/bin/env python3

import unittest
from gadget import Gadget
from gadget_simulation import (
    get_traversal_modes,
    is_reversible,
    is_dag,
    minimize,
    behaviorally_equivalent,
    refute_simulation,
)


# ── shared gadgets ───────────────────────────────────────────────────────────

def make_toggle():
    g = Gadget(2, 2)
    g.add_transition(0, 0, 1, 1)
    g.add_transition(1, 1, 0, 0)   # reverse: toggle is reversible
    return g


def make_dicrumbler():
    g = Gadget(2, 2)
    g.add_transition(0, 0, 1, 1)   # one-way only: irreversible, DAG
    return g


class TestGetTraversalModes(unittest.TestCase):
    def test_direct_modes(self):
        g = Gadget(2, 2)
        g.add_transition(0, 0, 1, 1)
        modes = get_traversal_modes(g, take_closure=False)
        self.assertIn((0, 0, 1, 1), modes)
        self.assertEqual(len(modes), 1)

    def test_closure_adds_shortcuts(self):
        g = Gadget(3, 2)
        g.add_transition(0, 0, 1, 1)
        g.add_transition(1, 0, 2, 1)
        modes = get_traversal_modes(g, take_closure=True)
        self.assertIn((0, 0, 1, 1), modes)
        self.assertIn((1, 0, 2, 1), modes)
        self.assertIn((0, 0, 2, 1), modes)   # shortcut added by closure


class TestInvariants(unittest.TestCase):
    def test_toggle_is_reversible(self):
        self.assertTrue(is_reversible(make_toggle()))

    def test_dicrumbler_is_not_reversible(self):
        self.assertFalse(is_reversible(make_dicrumbler()))

    def test_reversible_detected_from_transitions_not_flag(self):
        # Built with the directed API but with both directions present:
        # reversibility is a property of the transitions, not the flag.
        g = Gadget(2, 2)
        g.add_transition(0, 0, 1, 1)
        g.add_transition(1, 1, 0, 0)
        self.assertFalse(g.reversible)      # construction flag is off
        self.assertTrue(is_reversible(g))   # but it is reversible in fact

    def test_dicrumbler_is_dag(self):
        self.assertTrue(is_dag(make_dicrumbler()))

    def test_toggle_is_not_dag(self):
        self.assertFalse(is_dag(make_toggle()))

    def test_self_loop_is_not_dag(self):
        g = Gadget(1, 2)
        g.add_transition(0, 0, 0, 1)   # self-loop on the single state
        self.assertFalse(is_dag(g))


class TestMinimize(unittest.TestCase):
    def test_idempotent_on_minimal(self):
        t = make_toggle()
        m = minimize(t)
        self.assertEqual(m.num_states, 2)
        self.assertEqual(set(m.transitions), set(t.transitions))

    def test_merges_indistinguishable_states(self):
        # States 1 and 2 behave identically (both dead ends), so they merge.
        g = Gadget(3, 2)
        g.add_transition(0, 0, 1, 1)
        g.add_transition(0, 1, 2, 0)   # both 1 and 2 are dead after entry
        m = minimize(g)
        self.assertEqual(m.num_states, 2)

    def test_rejects_nondeterministic(self):
        g = Gadget(2, 3)
        g.add_transition(0, 0, 1, 1)
        g.add_transition(0, 0, 1, 2)   # same (state, entry), two exits
        with self.assertRaises(ValueError):
            minimize(g)


class TestBehavioralEquivalence(unittest.TestCase):
    def test_identical(self):
        self.assertTrue(behaviorally_equivalent(make_toggle(), make_toggle()))

    def test_state_relabeled_equivalent(self):
        g1 = make_toggle()
        g2 = Gadget(2, 2)
        g2.add_transition(1, 0, 0, 1)   # same structure, states swapped
        g2.add_transition(0, 1, 1, 0)
        self.assertTrue(behaviorally_equivalent(g1, g2))

    def test_toggle_not_equivalent_to_dicrumbler(self):
        # The crux: the toggle has the reverse traversal the dicrumbler lacks.
        self.assertFalse(
            behaviorally_equivalent(make_toggle(), make_dicrumbler()))

    def test_extra_behavior_breaks_equivalence(self):
        small = Gadget(2, 2)
        small.add_transition(0, 0, 1, 1)
        large = Gadget(2, 2)
        large.add_transition(0, 0, 1, 1)
        large.add_transition(1, 0, 0, 1)   # extra reachable mode
        self.assertFalse(behaviorally_equivalent(small, large))

    def test_fixed_locations_distinguishes_ports(self):
        g1 = Gadget(2, 2)
        g1.add_transition(0, 0, 1, 1)
        g2 = Gadget(2, 2)
        g2.add_transition(0, 1, 1, 0)   # same up to swapping the two ports
        self.assertTrue(behaviorally_equivalent(g1, g2))                 # ports abstract
        self.assertFalse(behaviorally_equivalent(g1, g2,
                                                 fixed_locations=True))   # ports fixed


class TestRefuteSimulation(unittest.TestCase):
    def test_toggle_cannot_simulate_dicrumbler(self):
        # Reversible block, irreversible target → reversibility obstruction.
        reasons = refute_simulation(make_dicrumbler(), make_toggle(),
                                    verbose=False)
        self.assertTrue(reasons)
        self.assertTrue(any('reversib' in r for r in reasons))

    def test_dicrumbler_cannot_simulate_toggle(self):
        # DAG block, cyclic target → boundedness obstruction.
        reasons = refute_simulation(make_toggle(), make_dicrumbler(),
                                    verbose=False)
        self.assertTrue(reasons)
        self.assertTrue(any('DAG' in r or 'bounded' in r for r in reasons))

    def test_no_obstruction_between_two_toggles(self):
        # Same gadget: no sound obstruction (and indeed a trivial simulation
        # exists).  Empty list means "not refuted", not "proven possible".
        self.assertEqual(
            refute_simulation(make_toggle(), make_toggle(), verbose=False), [])


if __name__ == '__main__':
    unittest.main()
