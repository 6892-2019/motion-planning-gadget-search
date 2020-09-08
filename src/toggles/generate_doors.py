#!/usr/bin/env python3

# This file isn't related to the other database-centered stuff.  It just
# generates gadgetdefs for Viglietta-style doors.

import itertools
import yaml
try:
    from yaml import CLoader as Loader, CSafeDumper as Dumper
except ImportError:
    from yaml import SafeLoader as Loader, SafeDumper as Dumper
from enum import Enum
from types import SimpleNamespace


# https://docs.python.org/3/library/enum.html#orderedenum
class OrderedEnum(Enum):
    def __ge__(self, other):
        if self.__class__ is other.__class__:
            return self.value >= other.value
        return NotImplemented
    def __gt__(self, other):
        if self.__class__ is other.__class__:
            return self.value > other.value
        return NotImplemented
    def __le__(self, other):
        if self.__class__ is other.__class__:
            return self.value <= other.value
        return NotImplemented
    def __lt__(self, other):
        if self.__class__ is other.__class__:
            return self.value < other.value
        return NotImplemented

class Location(OrderedEnum):
    OPEN = 0
    OPEN_IN = 1
    OPEN_OUT = 2
    TRAVERSE = 3
    TRAVERSE_IN = 4
    TRAVERSE_OUT = 5
    CLOSE = 6
    CLOSE_IN = 7
    CLOSE_OUT = 8

location_groups = (
    (Location.OPEN, (Location.OPEN_IN, Location.OPEN_OUT)),
    (Location.TRAVERSE, (Location.TRAVERSE_IN, Location.TRAVERSE_OUT)),
    (Location.CLOSE, (Location.CLOSE_IN, Location.CLOSE_OUT)),
)

# A gadget can be directed or undirected on each of its lines.  We then check
# all permutations of the appropriate ports (possible gadgets) and find the
# minimal gadget after circular rotations and reflections.  This is inefficient.

canonicalize_permutations_3 = (
    (0, 1, 2),
    (1, 2, 0),
    (2, 0, 1),
    (2, 1, 0),
    (0, 2, 1),
    (1, 0, 2),
)
canonicalize_permutations_4 = (
    (0, 1, 2, 3),
    (1, 2, 3, 0),
    (2, 3, 0, 1),
    (3, 0, 1, 2),
    (3, 2, 1, 0),
    (0, 3, 2, 1),
    (1, 0, 3, 2),
    (2, 1, 0, 3),
)
canonicalize_permutations_5 = (
    (0, 1, 2, 3, 4),
    (1, 2, 3, 4, 0),
    (2, 3, 4, 0, 1),
    (3, 4, 0, 1, 2),
    (4, 0, 1, 2, 3),
    (4, 3, 2, 1, 0),
    (0, 4, 3, 2, 1),
    (1, 0, 4, 3, 2),
    (2, 1, 0, 4, 3),
    (3, 2, 1, 0, 4),
)
canonicalize_permutations_6 = (
    (0, 1, 2, 3, 4, 5),
    (1, 2, 3, 4, 5, 0),
    (2, 3, 4, 5, 0, 1),
    (3, 4, 5, 0, 1, 2),
    (4, 5, 0, 1, 2, 3),
    (5, 0, 1, 2, 3, 4),
    (5, 4, 3, 2, 1, 0),
    (0, 5, 4, 3, 2, 1),
    (1, 0, 5, 4, 3, 2),
    (2, 1, 0, 5, 4, 3),
    (3, 2, 1, 0, 5, 4),
    (4, 3, 2, 1, 0, 5),
)
canonicalize_permutations = {
    3: canonicalize_permutations_3,
    4: canonicalize_permutations_4,
    5: canonicalize_permutations_5,
    6: canonicalize_permutations_6,
}

def canonicalize_options(gadget):
    options = []
    for p in canonicalize_permutations[len(gadget)]:
        g = list(gadget)
        for i in range(len(p)):
            g[i] = gadget[p[i]]
        options.append(tuple(g))
    return options


