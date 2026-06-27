#!/usr/bin/env python3

from typing import Dict, List, Set, Tuple, Optional
from dataclasses import dataclass
from collections import defaultdict
import sys

@dataclass
class Gadget:
    """
    A class representing a motion planning gadget with states and transitions.
    A gadget consists of states and locations, with transitions between state-location pairs.
    Transitions can be either directed or undirected (reversible).
    """
    def __init__(self, num_states: int, num_locations: int, reversible: bool = False):
        """
        Initialize a gadget with given number of states and locations.
        Args:
            num_states: Number of states in the gadget
            num_locations: Number of locations in the gadget
            reversible: If True, all transitions are bidirectional
        """
        self.num_states = num_states
        self.num_locations = num_locations
        self.reversible = reversible
        # List of transitions: (from_state, from_loc, to_state, to_loc)
        self.transitions: List[Tuple[int, int, int, int]] = []
        
    def add_transition(self, from_state: int, from_loc: int, to_state: int, to_loc: int) -> bool:
        """
        Add a transition to the gadget.
        Returns False if the transition is invalid (out of bounds or self-loop).
        """
        # Validate state and location bounds
        if not (0 <= from_state < self.num_states and 
                0 <= to_state < self.num_states and
                0 <= from_loc < self.num_locations and
                0 <= to_loc < self.num_locations):
            return False
            
        # No self-loops allowed (same location)
        if from_loc == to_loc:
            return False
            
        transition = (from_state, from_loc, to_state, to_loc)
        
        # Check if transition already exists
        if transition not in self.transitions:
            self.transitions.append(transition)
            # If gadget is reversible, add reverse transition
            if self.reversible:
                reverse_transition = (to_state, to_loc, from_state, from_loc)
                if reverse_transition not in self.transitions:
                    self.transitions.append(reverse_transition)
            
        return True
        
    def get_transitions_from(self, state: int, location: int) -> List[Tuple[int, int]]:
        """Get all possible transitions from a given state-location pair."""
        results = []
        for t in self.transitions:
            # Check forward transitions
            if t[0] == state and t[1] == location:
                results.append((t[2], t[3]))
        return results
        
    def is_deterministic(self) -> bool:
        """
        Check if the gadget is deterministic.
        A gadget is deterministic if each state-location pair has at most two outgoing transitions.
        """
        transition_counts = defaultdict(int)
        
        for from_state, from_loc, _, _ in self.transitions:
            transition_counts[(from_state, from_loc)] += 1
            
        # Check if any state-location pair has more than 2 transitions
        return all(count <= 2 for count in transition_counts.values())
        
    def is_equivalent_to(self, other: 'Gadget') -> bool:
        """
        Check if two gadgets are equivalent under state relabeling, holding
        location indices fixed.

        Use this when locations have a fixed physical identity (e.g. they are
        ordered by position along a corridor, or planarity constraints prevent
        relabeling).  If locations are interchangeable, use is_isomorphic_to().
        """
        if (self.num_states != other.num_states or
                self.num_locations != other.num_locations or
                len(self.transitions) != len(other.transitions) or
                self.reversible != other.reversible):
            return False

        other_set = set(other.transitions)
        for state_map in self._generate_state_mappings():
            if self._check_mapping_equivalence(other_set, state_map, None):
                return True
        return False

    def is_isomorphic_to(self, other: 'Gadget') -> bool:
        """
        Check if two gadgets are isomorphic under joint state AND location
        relabeling.

        Use this when locations have no intrinsic ordering or planarity
        constraint — i.e., we only care whether the abstract traversal-mode
        structure matches under some port correspondence.  This is O(n! * m!)
        in the number of states/locations, so it is only practical for small
        gadgets (n, m ≤ ~6).
        """
        from itertools import permutations

        if (self.num_states != other.num_states or
                self.num_locations != other.num_locations or
                len(self.transitions) != len(other.transitions) or
                self.reversible != other.reversible):
            return False

        other_set = set(other.transitions)
        locs = range(self.num_locations)
        for state_map in self._generate_state_mappings():
            for loc_perm in permutations(locs):
                loc_map = {original: mapped for original, mapped in zip(locs, loc_perm)}
                if self._check_mapping_equivalence(other_set, state_map, loc_map):
                    return True
        return False

    def _generate_state_mappings(self) -> List[Dict[int, int]]:
        """Generate all permutations of state indices."""
        from itertools import permutations
        states = range(self.num_states)
        return [{orig: mapped for orig, mapped in zip(states, p)}
                for p in permutations(states)]

    def _check_mapping_equivalence(self, other_transitions: set,
                                   state_map: Dict[int, int],
                                   loc_map: Optional[Dict[int, int]]) -> bool:
        """Check equivalence under the given state (and optional location) mapping."""
        mapped = set()
        for fs, fl, ts, tl in self.transitions:
            mapped.add((
                state_map[fs],
                loc_map[fl] if loc_map else fl,
                state_map[ts],
                loc_map[tl] if loc_map else tl,
            ))
        return mapped == other_transitions

    def closure(self) -> 'Gadget':
        """
        Compute the transitive closure of the state-location graph.
        Returns a new gadget with all possible "shortcut" transitions added.
        A shortcut transition is added from state A to state B through location L
        if B is reachable from A through a sequence of moves all using location L.
        """
        # Create a new gadget with same properties
        result = Gadget(self.num_states, self.num_locations, self.reversible)
        result.transitions = self.transitions.copy()
        
        # Keep adding transitions until no new ones can be added
        while True:
            num_transitions = len(result.transitions)
            
            # For each location
            for loc in range(self.num_locations):
                # Get all transitions involving this location
                loc_transitions = []
                for s1, l1, s2, l2 in result.transitions:
                    if l1 == loc:
                        loc_transitions.append((s1, s2, l2))
                    # For reversible gadgets, consider transitions in both directions
                    if result.reversible and l2 == loc:
                        loc_transitions.append((s2, s1, l1))
                
                # For each pair of transitions that can be chained
                for s1, s2, l2 in loc_transitions:
                    for s3, s4, l4 in loc_transitions:
                        if s2 == s3:  # Can chain these transitions
                            # Add the shortcut transition if it doesn't exist
                            new_transition = (s1, loc, s4, l4)
                            if new_transition not in result.transitions:
                                result.transitions.append(new_transition)
                                # If reversible, add the reverse shortcut.
                                # Reverse of (s1, loc, s4, l4): enter l4 in state s4, exit loc in state s1.
                                if result.reversible:
                                    reverse = (s4, l4, s1, loc)
                                    if reverse not in result.transitions:
                                        result.transitions.append(reverse)
            
            # If no new transitions were added, we're done
            if len(result.transitions) == num_transitions:
                break

        return result

    def trace(self, initial_state: int, moves: List[Tuple[int, int]],
              state_labels: Optional[Dict[int, str]] = None,
              loc_labels: Optional[Dict[int, str]] = None) -> bool:
        """
        Trace a robot's traversal through the gadget, printing each step.

        Args:
            initial_state: Starting state of the gadget
            moves: Sequence of (entry_loc, exit_loc) pairs representing the robot's path
            state_labels: Optional display names for states, e.g. {0: 'open', 1: 'closed'}
            loc_labels: Optional display names for locations, e.g. {0: 'north', 1: 'south'}

        Returns:
            True if all moves completed successfully, False if the trace failed
        """
        def fmt_state(s: int) -> str:
            if state_labels and s in state_labels:
                return state_labels[s]
            return f"state {s}"

        def fmt_loc(l: int) -> str:
            if loc_labels and l in loc_labels:
                return loc_labels[l]
            return f"loc {l}"

        state = initial_state
        print(f"Trace start: {fmt_state(state)}")

        for step, (entry_loc, exit_loc) in enumerate(moves):
            matching = [
                t for t in self.transitions
                if t[0] == state and t[1] == entry_loc and t[3] == exit_loc
            ]

            prefix = f"  Move {step + 1}: enter {fmt_loc(entry_loc)}, exit {fmt_loc(exit_loc)}"

            if not matching:
                print(f"{prefix}  ->  FAILED (no valid transition from {fmt_state(state)})")
                available = [(t[3], t[2]) for t in self.transitions
                             if t[0] == state and t[1] == entry_loc]
                if available:
                    print(f"    Available exits from {fmt_loc(entry_loc)} in {fmt_state(state)}:")
                    for ex_loc, ex_state in available:
                        print(f"      exit {fmt_loc(ex_loc)} -> {fmt_state(ex_state)}")
                else:
                    print(f"    No transitions out of {fmt_loc(entry_loc)} in {fmt_state(state)}")
                return False

            new_state = matching[0][2]
            suffix = f" (ambiguous: {len(matching)} transitions, using first)" if len(matching) > 1 else ""
            print(f"{prefix}  ->  {fmt_state(state)} -> {fmt_state(new_state)}{suffix}")
            state = new_state

        print(f"Trace end: {fmt_state(state)}")
        return True
