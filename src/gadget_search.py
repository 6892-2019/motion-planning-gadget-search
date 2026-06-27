#!/usr/bin/env python3

from typing import List, Set, Dict, Tuple, Optional
from gadget import Gadget
from collections import defaultdict, deque

def search_gadget_path(gadget: Gadget, start_state: int, start_loc: int, 
                      target_state: int, target_loc: int) -> Optional[List[Tuple[int, int]]]:
    """
    Search for a path in a gadget from a start state-location pair to a target state-location pair.
    Uses breadth-first search to find the shortest path.
    
    Args:
        gadget: The gadget to search in
        start_state: Starting state
        start_loc: Starting location
        target_state: Target state
        target_loc: Target location
        
    Returns:
        A list of (state, location) pairs representing the path from start to target,
        or None if no path exists.
    """
    # Validate input
    if not (0 <= start_state < gadget.num_states and 
            0 <= target_state < gadget.num_states and
            0 <= start_loc < gadget.num_locations and
            0 <= target_loc < gadget.num_locations):
        return None
        
    # Queue for BFS: (state, location)
    queue = deque([(start_state, start_loc)])
    
    # Keep track of visited state-location pairs and their predecessors
    visited = {(start_state, start_loc): None}  # (state, loc) -> predecessor (state, loc)
    
    while queue:
        current_state, current_loc = queue.popleft()
        
        # Check if we reached the target
        if current_state == target_state and current_loc == target_loc:
            # Reconstruct path
            path = []
            current = (current_state, current_loc)
            while current is not None:
                path.append(current)
                current = visited[current]
            return list(reversed(path))
            
        # Get all possible transitions from current state-location
        for from_state, from_loc, to_state, to_loc in gadget.transitions:
            # Only consider transitions from our current state-location
            if from_state == current_state and from_loc == current_loc:
                next_state, next_loc = to_state, to_loc
                if (next_state, next_loc) not in visited:
                    visited[(next_state, next_loc)] = (current_state, current_loc)
                    queue.append((next_state, next_loc))
                
    # No path found
    return None

def find_reachable_states(gadget: Gadget, start_state: int, start_loc: int) -> Set[Tuple[int, int]]:
    """
    Find all reachable state-location pairs from a given starting point.
    Uses breadth-first search for exploration.
    
    Args:
        gadget: The gadget to search in
        start_state: Starting state
        start_loc: Starting location
        
    Returns:
        A set of (state, location) pairs that are reachable from the start point
    """
    # Validate input
    if not (0 <= start_state < gadget.num_states and 
            0 <= start_loc < gadget.num_locations):
        return set()
        
    # Queue for BFS: (state, location)
    queue = deque([(start_state, start_loc)])
    visited = {(start_state, start_loc)}
    
    while queue:
        current_state, current_loc = queue.popleft()
        
        # Get all possible transitions from current state-location
        for from_state, from_loc, to_state, to_loc in gadget.transitions:
            # Only consider transitions from our current state-location
            if from_state == current_state and from_loc == current_loc:
                next_state, next_loc = to_state, to_loc
                if (next_state, next_loc) not in visited:
                    visited.add((next_state, next_loc))
                    queue.append((next_state, next_loc))
                
    return visited

def analyze_gadget_connectivity(gadget: Gadget) -> Dict[Tuple[int, int], Set[Tuple[int, int]]]:
    """
    Analyze the connectivity of a gadget by finding all reachable states from each state-location pair.
    
    Args:
        gadget: The gadget to analyze
        
    Returns:
        A dictionary mapping each state-location pair to a set of reachable state-location pairs
    """
    connectivity = {}
    
    # For each state-location pair
    for state in range(gadget.num_states):
        for loc in range(gadget.num_locations):
            # Find all reachable states from this point
            reachable = find_reachable_states(gadget, state, loc)
            connectivity[(state, loc)] = reachable
            
    return connectivity
