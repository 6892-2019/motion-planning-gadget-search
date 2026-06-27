#!/usr/bin/env python3

import unittest
from gadget import Gadget
from gadget_system import GadgetSystem
from gadget_simulation import behaviorally_equivalent, is_reversible


def toggle():
    g = Gadget(2, 2)
    g.add_transition(0, 0, 1, 1)
    g.add_transition(1, 1, 0, 0)
    return g


def dicrumbler():
    g = Gadget(2, 2)
    g.add_transition(0, 0, 1, 1)
    return g


def series_toggles():
    s = GadgetSystem()
    s.add_instance('T1', toggle(), 0).add_instance('T2', toggle(), 0)
    s.connect(('T1', 1), ('T2', 0))
    s.expose('L', ('T1', 0)).expose('R', ('T2', 1))
    return s


class TestInducedGadget(unittest.TestCase):
    def test_single_instance_is_itself(self):
        s = GadgetSystem()
        s.add_instance('X', toggle(), 0)
        s.expose('L', ('X', 0)).expose('R', ('X', 1))
        induced, configs, labels, defects = s.induced_gadget()
        self.assertEqual(defects, [])
        self.assertTrue(behaviorally_equivalent(induced, toggle()))

    def test_series_toggles_induce_a_toggle(self):
        induced, configs, labels, defects = series_toggles().induced_gadget()
        self.assertEqual(defects, [])
        # Two reachable configs: (0,0) and (1,1).
        self.assertEqual(set(configs), {(0, 0), (1, 1)})
        self.assertTrue(behaviorally_equivalent(induced, toggle()))
        self.assertTrue(is_reversible(induced))

    def test_same_port_state_change_is_a_defect(self):
        # Wire a toggle's R back to its L so the agent can flip it and return.
        s = GadgetSystem()
        s.add_instance('X', toggle(), 0)
        s.connect(('X', 1), ('X', 0))
        s.expose('A', ('X', 0)).expose('B', ('X', 1))
        _, _, _, defects = s.induced_gadget()
        self.assertTrue(defects)


def k_use(k):
    g = Gadget(k + 1, 2)
    for s in range(k):
        g.add_transition(s, 0, s + 1, 1)
    return g


def parallel(block, count):
    s = GadgetSystem()
    names = [f'B{i}' for i in range(count)]
    for n in names:
        s.add_instance(n, block(), 0)
    for n in names[1:]:
        s.connect((names[0], 0), (n, 0))
        s.connect((names[0], 1), (n, 1))
    s.expose('L', (names[0], 0)).expose('R', (names[0], 1))
    return s


class TestNonTrivialConstructions(unittest.TestCase):
    def test_two_dicrumblers_parallel_is_two_use(self):
        s = parallel(dicrumbler, 2)
        induced, configs, _, defects = s.induced_gadget()
        self.assertEqual(defects, [])
        self.assertEqual(len(configs), 4)            # 2^2 configs
        self.assertTrue(behaviorally_equivalent(induced, k_use(2)))

    def test_three_dicrumblers_parallel_is_three_use(self):
        s = parallel(dicrumbler, 3)
        self.assertTrue(
            s.verify_simulates(k_use(3),
                               label_to_target_loc={'L': 0, 'R': 1},
                               verbose=False))

    def test_two_toggles_parallel_is_rejected(self):
        s = parallel(toggle, 2)
        _, _, _, defects = s.induced_gadget()
        self.assertTrue(defects)                     # same-port state changes
        self.assertFalse(
            s.verify_simulates(toggle(),
                               label_to_target_loc={'L': 0, 'R': 1},
                               verbose=False))

    def test_three_toggles_series_is_toggle(self):
        s = GadgetSystem()
        for n in ['T1', 'T2', 'T3']:
            s.add_instance(n, toggle(), 0)
        s.connect(('T1', 1), ('T2', 0)).connect(('T2', 1), ('T3', 0))
        s.expose('L', ('T1', 0)).expose('R', ('T3', 1))
        self.assertTrue(
            s.verify_simulates(toggle(),
                               label_to_target_loc={'L': 0, 'R': 1},
                               verbose=False))


def two_toggle():
    g = Gadget(2, 4)
    for t in [(0, 0, 1, 1), (0, 2, 1, 3), (1, 1, 0, 0), (1, 3, 0, 2)]:
        g.add_transition(*t)
    return g


