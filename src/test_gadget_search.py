#!/usr/bin/env python3

import unittest
from gadget import Gadget
from gadget_search import search_gadget_path, find_reachable_states, analyze_gadget_connectivity

class TestGadgetSearch(unittest.TestCase):
    def setUp(self):
        # Create a simple test gadget
        self.gadget = Gadget(2, 3, reversible=False)  # 2 states, 3 locations
        
        # Add transitions to form a simple path:
        # s0l0 -> s0l1 -> s1l2 -> s1l0
        self.gadget.add_transition(0, 0, 0, 1)  # s0l0 -> s0l1
        self.gadget.add_transition(0, 1, 1, 2)  # s0l1 -> s1l2
        self.gadget.add_transition(1, 2, 1, 0)  # s1l2 -> s1l0
        
    def test_search_gadget_path(self):
        # Test finding a path that exists
        path = search_gadget_path(self.gadget, 0, 0, 1, 2)
        self.assertIsNotNone(path)
        self.assertEqual(path, [(0, 0), (0, 1), (1, 2)])
        
        # Test finding a path that doesn't exist
        path = search_gadget_path(self.gadget, 0, 0, 0, 2)
        self.assertIsNone(path)
        
        # Test invalid input
        path = search_gadget_path(self.gadget, 2, 0, 0, 0)  # Invalid state
        self.assertIsNone(path)
        
    def test_find_reachable_states(self):
        # Test reachable states from start
        reachable = find_reachable_states(self.gadget, 0, 0)
        expected = {(0, 0), (0, 1), (1, 2), (1, 0)}  # Can reach s0l1, s1l2, and s1l0
        self.assertEqual(reachable, expected)
        
        # Test reachable states from middle
        reachable = find_reachable_states(self.gadget, 0, 1)
        expected = {(0, 1), (1, 2), (1, 0)}  # Can reach s1l2 and s1l0
        self.assertEqual(reachable, expected)
        
        # Test reachable states from end
        reachable = find_reachable_states(self.gadget, 1, 2)
        expected = {(1, 2), (1, 0)}  # Can reach s1l0
        self.assertEqual(reachable, expected)
        
        # Test invalid input
        reachable = find_reachable_states(self.gadget, 2, 0)  # Invalid state
        self.assertEqual(reachable, set())
        
    def test_analyze_gadget_connectivity(self):
        # Test full connectivity analysis
        connectivity = analyze_gadget_connectivity(self.gadget)
        
        # Check connectivity from each state-location pair
        self.assertEqual(connectivity[(0, 0)], {(0, 0), (0, 1), (1, 2), (1, 0)})  # Can reach s0l1, s1l2, and s1l0
        self.assertEqual(connectivity[(0, 1)], {(0, 1), (1, 2), (1, 0)})  # Can reach s1l2 and s1l0
        self.assertEqual(connectivity[(0, 2)], {(0, 2)})  # No outgoing transitions
        self.assertEqual(connectivity[(1, 0)], {(1, 0)})  # No outgoing transitions from s1l0
        self.assertEqual(connectivity[(1, 1)], {(1, 1)})  # No outgoing transitions
        self.assertEqual(connectivity[(1, 2)], {(1, 2), (1, 0)})  # Can reach s1l0
        
    def test_reversible_gadget(self):
        # Create a reversible test gadget
        gadget = Gadget(2, 3, reversible=True)
        gadget.add_transition(0, 0, 0, 1)  # s0l0 <-> s0l1
        gadget.add_transition(0, 1, 1, 2)  # s0l1 <-> s1l2
        
        # Test path finding in both directions
        path1 = search_gadget_path(gadget, 0, 0, 1, 2)
        path2 = search_gadget_path(gadget, 1, 2, 0, 0)
        
        self.assertEqual(path1, [(0, 0), (0, 1), (1, 2)])
        self.assertEqual(path2, [(1, 2), (0, 1), (0, 0)])
        
if __name__ == '__main__':
    unittest.main()
