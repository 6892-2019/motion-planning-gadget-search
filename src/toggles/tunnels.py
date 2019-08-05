#!/usr/bin/env python3

import functools
import itertools
import sys, yaml
from itertools import starmap
from operator import methodcaller
from enum import Enum
from types import SimpleNamespace
from typing import List, Set, Iterator, Iterable, Dict


@functools.total_ordering
class GadgetEdge:
    def __init__(self, state1, loc1, loc2, state2):
        self.state1 = state1
        self.loc1 = loc1
        self.loc2 = loc2
        self.state2 = state2
    def states(self):
        return tuple({self.state1, self.state2})
    def locations(self):
        return tuple({self.loc1, self.loc2})
    def reverse(self):
        return GadgetEdge(self.state2, self.loc2, self.loc1, self.state1)
    def renumber_locs(self, mapping):
        return GadgetEdge(self.state1, mapping[self.loc1], mapping[self.loc2], self.state2)
    def renumber_states(self, mapping):
        return GadgetEdge(mapping[self.state1], self.loc1, self.loc2, mapping[self.state2])
    def renumber(self, location_mapping, state_mapping):
        return GadgetEdge(state_mapping[self.state1], location_mapping[self.loc1], location_mapping[self.loc2], state_mapping[self.state2])
    def __str__(self):
        return '[{}, {}, {}, {}]'.format(self.state1, self.loc1, self.loc2, self.state2)
    def __repr__(self):
        return str(self)
    def __eq__(self, other):
        return ((self.state1, self.loc1, self.loc2, self.state2) ==
                (other.state1, other.loc1, other.loc2, other.state2))
    def __le__(self, other):
        return ((self.state1, self.loc1, self.loc2, self.state2) <
                (other.state1, other.loc1, other.loc2, other.state2))
    def __hash__(self):
        return hash((self.state1, self.loc1, self.loc2, self.state2))
    def prepare_yaml(self):
        return [self.state1, self.loc1, self.loc2, self.state2]

class Gadget:
    def __init__(self, edges: Iterable[GadgetEdge], state_size=None, state_names=None):
        self.edges: Set[GadgetEdge] = set(edges)
        empirical_state_size = max(max(e.states()) for e in self.edges) + 1
        if state_size and state_size < empirical_state_size:
            raise ValueError('bad explicit state size')
        self.state_size = state_size if state_size else empirical_state_size
        self.location_size = max(max(e.locations()) for e in self.edges) + 1
        if not state_names:
            state_names = {i: str(i) for i in range(self.state_size)}
        if sorted(state_names.keys()) != sorted(range(self.state_size)):
            raise ValueError('bad state names {} {}'.format(self.state_size, state_names))
        self.state_names = state_names
    @staticmethod
    def make(uedges, dedges, state_size=None, state_names=None):
        edges: Set[GadgetEdge] = set()
        for e in uedges:
            if not isinstance(e, GadgetEdge):
                e = GadgetEdge(*e)
            edges.add(e)
            edges.add(e.reverse())
        for e in dedges:
            if not isinstance(e, GadgetEdge):
                e = GadgetEdge(*e)
            edges.add(e)
        return Gadget(edges, state_size=state_size, state_names=state_names)
    def __eq__(self, other):
        return (self.location_size, self.state_size, self.edges) ==\
               (other.location_size, other.state_size, other.edges)
    def __hash__(self):
        return hash((self.location_size, self.state_size, frozenset(self.edges)))
    def __str__(self):
        return '[{}, {}, {}]'.format(self.location_size, self.state_size, self.edges)
    def __repr__(self):
        return str(self)
    def _renumber(self, renumber_fn):
        return Gadget(map(renumber_fn, self.edges), self.state_size)
    def renumber_locs(self, mapping):
        return self._renumber(lambda e: e.renumber_locs(mapping))
    def renumber_states(self, mapping):
        return self._renumber(lambda e: e.renumber_states(mapping))
    def renumber(self, location_mapping, state_mapping):
        return self._renumber(lambda e: e.renumber(location_mapping, state_mapping))
    # Returns an object that can be serialized in yaml.
    def prepare_yaml(self):
        uedges = set()
        dedges = set()
        for e in self.edges:
            if e.reverse() in dedges:
                dedges.remove(e.reverse())
                uedges.add(min(e, e.reverse()))
            else:
                dedges.add(e)
        yaml = {}
        if uedges:
            yaml['uedges'] = sorted(map(methodcaller('prepare_yaml'), uedges))
        if dedges:
            yaml['dedges'] = sorted(map(methodcaller('prepare_yaml'), dedges))
        yaml['state-names'] = self.state_names
        return yaml

class Connectivity(Enum):
    DISCONNECTED = 0 # aka nonflipping
    CONNECTED = 1 # aka "has components"
    STRONG_CONNECTED = 2

