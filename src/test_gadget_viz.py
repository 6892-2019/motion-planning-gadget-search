#!/usr/bin/env python3

import unittest
from gadget import Gadget
from gadget_viz import (visualize_gadget, visualize_gadget_comparison,
                        visualize_gadget_circular, visualize_gadget_box,
                        visualize_system)
from gadget_system import GadgetSystem
import os

class TestGadgetVisualization(unittest.TestCase):
    def setUp(self):
        # Create output directory if it doesn't exist
        self.output_dir = "viz_output"
        if not os.path.exists(self.output_dir):
            os.makedirs(self.output_dir)
    
    def test_simple_gadget_visualization(self):
        # Create a simple 2-state, 3-location gadget
        g = Gadget(2, 3, reversible=False)
        
        # Add some transitions
        g.add_transition(0, 0, 1, 1)  # State 0, Loc 0 -> State 1, Loc 1
        g.add_transition(1, 1, 0, 2)  # State 1, Loc 1 -> State 0, Loc 2
        g.add_transition(0, 2, 1, 0)  # State 0, Loc 2 -> State 1, Loc 0
        
        # Visualize the gadget
        output_path = os.path.join(self.output_dir, "simple_gadget")
        visualize_gadget(g, output_path)
        
        # Assert the output file exists
        self.assertTrue(os.path.exists(f"{output_path}.png"))
    
    def test_reversible_gadget_visualization(self):
        # Create a reversible 2-state, 2-location gadget
        g = Gadget(2, 2, reversible=True)
        g.add_transition(0, 0, 1, 1)
        
        output_path = os.path.join(self.output_dir, "reversible_gadget")
        visualize_gadget(g, output_path)
        self.assertTrue(os.path.exists(f"{output_path}.png"))
    
    def test_gadget_comparison(self):
        # Create two similar but different gadgets
        g1 = Gadget(2, 2, reversible=False)
        g1.add_transition(0, 0, 1, 1)
        
        g2 = Gadget(2, 2, reversible=True)
        g2.add_transition(0, 0, 1, 1)
        
        output_path = os.path.join(self.output_dir, "gadget_comparison")
        visualize_gadget_comparison(g1, g2, output_path)
        self.assertTrue(os.path.exists(f"{output_path}.png"))

    def test_circular_visualization(self):
        # Create a simple 2-state, 3-location gadget
        g = Gadget(2, 3, reversible=False)
        
        # Add some transitions that form a cycle
        g.add_transition(0, 0, 1, 1)  # State 0, Loc 0 -> State 1, Loc 1
        g.add_transition(1, 1, 0, 2)  # State 1, Loc 1 -> State 0, Loc 2
        g.add_transition(0, 2, 1, 0)  # State 0, Loc 2 -> State 1, Loc 0
        
        # Visualize the gadget in circular style
        output_path = os.path.join(self.output_dir, "circular_gadget")
        visualize_gadget_circular(g, output_path)
        self.assertTrue(os.path.exists(f"{output_path}.png"))

        # Create a reversible 2-state, 2-location gadget
        g = Gadget(2, 2, reversible=True)
        g.add_transition(0, 0, 1, 1)
        
        output_path = os.path.join(self.output_dir, "circular_reversible")
        visualize_gadget_circular(g, output_path)
        self.assertTrue(os.path.exists(f"{output_path}.png"))

        # Create an l2t
        g = Gadget(3, 4, reversible=True)
        g.add_transition(0, 0, 1, 1)
        g.add_transition(0, 2, 2, 3)
       
        output_path = os.path.join(self.output_dir, "l2t_circular")
        visualize_gadget_circular(g, output_path)
        self.assertTrue(os.path.exists(f"{output_path}.png"))

        # Create a door
        g = Gadget(2, 6, reversible=False)
        g.add_transition(0, 0, 0, 1)
        g.add_transition(0, 2, 0, 3)
        g.add_transition(0, 4, 0, 5)

        g.add_transition(1, 4, 1, 5)
        g.add_transition(1, 2, 0, 3)
        
        output_path = os.path.join(self.output_dir, "door_circular")
        visualize_gadget_circular(g, output_path)
        self.assertTrue(os.path.exists(f"{output_path}.png"))

class TestBoxAndSystemDiagrams(unittest.TestCase):
    def setUp(self):
        self.output_dir = "viz_output"
        os.makedirs(self.output_dir, exist_ok=True)

    def _toggle(self):
        g = Gadget(2, 2)
        g.add_transition(0, 0, 1, 1)
        g.add_transition(1, 1, 0, 0)
        return g

    def test_gadget_box_writes_svg(self):
        out = os.path.join(self.output_dir, "box_toggle")
        visualize_gadget_box(self._toggle(), out,
                             state_labels={0: 'closed', 1: 'open'},
                             loc_labels={0: 'L', 1: 'R'}, title='toggle')
        self.assertTrue(os.path.exists(f"{out}.svg"))   # SVG always written

    def test_system_diagram_with_trace_writes_svg(self):
        s = GadgetSystem()
        s.add_instance('T1', self._toggle(), 0).add_instance('T2', self._toggle(), 0)
        s.connect(('T1', 1), ('T2', 0))
        s.expose('L', ('T1', 0)).expose('R', ('T2', 1))
        out = os.path.join(self.output_dir, "sys_series")
        visualize_system(s, out, highlight_entry='L',
                         highlight_moves=s.trace((0, 0), 'L', 'R'),
                         title='series')
        self.assertTrue(os.path.exists(f"{out}.svg"))

    def test_system_diagram_column_layout(self):
        s = GadgetSystem()
        d = Gadget(2, 2)
        d.add_transition(0, 0, 1, 1)
        s.add_instance('D1', d, 0).add_instance('D2', d, 0)
        s.connect(('D1', 0), ('D2', 0)).connect(('D1', 1), ('D2', 1))
        s.expose('L', ('D1', 0)).expose('R', ('D1', 1))
        out = os.path.join(self.output_dir, "sys_parallel")
        visualize_system(s, out, positions={'D1': (0, 0), 'D2': (0, 1)},
                         title='parallel')
        self.assertTrue(os.path.exists(f"{out}.svg"))


if __name__ == '__main__':
    unittest.main()
