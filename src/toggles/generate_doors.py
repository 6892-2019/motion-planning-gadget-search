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

survivors = set()
for open_ports in ((Location.OPEN, Location.OPEN), (Location.OPEN_IN, Location.OPEN_OUT), (Location.OPEN,)):
    for traverse_ports in ((Location.TRAVERSE, Location.TRAVERSE), (Location.TRAVERSE_IN, Location.TRAVERSE_OUT)):
        for close_ports in ((Location.CLOSE, Location.CLOSE), (Location.CLOSE_IN, Location.CLOSE_OUT)):
            ports = open_ports + traverse_ports + close_ports
            for perm in itertools.permutations(ports):
                survivors.add(min(canonicalize_options(perm)))


def edges_for_ports(ports, optional_open=False):
    port_index = {}
    for i, p in enumerate(ports):
        port_index.setdefault(p, []).append(i)

    port_data = {}
    nonadjacent_count = 0
    directed_types = []
    for up, (dp1, dp2) in location_groups:
        port_data[up] = SimpleNamespace()
        if up in port_index:
            port_data[up].directed = False
            port_data[up].indices = port_index[up]
        else:
            port_data[up].directed = True
            directed_types.append(up.name.lower())
            port_data[up].indices = port_index[dp1] + port_index[dp2]
        if len(port_data[up].indices) == 2:
            ii = port_data[up].indices[0]
            oi = port_data[up].indices[1]
            if ((ii + 1) % len(ports) != oi) and ((ii - 1) % len(ports) != oi):
                nonadjacent_count += 1

    name_str = 'door-o' if optional_open else 'door-'
    for p in (Location.OPEN, Location.TRAVERSE, Location.CLOSE):
        d = port_data[p]
        if d.directed:
            name_str += 'd'
        name_str += str(d.indices[0])
        name_str += str(d.indices[1]) if len(d.indices) == 2 else 'x'

    uedges = []
    dedges = []

    # 0 is the open state, 1 is the closed state.
    d = port_data[Location.OPEN]
    if len(d.indices) == 1:
        uedges.append([0, d.indices[0], d.indices[0], 0])
        dedges.append([1, d.indices[0], d.indices[0], 0])
        if optional_open: # only matters in multiplayer: lets you waste a move
            uedges.append([1, d.indices[0], d.indices[0], 1])
    elif not d.directed:
        uedges.append([0, d.indices[0], d.indices[1], 0])
        dedges.append([1, d.indices[0], d.indices[1], 0])
        dedges.append([1, d.indices[1], d.indices[0], 0])
        if optional_open:
            uedges.append([1, d.indices[0], d.indices[1], 1])
    else:
        dedges.append([0, d.indices[0], d.indices[1], 0])
        dedges.append([1, d.indices[0], d.indices[1], 0])
        if optional_open:
            dedges.append([1, d.indices[0], d.indices[1], 1])

    d = port_data[Location.TRAVERSE]
    if not d.directed:
        uedges.append([0, d.indices[0], d.indices[1], 0])
    else:
        dedges.append([0, d.indices[0], d.indices[1], 0])

    d = port_data[Location.CLOSE]
    if not d.directed:
        dedges.append([0, d.indices[0], d.indices[1], 1])
        dedges.append([0, d.indices[1], d.indices[0], 1])
    else:
        dedges.append([0, d.indices[0], d.indices[1], 1])

    return {
        'name': name_str,
        'uedges': uedges, 'dedges': dedges,
        'planar': nonadjacent_count < 2, 'directed': directed_types,
        'optional-open': optional_open,
        'state-names': {0: 'open', 1: 'closed'},
    }


def gadget_keyfunc(g):
    directedness_priority = {0: 0, 3: 1, 1: 2, 2: 3}
    return (
        g['optional-open'],
        directedness_priority[len(g['directed'])],
        not g['planar'],
        g['name'],
    )


gadgets = []
for s in survivors:
    gadgets.append(edges_for_ports(s))
    # There's no point to optional open doors for the gadget search, because in
    # singleplayer there's no reason not to open a door.  Optional close doors
    # are similarly pointless.
    #gadgets.append(edges_for_ports(s, True))

gadgets = sorted(gadgets, key=gadget_keyfunc)
gadget_subdoc = {}
for g in gadgets:
    gadget_subdoc[g['name']] = g
    del g['name']

alias_subdoc = {}
doc = {'gadgets': gadget_subdoc, 'aliases': alias_subdoc}

print(yaml.dump(doc, default_flow_style=None, Dumper=Dumper, sort_keys=False))