def determine_connectivity(g: Gadget):
    forward, backward = False, False
    for e in g.edges:
        if e.state1 == 0 and e.state2 == 1:
            forward = True
        elif e.state1 == 1 and e.state2 == 0:
            backward = True
    return (Connectivity.STRONG_CONNECTED if forward and backward else
            Connectivity.CONNECTED if forward or backward else
            Connectivity.DISCONNECTED)

class Rotatability(Enum):
    INVARIANT = 0
    FLIPS_STATE = 1 # rotation equivalent to flipping the state
    FULL = 2 # rotation not equivalent to states

def determine_rotatability(g: Gadget):
    flip_mapping = {0: 1, 1: 0}
    if g.renumber_locs(flip_mapping) == g:
        return Rotatability.INVARIANT
    if g.state_size > 1 and g.renumber(flip_mapping, flip_mapping) == g:
        return Rotatability.FLIPS_STATE
    return Rotatability.FULL

def determine_states_identical(g: Gadget):
    flip_mapping = {0: 1, 1: 0}
    return g.renumber_states(flip_mapping) == g

class StateFlip(Enum):
    NEITHER = 0
    SECOND = 1
    FIRST = 2

def do_state_flips(first: Gadget, second: Gadget) -> StateFlip:
    if first.states_identical or second.states_identical: return StateFlip.NEITHER
    if first.connectivity == Connectivity.DISCONNECTED and second.connectivity == Connectivity.DISCONNECTED: return StateFlip.NEITHER
    if first.rotatability == second.rotatability and first.rotatability == Rotatability.FLIPS_STATE: return StateFlip.NEITHER

    if first.rotatability == Rotatability.FLIPS_STATE and second.connectivity == Connectivity.DISCONNECTED: return StateFlip.SECOND
    if first.connectivity == Connectivity.DISCONNECTED and second.rotatability == Rotatability.FLIPS_STATE: return StateFlip.FIRST

    # TODO: possibly more checks?
    return StateFlip.SECOND