def edges_for_ports(ports, *, optional_open=False, optional_close=False, open_only_if_closed=False, close_only_if_open=False, delay=1):
    port_index = {}
    for i, p in enumerate(ports):
        port_index.setdefault(p, []).append(i)

    port_data = {}
    nonadjacent_count = 0
    directed_types = []
    for up, (dp1, dp2) in location_groups:
        if up in port_index:
            port_data[up] = SimpleNamespace()
            port_data[up].directed = False
            port_data[up].indices = port_index[up]
        elif dp1 in port_index:
            port_data[up] = SimpleNamespace()
            port_data[up].directed = True
            directed_types.append(up.name.lower())
            port_data[up].indices = port_index[dp1] + port_index[dp2]
        else: continue

        if len(port_data[up].indices) == 2:
            ii = port_data[up].indices[0]
            oi = port_data[up].indices[1]
            if ((ii + 1) % len(ports) != oi) and ((ii - 1) % len(ports) != oi):
                nonadjacent_count += 1

    name_str = 'door-'
    for p in (Location.OPEN, Location.TRAVERSE, Location.CLOSE):
        d = port_data.get(p)
        if not d: continue # traverse may not be present
        if (optional_open and p == Location.OPEN) or (optional_close and p == Location.CLOSE):
            name_str += 'o'
        if (open_only_if_closed and p == Location.OPEN) or (close_only_if_open and p == Location.CLOSE):
            name_str += 's' # "symmetric"
        if d.directed:
            name_str += 'd'
        name_str += str(d.indices[0])
        name_str += str(d.indices[1]) if len(d.indices) == 2 else 'x'
    if delay > 1:
        name_str += '-closedelay'+str(delay)

    uedges = []
    dedges = []

    # Without delay, 0 is the open state, 1 is the closed state.
    open_states = list(range(delay))
    closed_state = delay
    d = port_data[Location.OPEN]
    if len(d.indices) == 1:
        if open_only_if_closed:
            dedges.append([closed_state, d.indices[0], d.indices[0], open_states[0]])
            if optional_open: # lets you waste a move in multiplayer
                uedges.append([closed_state, d.indices[0], d.indices[0], closed_state])
        else:
            for state in open_states:
                (uedges if state == open_states[0] else dedges).append([state, d.indices[0], d.indices[0], open_states[0]])
            dedges.append([closed_state, d.indices[0], d.indices[0], open_states[0]])
            if optional_open:
                for state in open_states:
                    uedges.append([state, d.indices[0], d.indices[0], state])
                uedges.append([closed_state, d.indices[0], d.indices[0], closed_state])
    elif not d.directed:
        if open_only_if_closed:
            dedges.append([closed_state, d.indices[0], d.indices[1], open_states[0]])
            dedges.append([closed_state, d.indices[1], d.indices[0], open_states[0]])
            if optional_open:
                uedges.append([closed_state, d.indices[0], d.indices[1], closed_state])
        else:
            for state in open_states:
                dedges.append([state, d.indices[0], d.indices[1], open_states[0]])
                dedges.append([state, d.indices[1], d.indices[0], open_states[0]])
            dedges.append([closed_state, d.indices[0], d.indices[1], open_states[0]])
            dedges.append([closed_state, d.indices[1], d.indices[0], open_states[0]])
            if optional_open:
                for state in open_states:
                    uedges.append([state, d.indices[0], d.indices[1], state])
                uedges.append([closed_state, d.indices[0], d.indices[1], closed_state])
    else:
        if open_only_if_closed:
            dedges.append([closed_state, d.indices[0], d.indices[1], open_states[0]])
            if optional_open:
                dedges.append([closed_state, d.indices[0], d.indices[1], closed_state])
        else:
            for state in open_states:
                dedges.append([state, d.indices[0], d.indices[1], open_states[0]])
            dedges.append([closed_state, d.indices[0], d.indices[1], open_states[0]])
            if optional_open:
                for state in open_states:
                    dedges.append([state, d.indices[0], d.indices[1], state])
                dedges.append([closed_state, d.indices[0], d.indices[1], closed_state])

    d = port_data.get(Location.TRAVERSE)
    if d:
        if not d.directed:
            for state in open_states:
                uedges.append([state, d.indices[0], d.indices[1], state])
        else:
            for state in open_states:
                dedges.append([state, d.indices[0], d.indices[1], state])


    d = port_data[Location.CLOSE]
    if not d.directed:
        for state in open_states:
            dedges.append([state, d.indices[0], d.indices[1], state + 1])
            dedges.append([state, d.indices[1], d.indices[0], state + 1])
            if optional_close:
                uedges.append([state, d.indices[0], d.indices[1], state])
        if not close_only_if_open:
            uedges.append([closed_state, d.indices[0], d.indices[1], closed_state])
    else:
        for state in open_states:
            dedges.append([state, d.indices[0], d.indices[1], state + 1])
            if optional_close:
                dedges.append([state, d.indices[0], d.indices[1], state])
        if not close_only_if_open:
            dedges.append([closed_state, d.indices[0], d.indices[1], closed_state])

    if delay > 1:
        state_names = {x: 'open'+str(x) for x in range(delay)}
        state_names[closed_state] = 'closed'
    else:
        state_names = {0: 'open', 1: 'closed'}

    return {
        'name': name_str,
        'uedges': uedges, 'dedges': dedges,
        'state-names': state_names,
        'planar': nonadjacent_count < 2,
        'directed': directed_types,
        'optional-open': optional_open,
        'optional-close': optional_close,
        'open-only-if-closed': open_only_if_closed,
        'close-only-if-open': close_only_if_open,
        'close-delay': delay,
        'pragma': 'allow-pruning-named-states',
    }


