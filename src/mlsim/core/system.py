"""Construction representation (docs/ml/01 §2).

A construction over a block set is:
  - ``instances``: a tuple of block-type ids (a multiset of instances);
  - ``init``: each instance's initial state (default all 0);
  - ``wire``: a partition of the global ports into components (junctions),
    stored as a restricted-growth string ``wire[p] = component_id``;
  - ``iface``: injective map target-port index ``x`` -> component id,
    ``iface[x]`` = the component exposed as external interface port ``x``.

Global ports are ordered by (instance, local location); ``gport(i, a)`` gives the
global index. The induced gadget depends on the wiring only through this partition
and on the interface only through which component each target-port attaches to
(docs/ml/01 §2), so this is the canonical construction object.
"""
from typing import List, Sequence, Tuple

from gadget import Gadget


class BlockSet:
    """An ordered set of gadget block types; blocks are referred to by index."""

    def __init__(self, gadgets: Sequence[Gadget]):
        self.gadgets: Tuple[Gadget, ...] = tuple(gadgets)

    def __len__(self) -> int:
        return len(self.gadgets)

    def __getitem__(self, i: int) -> Gadget:
        return self.gadgets[i]

    def num_locations(self, t: int) -> int:
        return self.gadgets[t].num_locations

    def num_states(self, t: int) -> int:
        return self.gadgets[t].num_states


class Construction:
    """A network of block instances wired at their ports (see module docstring)."""

    def __init__(self, blockset: BlockSet, instances: Sequence[int],
                 wire: Sequence[int], iface: Sequence[int],
                 init: Sequence[int] = None):
        self.blockset = blockset
        self.instances: Tuple[int, ...] = tuple(instances)

        # global port layout
        self._offset: List[int] = []
        self.port_of: List[Tuple[int, int]] = []   # global port -> (instance, local loc)
        off = 0
        for i, t in enumerate(self.instances):
            self._offset.append(off)
            for a in range(blockset.num_locations(t)):
                self.port_of.append((i, a))
            off += blockset.num_locations(t)
        self.num_ports = off

        self.wire: Tuple[int, ...] = tuple(wire)
        self.iface: Tuple[int, ...] = tuple(iface)
        if init is None:
            init = [0] * len(self.instances)
        self.init: Tuple[int, ...] = tuple(init)
        self._validate()

    # -- helpers -------------------------------------------------------------
    def gport(self, i: int, a: int) -> int:
        return self._offset[i] + a

    def components(self) -> List[int]:
        return sorted(set(self.wire))

    @property
    def k(self) -> int:
        return len(self.iface)

    # -- validation ----------------------------------------------------------
    def _validate(self):
        n = len(self.instances)
        if len(self.wire) != self.num_ports:
            raise ValueError(f"wire has {len(self.wire)} entries, expected {self.num_ports}")
        if len(self.init) != n:
            raise ValueError("init length must match number of instances")
        for i, t in enumerate(self.instances):
            if not (0 <= self.init[i] < self.blockset.num_states(t)):
                raise ValueError(f"init state {self.init[i]} out of range for instance {i}")
        comps = set(self.wire)
        for c in self.iface:
            if c not in comps:
                raise ValueError(f"interface references empty component {c}")
        if len(set(self.iface)) != len(self.iface):
            raise ValueError("interface components must be distinct (injective)")

    def __repr__(self) -> str:
        return (f"Construction(instances={self.instances}, wire={self.wire}, "
                f"iface={self.iface}, init={self.init})")