class TestMultiPortConstructions(unittest.TestCase):
    def test_2toggle_simulates_toggle_one_tunnel(self):
        s = GadgetSystem()
        s.add_instance('X', two_toggle(), 0)
        s.expose('L', ('X', 0)).expose('R', ('X', 1))   # (c,d) tunnel unused
        self.assertTrue(
            s.verify_simulates(toggle(),
                               label_to_target_loc={'L': 0, 'R': 1},
                               verbose=False))

    def test_two_2toggles_series_is_2toggle(self):
        s = GadgetSystem()
        s.add_instance('A', two_toggle(), 0).add_instance('B', two_toggle(), 0)
        s.connect(('A', 1), ('B', 0)).connect(('A', 3), ('B', 2))
        for lbl, p in [('a', ('A', 0)), ('b', ('B', 1)),
                       ('c', ('A', 2)), ('d', ('B', 3))]:
            s.expose(lbl, p)
        induced, configs, _, defects = s.induced_gadget()
        self.assertEqual(defects, [])
        self.assertEqual(len(configs), 2)               # gadgets stay synced
        self.assertTrue(
            s.verify_simulates(two_toggle(),
                               label_to_target_loc={'a': 0, 'b': 1, 'c': 2, 'd': 3},
                               verbose=False))

    def test_shared_entry_dicrumblers_is_distributor(self):
        distributor = Gadget(4, 3)
        for t in [(0, 0, 1, 1), (0, 0, 2, 2), (1, 0, 3, 2), (2, 0, 3, 1)]:
            distributor.add_transition(*t)
        s = GadgetSystem()
        s.add_instance('D1', dicrumbler(), 0).add_instance('D2', dicrumbler(), 0)
        s.connect(('D1', 0), ('D2', 0))
        s.expose('A', ('D1', 0)).expose('B', ('D1', 1)).expose('C', ('D2', 1))
        self.assertTrue(
            s.verify_simulates(distributor,
                               label_to_target_loc={'A': 0, 'B': 1, 'C': 2},
                               verbose=False))
        # the agent can reach both B and C from the fresh configuration
        self.assertIsNotNone(s.trace((0, 0), 'A', 'B'))
        self.assertIsNotNone(s.trace((0, 0), 'A', 'C'))


class TestVerifySimulates(unittest.TestCase):
    def test_series_simulates_toggle(self):
        s = series_toggles()
        self.assertTrue(
            s.verify_simulates(toggle(),
                               label_to_target_loc={'L': 0, 'R': 1},
                               verbose=False))

    def test_series_does_not_simulate_dicrumbler(self):
        s = series_toggles()
        self.assertFalse(
            s.verify_simulates(dicrumbler(),
                               label_to_target_loc={'L': 0, 'R': 1},
                               verbose=False))

    def test_verify_without_fixed_ports_tries_relabelings(self):
        # Expose ports in the "wrong" order; relabeling should still find it.
        s = GadgetSystem()
        s.add_instance('T1', toggle(), 0).add_instance('T2', toggle(), 0)
        s.connect(('T1', 1), ('T2', 0))
        s.expose('R', ('T2', 1)).expose('L', ('T1', 0))   # reversed order
        self.assertTrue(s.verify_simulates(toggle(), verbose=False))


class TestTrace(unittest.TestCase):
    def test_trace_LR_steps(self):
        s = series_toggles()
        moves = s.trace((0, 0), 'L', 'R')
        self.assertIsNotNone(moves)
        kinds = [m[0] for m in moves]
        self.assertEqual(kinds, ['traverse', 'wire', 'traverse'])
        # First traverse is T1 entering loc 0; last is T2 exiting loc 1.
        self.assertEqual(moves[0][1], 'T1')
        self.assertEqual(moves[-1][1], 'T2')

    def test_trace_returns_none_when_no_traversal(self):
        # From config (1,1) there is no L→R traversal (only R→L).
        s = series_toggles()
        self.assertIsNone(s.trace((1, 1), 'L', 'R'))

    def test_trace_applies_to_correct_end_config(self):
        s = series_toggles()
        moves = s.trace((0, 0), 'L', 'R', end_config=(1, 1))
        self.assertIsNotNone(moves)


class TestValidation(unittest.TestCase):
    def test_bad_port_rejected(self):
        s = GadgetSystem()
        s.add_instance('X', toggle(), 0)
        with self.assertRaises(ValueError):
            s.connect(('X', 5), ('X', 0))

    def test_duplicate_external_label_rejected(self):
        s = GadgetSystem()
        s.add_instance('X', toggle(), 0)
        s.expose('L', ('X', 0))
        with self.assertRaises(ValueError):
            s.expose('L', ('X', 1))


if __name__ == '__main__':
    unittest.main()
