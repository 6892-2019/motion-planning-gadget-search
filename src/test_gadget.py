#!/usr/bin/env python3

import unittest
from gadget import Gadget
from gadget_viz import visualize_gadget_comparison
import io
import sys

class TestGadget(unittest.TestCase):
    def test_gadget_creation(self):
        """Test basic gadget creation and properties."""
        g = Gadget(2, 3)
        self.assertEqual(g.num_states, 2)
        self.assertEqual(g.num_locations, 3)
        self.assertEqual(len(g.transitions), 0)
        self.assertFalse(g.reversible)

    def test_add_directed_transition(self):
        """Test adding directed transitions."""
        g = Gadget(2, 3, reversible=False)
        self.assertTrue(g.add_transition(0, 0, 1, 1))
        self.assertEqual(len(g.transitions), 1)
        
        # Adding reverse transition should create new transition
        self.assertTrue(g.add_transition(1, 1, 0, 0))
        self.assertEqual(len(g.transitions), 2)
        
        # Check transitions are distinct
        transitions_from_0_0 = g.get_transitions_from(0, 0)
        transitions_from_1_1 = g.get_transitions_from(1, 1)
        self.assertEqual(len(transitions_from_0_0), 1)
        self.assertEqual(len(transitions_from_1_1), 1)

    def test_add_reversible_transition(self):
        """Test adding transitions in reversible gadget."""
        g = Gadget(2, 3, reversible=True)
        self.assertTrue(g.add_transition(0, 0, 1, 1))
        self.assertEqual(len(g.transitions), 2)  # Should add both directions
        
        # Adding reverse transition should not create new transitions
        self.assertTrue(g.add_transition(1, 1, 0, 0))
        self.assertEqual(len(g.transitions), 2)
        
        # Check both directions exist
        transitions_from_0_0 = g.get_transitions_from(0, 0)
        transitions_from_1_1 = g.get_transitions_from(1, 1)
        self.assertEqual(len(transitions_from_0_0), 1)
        self.assertEqual(len(transitions_from_1_1), 1)

    def test_add_invalid_transition(self):
        """Test adding invalid transitions."""
        g = Gadget(2, 3)
        # Out of bounds state
        self.assertFalse(g.add_transition(2, 0, 0, 1))
        # Out of bounds location
        self.assertFalse(g.add_transition(0, 3, 1, 1))
        # Self-loop (same location)
        self.assertFalse(g.add_transition(0, 1, 1, 1))

    def test_get_transitions(self):
        """Test getting transitions from a state-location pair."""
        g = Gadget(2, 3)
        g.add_transition(0, 0, 1, 1)
        g.add_transition(0, 0, 1, 2)
        
        transitions = g.get_transitions_from(0, 0)
        self.assertEqual(len(transitions), 2)
        self.assertIn((1, 1), transitions)
        self.assertIn((1, 2), transitions)
        
        # Test that reverse transitions don't exist in non-reversible gadget
        transitions = g.get_transitions_from(1, 1)
        self.assertEqual(len(transitions), 0)

    def test_deterministic(self):
        """Test deterministic property checking."""
        g = Gadget(2, 3)
        # Add transitions that make it deterministic
        g.add_transition(0, 0, 1, 1)
        g.add_transition(0, 0, 1, 2)
        self.assertTrue(g.is_deterministic())
        
        # Add transition that makes it non-deterministic
        g.add_transition(0, 0, 0, 2)
        self.assertFalse(g.is_deterministic())

    def test_gadget_equivalence_identical(self):
        """Test equivalence of identical gadgets."""
        g1 = Gadget(2, 3)
        g2 = Gadget(2, 3)
        
        g1.add_transition(0, 0, 1, 1)
        g2.add_transition(0, 0, 1, 1)
        
        self.assertTrue(g1.is_equivalent_to(g2))

    def test_gadget_equivalence_isomorphic(self):
        """Test equivalence of isomorphic gadgets."""
        g1 = Gadget(2, 3)
        g2 = Gadget(2, 3)
        
        # Add transitions with different state numbering but same structure
        g1.add_transition(0, 0, 1, 1)
        g1.add_transition(0, 0, 1, 2)
        
        g2.add_transition(1, 0, 0, 1)
        g2.add_transition(1, 0, 0, 2)
        
        self.assertTrue(g1.is_equivalent_to(g2))

    def test_gadget_nonequivalence(self):
        """Test non-equivalence of different gadgets."""
        g1 = Gadget(2, 3)
        g2 = Gadget(2, 3)

        g1.add_transition(0, 0, 1, 1)
        g2.add_transition(0, 0, 1, 2)

        self.assertFalse(g1.is_equivalent_to(g2))

        # Different number of transitions
        g2.add_transition(0, 1, 1, 2)
        self.assertFalse(g1.is_equivalent_to(g2))

    def test_isomorphic_location_relabeling(self):
        """is_isomorphic_to catches equivalence that only holds under location relabeling."""
        # g1: enter loc0 → exit loc1 (state 0→1)
        # g2: enter loc1 → exit loc0 (state 0→1) — same structure, locations swapped
        g1 = Gadget(2, 2)
        g2 = Gadget(2, 2)
        g1.add_transition(0, 0, 1, 1)
        g2.add_transition(0, 1, 1, 0)

        # is_equivalent_to holds locations fixed → not equivalent
        self.assertFalse(g1.is_equivalent_to(g2))
        # is_isomorphic_to tries all loc permutations → isomorphic
        self.assertTrue(g1.is_isomorphic_to(g2))

    def test_isomorphic_joint_state_and_location(self):
        """is_isomorphic_to finds equivalence under a joint state+location permutation."""
        # g1: (state 0, loc 0) → (state 1, loc 1)
        # g2: (state 1, loc 1) → (state 0, loc 0)  — both states AND locations swapped
        g1 = Gadget(2, 2)
        g2 = Gadget(2, 2)
        g1.add_transition(0, 0, 1, 1)
        g2.add_transition(1, 1, 0, 0)

        self.assertFalse(g1.is_equivalent_to(g2))
        self.assertTrue(g1.is_isomorphic_to(g2))

    def test_isomorphic_truly_different(self):
        """Gadgets with different abstract structure are not isomorphic."""
        # g1 has a self-loop in state space; g2 does not
        g1 = Gadget(2, 2)
        g2 = Gadget(2, 2)
        g1.add_transition(0, 0, 0, 1)  # state stays 0
        g2.add_transition(0, 0, 1, 1)  # state changes to 1

        self.assertFalse(g1.is_isomorphic_to(g2))

    def test_reversible_nonequivalence(self):
        """Test non-equivalence of reversible and non-reversible gadgets."""
        g1 = Gadget(2, 3, reversible=True)
        g2 = Gadget(2, 3, reversible=False)
        
        g1.add_transition(0, 0, 1, 1)  # Will add both directions
        g2.add_transition(0, 0, 1, 1)  # Only one direction
        g2.add_transition(1, 1, 0, 0)  # Add reverse manually
        
        # Even though they have the same transitions, they should not be equivalent
        # because one is reversible and the other isn't
        self.assertFalse(g1.is_equivalent_to(g2))

    def test_closure_simple(self):
        """Test closure on a simple linear chain of transitions."""
        # Create a gadget with a chain of transitions through location 0
        g = Gadget(4, 2)  # 4 states, 2 locations
        g.add_transition(0, 0, 1, 1)  # 0 --(loc0)--> 1
        g.add_transition(1, 0, 2, 1)  # 1 --(loc0)--> 2
        g.add_transition(2, 0, 3, 1)  # 2 --(loc0)--> 3
        
        # Compute closure
        g_closed = g.closure()
        
        # Visualize the before/after
        visualize_gadget_comparison(g, g_closed, 'test_closure_simple', format='png')
        
        # Check that all possible shortcuts were added
        assert (0, 0, 2, 1) in g_closed.transitions  # 0 -> 2
        assert (1, 0, 3, 1) in g_closed.transitions  # 1 -> 3
        assert (0, 0, 3, 1) in g_closed.transitions  # 0 -> 3

    def test_closure_branching(self):
        """Test closure on a gadget with branching paths."""
        # Create a gadget with branching paths
        g = Gadget(4, 2)
        # Path through location 0
        g.add_transition(0, 0, 1, 1)
        g.add_transition(1, 0, 2, 1)
        # Branch at state 1 through location 0
        g.add_transition(1, 0, 3, 1)
        
        # Compute closure
        g_closed = g.closure()
        
        # Visualize the before/after
        visualize_gadget_comparison(g, g_closed, 'test_closure_branching', format='png')
        
        # Check that shortcuts were added
        assert (0, 0, 2, 1) in g_closed.transitions  # 0 -> 2
        assert (0, 0, 3, 1) in g_closed.transitions  # 0 -> 3

    def test_closure_reversible(self):
        """Test closure on a reversible gadget."""
        # Create a reversible gadget
        g = Gadget(3, 2, reversible=True)
        g.add_transition(0, 0, 1, 1)  # 0 <--(loc0)--> 1
        g.add_transition(1, 0, 2, 1)  # 1 <--(loc0)--> 2

        # Compute closure
        g_closed = g.closure()

        # Visualize the before/after
        visualize_gadget_comparison(g, g_closed, 'test_closure_reversible', format='png')

        # Forward shortcut: enter loc 0 in state 0, exit loc 1 in state 2
        assert (0, 0, 2, 1) in g_closed.transitions
        # Correct reverse: enter loc 1 in state 2, exit loc 0 in state 0
        assert (2, 1, 0, 0) in g_closed.transitions
        # The buggy reverse (enter loc 0 in state 2, exit loc 1 in state 0) must NOT be added
        assert (2, 0, 0, 1) not in g_closed.transitions

    def test_closure_idempotent(self):
        """Closure must satisfy closure(closure(g)) == closure(g)."""
        # Directed chain
        g = Gadget(4, 2)
        g.add_transition(0, 0, 1, 1)
        g.add_transition(1, 0, 2, 1)
        g.add_transition(2, 0, 3, 1)
        g_closed = g.closure()
        self.assertEqual(set(g_closed.transitions), set(g_closed.closure().transitions))

        # Reversible chain
        g_rev = Gadget(3, 2, reversible=True)
        g_rev.add_transition(0, 0, 1, 1)
        g_rev.add_transition(1, 0, 2, 1)
        g_rev_closed = g_rev.closure()
        self.assertEqual(set(g_rev_closed.transitions), set(g_rev_closed.closure().transitions))

        # Branching gadget
        g_branch = Gadget(4, 2)
        g_branch.add_transition(0, 0, 1, 1)
        g_branch.add_transition(1, 0, 2, 1)
        g_branch.add_transition(1, 0, 3, 1)
        g_branch_closed = g_branch.closure()
        self.assertEqual(set(g_branch_closed.transitions), set(g_branch_closed.closure().transitions))

    def test_trace_success(self):
        """Trace returns True and prints steps for a valid move sequence."""
        g = Gadget(2, 2)
        g.add_transition(0, 0, 1, 1)
        g.add_transition(1, 1, 0, 0)

        out = io.StringIO()
        sys.stdout = out
        result = g.trace(0, [(0, 1), (1, 0)])
        sys.stdout = sys.__stdout__

        self.assertTrue(result)
        output = out.getvalue()
        self.assertIn('state 0 -> state 1', output)
        self.assertIn('state 1 -> state 0', output)

    def test_trace_failure(self):
        """Trace returns False and prints failure info for an impossible move."""
        g = Gadget(2, 2)
        g.add_transition(0, 0, 1, 1)

        out = io.StringIO()
        sys.stdout = out
        result = g.trace(0, [(1, 0)])  # No transition from loc 1
        sys.stdout = sys.__stdout__

        self.assertFalse(result)
        self.assertIn('FAILED', out.getvalue())

    def test_trace_with_labels(self):
        """Trace uses state and location names when provided."""
        g = Gadget(2, 2)
        g.add_transition(0, 0, 1, 1)

        out = io.StringIO()
        sys.stdout = out
        result = g.trace(0, [(0, 1)],
                         state_labels={0: 'open', 1: 'closed'},
                         loc_labels={0: 'in', 1: 'out'})
        sys.stdout = sys.__stdout__

        self.assertTrue(result)
        output = out.getvalue()
        self.assertIn('open', output)
        self.assertIn('closed', output)
        self.assertIn('in', output)
        self.assertIn('out', output)


if __name__ == '__main__':
    unittest.main()