if __name__ == '__main__':
    tunnel_specs = yaml.safe_load(open(sys.argv[1], 'r'))
    tunnels: Dict[str, Gadget] = {k: Gadget.make(v.get('uedges', []), v.get('dedges', []), v.get('state-size'), v.get('state-names'))
            for k, v in tunnel_specs.items()}
    fallback_specs = yaml.safe_load(open(sys.argv[2], 'r'))
    fallback: Dict[str, Gadget] = {k: Gadget.make(v.get('uedges', []), v.get('dedges', []), v.get('state-size'), v.get('state-names'))
            for k, v in fallback_specs.items()}

    tunnel_blockers = {v: k for k, v in tunnels.items()}
    tunnel_blockers.update({v.renumber_states({0: 1, 1: 0}): k for k, v in tunnels.items()})
    tunnel_blockers.update({v.renumber_locs({0: 1, 1: 0}): k for k, v in tunnels.items()})
    tunnel_blockers.update({v.renumber({0: 1, 1: 0}, {0: 1, 1: 0}): k for k, v in tunnels.items()})
    for fname, fgadget in fallback.items():
        blocker = tunnel_blockers.get(fgadget)
        if blocker:
            print(blocker, 'blocked', fname, file=sys.stderr)
        else:
            tunnels[fname] = fgadget

    document = {'gadgets': dict(), 'aliases': dict()}
    for name, gadget in tunnels.items():
        # Wires and diodes are 1-state tunnels that work out just fine with
        # these rules if we copy their edges into the other state.
        if gadget.state_size == 1:
            copied_edges = set(map(lambda e: e.renumber_states({0: 1}), gadget.edges))
            gadget.edges.update(copied_edges)
            gadget.state_size = 2

        gadget.connectivity = determine_connectivity(gadget)
        gadget.rotatability = determine_rotatability(gadget)
        gadget.states_identical = determine_states_identical(gadget)

        if gadget.connectivity != Connectivity.DISCONNECTED:
            document['gadgets'][name] = gadget.prepare_yaml()
        else:
            print('skipped singleton', name, gadget, file=sys.stderr)

    identity_map = {0: 0, 1: 1}
    flip_map = {0: 1, 1: 0}
    three_loc_maps = (
        ('parallel', identity_map, {0: 3, 1: 2}),
        ('antiparallel', identity_map, {0: 2, 1: 3}),
        ('crossing', {0: 0, 1: 2}, {0: 3, 1: 1}),
    )
    two_loc_maps = (
        ('noncrossing', identity_map, {0: 3, 1: 2}),
        ('crossing', {0: 0, 1: 2}, {0: 3, 1: 1}),
    )
    state_maps = (
        ('matched', identity_map),
        ('mismatched', flip_map),
    )

    for (name1, gadget1), (name2, gadget2) in itertools.product(tunnels.items(), repeat=2):
        # If both gadgets are state-identical and disconnected, we can fold
        # away state 1, but otherwise process normally (we won't state-flip
        # later as not state-identical is already in do_state_flips).
        if gadget1.states_identical and gadget2.states_identical and \
                gadget1.connectivity == Connectivity.DISCONNECTED and \
                gadget2.connectivity == Connectivity.DISCONNECTED:
            # Gadget.renumber_states should really do this for us, but eh...
            oldnames1 = gadget1.state_names
            gadget1 = gadget1.renumber_states({0: 0, 1: 0})
            gadget1.state_names = {0: oldnames1[0]}
            gadget1.state_size = 1
            gadget1.connectivity = Connectivity.STRONG_CONNECTED
            gadget1.rotatability = determine_rotatability(gadget1)
            assert gadget1.rotatability != Rotatability.FLIPS_STATE
            gadget1.states_identical = True
            oldnames2 = gadget2.state_names
            gadget2 = gadget2.renumber_states({0: 0, 1: 0})
            gadget2.state_names = {0: oldnames2[0]}
            gadget2.state_size = 1
            gadget2.connectivity = Connectivity.STRONG_CONNECTED
            gadget2.rotatability = determine_rotatability(gadget2)
            assert gadget2.rotatability != Rotatability.FLIPS_STATE
            gadget2.states_identical = True
            print('folded', name1, name2, file=sys.stderr)
        # Otherwise if they're both disconnected, we skip (e.g., a lock-lock
        # is not interesting), and if they're both state-identical, we also
        # skip (a tripwire-diode is not interesting).
        elif (gadget1.connectivity == Connectivity.DISCONNECTED and
                gadget2.connectivity == Connectivity.DISCONNECTED) or \
                (gadget1.states_identical and gadget2.states_identical):
            print('skipping', name1, name2, file=sys.stderr)
            continue


        loc_maps = two_loc_maps if Rotatability.INVARIANT in (gadget1.rotatability, gadget2.rotatability) or ((gadget1.rotatability == Rotatability.FLIPS_STATE) != (gadget2.rotatability == Rotatability.FLIPS_STATE)) else three_loc_maps
        if loc_maps is two_loc_maps:
            print('twoloc', name1, name2, file=sys.stderr)

        desired_flip = do_state_flips(gadget1, gadget2)
        if desired_flip == StateFlip.SECOND:
            for superprefix, state_map in state_maps:
                for prefix, map1, map2 in loc_maps:
                    edges1 = gadget1.renumber_locs(map1).edges
                    edges2 = gadget2.renumber(map2, state_map).edges
                    state_names = {s: '{}-{}'.format(gadget1.state_names[s], gadget2.state_names[state_map[s]]) for s in range(max(gadget1.state_size, gadget2.state_size))}
                    gadget = Gadget(edges1 | edges2, max(gadget1.state_size, gadget2.state_size), state_names)
                    name = '{}-{}-{}-{}'.format(name1, name2, prefix, superprefix)
                    document['gadgets'][name] = gadget.prepare_yaml()
        elif desired_flip == StateFlip.FIRST:
            for superprefix, state_map in state_maps:
                for prefix, map1, map2 in loc_maps:
                    edges1 = gadget1.renumber(map1, state_map).edges
                    edges2 = gadget2.renumber_locs(map2).edges
                    state_names = {s: '{}-{}'.format(gadget1.state_names[state_map[s]], gadget2.state_names[s]) for s in range(max(gadget1.state_size, gadget2.state_size))}
                    gadget = Gadget(edges1 | edges2, max(gadget1.state_size, gadget2.state_size), state_names)
                    name = '{}-{}-{}-{}'.format(name1, name2, prefix, superprefix)
                    document['gadgets'][name] = gadget.prepare_yaml()
        else:
            for prefix, map1, map2 in loc_maps:
                edges1 = gadget1.renumber_locs(map1).edges
                edges2 = gadget2.renumber_locs(map2).edges
                state_names = {s: '{}-{}'.format(gadget1.state_names[s], gadget2.state_names[s]) for s in range(max(gadget1.state_size, gadget2.state_size))}
                gadget = Gadget(edges1 | edges2, max(gadget1.state_size, gadget2.state_size), state_names)
                name = '{}-{}-{}'.format(name1, name2, prefix)
                document['gadgets'][name] = gadget.prepare_yaml()



    # Force PyYAML to respect dict order.  https://stackoverflow.com/a/52621703/3614835
    yaml.add_representer(dict, lambda self, data: yaml.representer.SafeRepresenter.represent_dict(self, data.items()))
    print(yaml.dump(document, default_flow_style=None))