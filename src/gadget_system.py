#!/usr/bin/env python3
"""
gadget_system.py — explicit simulation *constructions* and their verification.

A `GadgetSystem` is a network of gadget *instances* (named copies of gadget
types) wired together at their locations (ports), with some ports `expose`d as
the construction's external interface.  This is the object a simulation
construction actually is: "wire these gadgets up like so, and the result
behaves like the target gadget".

From a system we can compute its INDUCED GADGET — the gadget the construction
implements as seen from the external ports — by exploring the configuration
graph (the agent walks through internal gadgets and wires, the joint state of
all instances evolving).  The induced gadget is then checked against a target
with `behaviorally_equivalent` (gadget_simulation.py), which is the authoritative
equality test.  Because the induced gadget is computed honestly — it records
*every* reachable external-to-external traversal, including unwanted ones — a
PASS is a real one-player simulation (the "if and only if" of arXiv:2005.03192),
not a one-directional containment.

For each allowed traversal we can also reconstruct a concrete TRACE: the
step-by-step path the agent takes through the network.  See `gadget_viz.py` for
diagrams of a system and of traces.
"""

from typing import Dict, List, Optional, Tuple, Set
from collections import deque, defaultdict
from gadget import Gadget
from gadget_simulation import behaviorally_equivalent, is_reversible, is_dag

Port = Tuple[str, int]       # (instance_name, local_location)
Config = Tuple[int, ...]     # joint state of all instances, in instance order


