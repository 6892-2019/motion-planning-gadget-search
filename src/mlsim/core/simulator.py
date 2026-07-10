"""The induced-gadget simulator and verifier (docs/ml/02).

Independent re-implementation of the reference ``gadget_system.GadgetSystem`` over
the partition representation, so the differential test (``test_mlsim``) cross-checks
two independently written simulators rather than one calling the other.
"""
from collections import defaultdict, deque
from typing import List, Optional, Tuple

from gadget import Gadget
from .gadget import canon, canon_fixed
from .system import Construction

Defect = Tuple[tuple, int, tuple]   # (config, target-port x, end config)


def induced(c: Construction) -> Tuple[Gadget, List[Defect]]:
    """Compute the gadget this construction induces (states = reachable configs;
    locations = target-port indices), plus same-port-with-state-change defects."""
    bs = c.blockset
    types = [bs[t] for t in c.instances]

    # per-instance transition map: (state, loc) -> list[(exit_loc, to_state)]
    tmap = []
    for g in types:
        d = defaultdict(list)
        for fs, fl, ts, tl in g.transitions:
            d[(fs, fl)].append((tl, ts))
        tmap.append(d)

    comp_ports = defaultdict(list)
    for p in range(c.num_ports):
        comp_ports[c.wire[p]].append(p)
    comp_ext = {}                      # component -> target-port x
    for x, comp in enumerate(c.iface):
        comp_ext[comp] = x
    k = c.k
    port_of = c.port_of

    c0 = c.init
    config_index = {c0: 0}
    configs = [c0]
    work = deque([c0])
    trans = set()
    defects: List[Defect] = []

    while work:
        cfg = work.popleft()
        for x in range(k):
            xcomp = c.iface[x]
            start = [(cfg, p) for p in comp_ports[xcomp]]
            seen = set(start)
            moved = {s: False for s in start}
            q = deque(start)
            while q:
                node = q.popleft()
                ccfg, p = node
                i, a = port_of[p]
                pc = c.wire[p]

                if moved[node] and pc in comp_ext:
                    y = comp_ext[pc]
                    if y == x:
                        if ccfg != cfg:
                            defects.append((cfg, x, ccfg))
                    else:
                        if ccfg not in config_index:
                            config_index[ccfg] = len(configs)
                            configs.append(ccfg)
                            work.append(ccfg)
                        trans.add((config_index[cfg], x, config_index[ccfg], y))

                # traverse the gadget at this port
                st = ccfg[i]
                for (tl, ts) in tmap[i].get((st, a), ()):
                    ncfg = ccfg[:i] + (ts,) + ccfg[i + 1:]
                    nxt = (ncfg, c.gport(i, tl))
                    if nxt not in seen:
                        seen.add(nxt)
                        moved[nxt] = True
                        q.append(nxt)

                # walk within the wire component
                for pp in comp_ports[pc]:
                    if pp == p:
                        continue
                    nxt = (ccfg, pp)
                    if nxt not in seen:
                        seen.add(nxt)
                        moved[nxt] = True
                        q.append(nxt)

    g_ind = Gadget(len(configs), k)
    for t in sorted(trans):
        g_ind.transitions.append(t)
    return g_ind, defects


def verify(c: Construction, target: Gadget, fixed_interface: bool = True) -> bool:
    """True iff the construction simulates ``target``: induced gadget behaviourally
    equal to ``target`` and no defects.

    ``fixed_interface=True`` requires the construction's target-port indices to match
    ``target``'s locations exactly (``canon_fixed``); ``False`` allows any port
    relabeling (``canon``).
    """
    g_ind, defects = induced(c)
    if defects:
        return False
    if g_ind.num_locations != target.num_locations:
        return False
    if fixed_interface:
        return canon_fixed(g_ind) == canon_fixed(target)
    return canon(g_ind) == canon(target)


def to_gadget_system(c: Construction):
    """Build the equivalent reference ``GadgetSystem`` (for differential testing)."""
    from gadget_system import GadgetSystem
    s = GadgetSystem()
    for i, t in enumerate(c.instances):
        s.add_instance(f"g{i}", c.blockset[t], c.init[i])
    comp_ports = defaultdict(list)
    for p in range(c.num_ports):
        comp_ports[c.wire[p]].append(p)
    for ports in comp_ports.values():
        for p, q in zip(ports, ports[1:]):
            (i0, a0), (i1, a1) = c.port_of[p], c.port_of[q]
            s.connect((f"g{i0}", a0), (f"g{i1}", a1))
    for x, comp in enumerate(c.iface):
        rep = comp_ports[comp][0]
        i, a = c.port_of[rep]
        s.expose(str(x), (f"g{i}", a))
    return s