def gadget_keyfunc(g):
    directedness_priority = {0: 0, 3: 1, 1: 2, 2: 3}
    return (
        g['optional-open'],
        g['close-only-if-open'],
        directedness_priority[len(g['directed'])],
        not g['planar'],
        g['name'],
    )


six_port_sets = set()
for open_ports in ((Location.OPEN, Location.OPEN), (Location.OPEN_IN, Location.OPEN_OUT)):
    for traverse_ports in ((Location.TRAVERSE, Location.TRAVERSE), (Location.TRAVERSE_IN, Location.TRAVERSE_OUT)):
        for close_ports in ((Location.CLOSE, Location.CLOSE), (Location.CLOSE_IN, Location.CLOSE_OUT)):
            ports = open_ports + traverse_ports + close_ports
            for perm in itertools.permutations(ports):
                six_port_sets.add(min(canonicalize_options(perm)))
five_port_sets = set()
for open_ports in ((Location.OPEN,),):
    for traverse_ports in ((Location.TRAVERSE, Location.TRAVERSE), (Location.TRAVERSE_IN, Location.TRAVERSE_OUT)):
        for close_ports in ((Location.CLOSE, Location.CLOSE), (Location.CLOSE_IN, Location.CLOSE_OUT)):
            ports = open_ports + traverse_ports + close_ports
            for perm in itertools.permutations(ports):
                five_port_sets.add(min(canonicalize_options(perm)))
four_port_sets = set()
for open_ports in ((Location.OPEN, Location.OPEN), (Location.OPEN_IN, Location.OPEN_OUT)):
    for close_ports in ((Location.CLOSE, Location.CLOSE), (Location.CLOSE_IN, Location.CLOSE_OUT)):
        ports = open_ports + close_ports
        for perm in itertools.permutations(ports):
            four_port_sets.add(min(canonicalize_options(perm)))
three_port_sets = set()
for open_ports in ((Location.OPEN,),):
    for close_ports in ((Location.CLOSE, Location.CLOSE), (Location.CLOSE_IN, Location.CLOSE_OUT)):
        ports = open_ports + close_ports
        for perm in itertools.permutations(ports):
            three_port_sets.add(min(canonicalize_options(perm)))

gadgets = []
# The gadget search operates under 1-player incentives, so optional open and
# close are only relevant under symmetry for that port.
for s in six_port_sets:
    gadgets.append(edges_for_ports(s))
    gadgets.append(edges_for_ports(s, open_only_if_closed=True))
    gadgets.append(edges_for_ports(s, open_only_if_closed=True, optional_open=True))
    gadgets.append(edges_for_ports(s, close_only_if_open=True))
    gadgets.append(edges_for_ports(s, close_only_if_open=True, optional_close=True))
    gadgets.append(edges_for_ports(s, open_only_if_closed=True, close_only_if_open=True))
    gadgets.append(edges_for_ports(s, open_only_if_closed=True, optional_open=True, close_only_if_open=True))
    gadgets.append(edges_for_ports(s, open_only_if_closed=True, close_only_if_open=True, optional_close=True))
    gadgets.append(edges_for_ports(s, open_only_if_closed=True, optional_open=True, close_only_if_open=True, optional_close=True))
for s in five_port_sets:
    gadgets.append(edges_for_ports(s))
    gadgets.append(edges_for_ports(s, close_only_if_open=True))
    gadgets.append(edges_for_ports(s, close_only_if_open=True, optional_close=True))
for s in four_port_sets:
    # gadgets.append(edges_for_ports(s, open_only_if_closed=True))
    # gadgets.append(edges_for_ports(s, open_only_if_closed=True, optional_open=True))
    for delay in range(1, 6):
        gadgets.append(edges_for_ports(s, close_only_if_open=True, delay=delay))
    # gadgets.append(edges_for_ports(s, close_only_if_open=True, optional_close=True))
    for delay in range(1, 6):
        gadgets.append(edges_for_ports(s, open_only_if_closed=True, close_only_if_open=True, delay=delay))
    gadgets.append(edges_for_ports(s, open_only_if_closed=True, optional_open=True, close_only_if_open=True))
    gadgets.append(edges_for_ports(s, open_only_if_closed=True, close_only_if_open=True, optional_close=True))
    gadgets.append(edges_for_ports(s, open_only_if_closed=True, optional_open=True, close_only_if_open=True, optional_close=True))
for s in three_port_sets:
    for delay in range(1, 6):
        gadgets.append(edges_for_ports(s, close_only_if_open=True, delay=delay))
    # gadgets.append(edges_for_ports(s, close_only_if_open=True, optional_close=True))


gadgets = sorted(gadgets, key=gadget_keyfunc)
gadget_subdoc = {}
for g in gadgets:
    gadget_subdoc[g['name']] = g
    del g['name']

alias_subdoc = {}
doc = {'gadgets': gadget_subdoc, 'aliases': alias_subdoc}

print(yaml.dump(doc, default_flow_style=None, Dumper=Dumper, sort_keys=False))