class GadgetSystem:
    """A network of gadget instances connected at their ports."""

    def __init__(self) -> None:
        self.instance_names: List[str] = []          # insertion order
        self.types: Dict[str, Gadget] = {}
        self.initial_state: Dict[str, int] = {}
        self.connections: List[Tuple[Port, Port]] = []
        self.external: Dict[str, Port] = {}          # label -> internal port
        self.external_labels: List[str] = []         # insertion order

    # ── construction ─────────────────────────────────────────────────────────

    def add_instance(self, name: str, gadget: Gadget,
                     initial_state: int = 0) -> 'GadgetSystem':
        if name in self.types:
            raise ValueError(f"instance {name!r} already exists")
        if not (0 <= initial_state < gadget.num_states):
            raise ValueError(f"initial_state {initial_state} out of range for {name!r}")
        self.instance_names.append(name)
        self.types[name] = gadget
        self.initial_state[name] = initial_state
        return self

    def connect(self, a: Port, b: Port) -> 'GadgetSystem':
        """Wire two ports together (an undirected corridor)."""
        self._check_port(a)
        self._check_port(b)
        self.connections.append((a, b))
        return self

    def expose(self, label: str, port: Port) -> 'GadgetSystem':
        """Expose an internal port as an external interface location."""
        self._check_port(port)
        if label in self.external:
            raise ValueError(f"external label {label!r} already used")
        if port in self.external.values():
            raise ValueError(f"port {port} already exposed under another label")
        self.external[label] = port
        self.external_labels.append(label)
        return self

    def _check_port(self, port: Port) -> None:
        name, loc = port
        if name not in self.types:
            raise ValueError(f"unknown instance {name!r}")
        if not (0 <= loc < self.types[name].num_locations):
            raise ValueError(f"location {loc} out of range for instance {name!r}")

    # ── helpers ──────────────────────────────────────────────────────────────

    def _instance_index(self, name: str) -> int:
        return self.instance_names.index(name)

    def initial_config(self) -> Config:
        return tuple(self.initial_state[n] for n in self.instance_names)

    def _wire_components(self) -> Dict[Port, Set[Port]]:
        """Map each wired port to the set of ports in its connection component."""
        adj: Dict[Port, Set[Port]] = defaultdict(set)
        for a, b in self.connections:
            adj[a].add(b)
            adj[b].add(a)
        comp: Dict[Port, Set[Port]] = {}
        seen: Set[Port] = set()
        for start in list(adj):
            if start in seen:
                continue
            group: Set[Port] = set()
            stack = [start]
            while stack:
                p = stack.pop()
                if p in seen:
                    continue
                seen.add(p)
                group.add(p)
                stack.extend(adj[p] - seen)
            for p in group:
                comp[p] = group
        return comp

    def fmt_config(self, cfg: Config) -> str:
        return "(" + ", ".join(f"{n}={cfg[i]}"
                               for i, n in enumerate(self.instance_names)) + ")"

    # ── exploration: the agent walking inside the construction ────────────────

    def _explore(self, config: Config, entry_label: str):
        """
        BFS over (config, port) reachable by entering at `entry_label`.

        Returns (results, parent, start) where results is a list of
        (end_config, exit_label, end_node), parent maps each node to
        (prev_node, move) for path reconstruction, and start is the entry node.
        A move is one of:
            ('traverse', inst, from_loc, to_loc, from_state, to_state)
            ('wire',     from_port, to_port)
        An exit is recorded whenever the agent stands on an exposed port after
        at least one move.
        """
        comp = self._wire_components()
        ext_by_port = {p: l for l, p in self.external.items()}
        start_port = self.external[entry_label]
        start = (config, start_port)

        parent: Dict[Tuple[Config, Port], Optional[Tuple]] = {start: None}
        moved: Dict[Tuple[Config, Port], bool] = {start: False}
        results: List[Tuple[Config, str, Tuple[Config, Port]]] = []

        queue = deque([start])
        while queue:
            node = queue.popleft()
            cfg, port = node

            label = ext_by_port.get(port)
            if label is not None and moved[node]:
                results.append((cfg, label, node))

            name, loc = port
            i = self._instance_index(name)
            state = cfg[i]
            gadget = self.types[name]

            # Traverse the gadget at this port.
            for fs, fl, ts, tl in gadget.transitions:
                if fs == state and fl == loc:
                    new_cfg = cfg[:i] + (ts,) + cfg[i + 1:]
                    nxt = (new_cfg, (name, tl))
                    if nxt not in parent:
                        parent[nxt] = (node, ('traverse', name, loc, tl, state, ts))
                        moved[nxt] = True
                        queue.append(nxt)

            # Walk along wires (no state change).
            for q in comp.get(port, ()):  # ports in the same wire component
                if q == port:
                    continue
                nxt = (cfg, q)
                if nxt not in parent:
                    parent[nxt] = (node, ('wire', port, q))
                    moved[nxt] = True
                    queue.append(nxt)

        return results, parent, start

    # ── induced gadget ────────────────────────────────────────────────────────

    def induced_gadget(self):
        """
        Compute the gadget this construction implements, as seen from the
        external ports.

        Returns (induced, configs, labels, defects):
          induced  : a Gadget whose states are the reachable configurations
                     (in discovery order) and whose locations are the external
                     ports (in `external_labels` order).
          configs  : list mapping induced state index -> configuration tuple.
          labels   : the external-port labels, in induced location order.
          defects  : list of (config, label, end_config) for *same-port*
                     traversals that change the configuration.  These are
                     behaviors the construction exhibits that no ordinary
                     gadget can represent (entrance == exit), so they make the
                     construction an unfaithful simulation of any target.
        """
        labels = list(self.external_labels)
        loc_of = {l: i for i, l in enumerate(labels)}

        c0 = self.initial_config()
        config_index: Dict[Config, int] = {c0: 0}
        configs: List[Config] = [c0]
        transitions: List[Tuple[int, int, int, int]] = []
        defects: List[Tuple[Config, str, Config]] = []

        work = deque([c0])
        while work:
            c = work.popleft()
            for x in labels:
                results, _, _ = self._explore(c, x)
                for end_config, y, _node in results:
                    if y == x:
                        if end_config != c:
                            defects.append((c, x, end_config))
                        continue
                    if end_config not in config_index:
                        config_index[end_config] = len(configs)
                        configs.append(end_config)
                        work.append(end_config)
                    transitions.append((config_index[c], loc_of[x],
                                        config_index[end_config], loc_of[y]))

        induced = Gadget(len(configs), len(labels))
        seen: Set[Tuple[int, int, int, int]] = set()
        for t in transitions:
            if t not in seen:
                seen.add(t)
                induced.transitions.append(t)
        return induced, configs, labels, defects

    # ── verification ──────────────────────────────────────────────────────────

    def verify_simulates(
        self,
        target: Gadget,
        label_to_target_loc: Optional[Dict[str, int]] = None,
        verbose: bool = True,
    ) -> bool:
        """
        Check that this construction simulates `target`.

        If `label_to_target_loc` is given (external label -> target location
        index), the external interface is matched to the target's locations
        exactly (fixed ports); otherwise all location relabelings are tried.

        Returns True iff the induced gadget is behaviorally equivalent to the
        target and the construction has no same-port-with-state-change defects.
        """
        induced, configs, labels, defects = self.induced_gadget()

        if label_to_target_loc is not None:
            loc_map = {i: label_to_target_loc[labels[i]] for i in range(len(labels))}
            relabeled = Gadget(induced.num_states, max(target.num_locations,
                                                       induced.num_locations))
            relabeled.transitions = [(fs, loc_map[fl], ts, loc_map[tl])
                                     for fs, fl, ts, tl in induced.transitions]
            relabeled.num_locations = target.num_locations
            equiv = (induced.num_locations == target.num_locations and
                     behaviorally_equivalent(relabeled, target, fixed_locations=True))
        else:
            equiv = behaviorally_equivalent(induced, target)

        ok = equiv and not defects

        if verbose:
            print(f"Construction: {len(self.instance_names)} instance(s), "
                  f"{len(self.connections)} wire(s), external = {labels}")
            print(f"Induced gadget: {induced.num_states} reachable config(s), "
                  f"{len(induced.transitions)} traversal mode(s).")
            print(f"  induced reversible? {is_reversible(induced)}   "
                  f"target reversible? {is_reversible(target)}")
            print(f"  induced DAG?        {is_dag(induced)}   "
                  f"target DAG?        {is_dag(target)}")
            if defects:
                print(f"  DEFECT: {len(defects)} same-port traversal(s) that change "
                      f"state (extra behavior no gadget can represent):")
                for c, x, ce in defects:
                    print(f"    enter {x} in {self.fmt_config(c)} can exit {x} "
                          f"in {self.fmt_config(ce)}")
            print(f"Simulates target: {ok}"
                  + ("" if ok else "  (induced is NOT equivalent to target)"
                     if not equiv else "  (defects present)"))
        return ok

    # ── traces ────────────────────────────────────────────────────────────────

    def _reconstruct(self, parent, end_node) -> List[Tuple]:
        moves: List[Tuple] = []
        node = end_node
        while parent[node] is not None:
            prev, move = parent[node]
            moves.append(move)
            node = prev
        moves.reverse()
        return moves

    def trace(self, config: Config, entry_label: str, exit_label: str,
              end_config: Optional[Config] = None) -> Optional[List[Tuple]]:
        """
        Return one concrete agent path (list of moves) for the traversal that
        enters at `entry_label` in `config` and exits at `exit_label`.  If
        `end_config` is given, require that ending configuration.  Returns None
        if no such traversal exists.
        """
        results, parent, _ = self._explore(config, entry_label)
        for ec, y, node in results:
            if y == exit_label and (end_config is None or ec == end_config):
                return self._reconstruct(parent, node)
        return None

    def format_trace(self, config: Config, entry_label: str,
                     moves: List[Tuple]) -> str:
        """Render a move list (from `trace`) as a readable step-by-step block."""
        lines: List[str] = []
        port = self.external[entry_label]
        cfg = config
        lines.append(f"  enter external port {entry_label} "
                     f"(= {port[0]}.{port[1]});  config {self.fmt_config(cfg)}")
        for step, move in enumerate(moves, 1):
            if move[0] == 'traverse':
                _, name, fl, tl, fs, ts = move
                i = self._instance_index(name)
                cfg = cfg[:i] + (ts,) + cfg[i + 1:]
                lines.append(f"  {step}. traverse {name}: enter loc {fl} → exit loc {tl}"
                             f"   (state {fs}→{ts});  config {self.fmt_config(cfg)}")
                port = (name, tl)
            else:  # wire
                _, p, q = move
                lines.append(f"  {step}. walk wire {p[0]}.{p[1]} → {q[0]}.{q[1]}")
                port = q
        lines.append(f"  exit external port "
                     f"{next(l for l, pr in self.external.items() if pr == port)} "
                     f"(= {port[0]}.{port[1]});  config {self.fmt_config(cfg)}")
        return "\n".join(lines)

    def print_all_traces(self) -> None:
        """
        Print one trace for every allowed external-to-external traversal of the
        induced gadget, grouped by configuration.
        """
        induced, configs, labels, _ = self.induced_gadget()
        for sidx, cfg in enumerate(configs):
            print(f"From config {self.fmt_config(cfg)} "
                  f"(induced state {sidx}):")
            any_mode = False
            for x in labels:
                results, parent, _ = self._explore(cfg, x)
                # one trace per distinct (exit_label, end_config) with exit != entry
                seen: Set[Tuple[str, Config]] = set()
                for ec, y, node in results:
                    if y == x or (y, ec) in seen:
                        continue
                    seen.add((y, ec))
                    any_mode = True
                    print(f"  ── traversal {x} → {y} ──")
                    print(self.format_trace(cfg, x, self._reconstruct(parent, node)))
            if not any_mode:
                print("  (no traversals; dead configuration)")
            print()
