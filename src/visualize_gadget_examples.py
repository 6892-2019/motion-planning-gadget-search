#!/usr/bin/env python3

from gadget import Gadget
from gadget_viz import visualize_gadget_circular

def visualize_cycle_gadget():
    """Create and visualize a cyclic gadget with 3 states."""
    g = Gadget(3, 1)  # 3 states, 1 location
    # Create a cycle: 0 -> 1 -> 2 -> 0
    g.add_transition(0, 0, 1, 0)
    g.add_transition(1, 0, 2, 0)
    g.add_transition(2, 0, 0, 0)
    
    g_closed = g.closure()
    visualize_gadget_circular(g, 'cycle_gadget', format='png')
    visualize_gadget_circular(g_closed, 'cycle_gadget_closed', format='png')

def visualize_bidirectional_chain():
    """Create and visualize a reversible chain gadget."""
    g = Gadget(4, 1, reversible=True)  # 4 states, 1 location, reversible
    # Create a chain: 0 <-> 1 <-> 2 <-> 3
    # Note: add_transition will automatically add the reverse transitions
    g.add_transition(0, 0, 1, 0)
    g.add_transition(1, 0, 2, 0)
    g.add_transition(2, 0, 3, 0)
    
    g_closed = g.closure()
    visualize_gadget_circular(g, 'bidirectional_chain', format='png')
    visualize_gadget_circular(g_closed, 'bidirectional_chain_closed', format='png')

def visualize_multilocation_gadget():
    """Create and visualize a gadget with multiple locations."""
    g = Gadget(3, 2)  # 3 states, 2 locations
    # Location 0 transitions
    g.add_transition(0, 0, 1, 0)
    g.add_transition(1, 0, 2, 0)
    # Location 1 transitions
    g.add_transition(0, 1, 2, 1)
    
    g_closed = g.closure()
    visualize_gadget_circular(g, 'multilocation_gadget', format='png')
    visualize_gadget_circular(g_closed, 'multilocation_gadget_closed', format='png')

def visualize_complex_reversible():
    """Create and visualize a complex reversible gadget with multiple locations."""
    g = Gadget(4, 2, reversible=True)  # 4 states, 2 locations, reversible
    # Location 0 forms a cycle
    g.add_transition(0, 0, 1, 0)  # This will create 0 <-> 1
    g.add_transition(1, 0, 2, 0)  # This will create 1 <-> 2
    g.add_transition(2, 0, 0, 0)  # This will create 2 <-> 0
    
    # Location 1 connects different states
    g.add_transition(1, 1, 3, 1)  # This will create 1 <-> 3
    g.add_transition(3, 1, 0, 1)  # This will create 3 <-> 0
    
    g_closed = g.closure()
    visualize_gadget_circular(g, 'complex_reversible', format='png')
    visualize_gadget_circular(g_closed, 'complex_reversible_closed', format='png')

if __name__ == "__main__":
    visualize_cycle_gadget()
    visualize_bidirectional_chain()
    visualize_multilocation_gadget()
    visualize_complex_reversible()
