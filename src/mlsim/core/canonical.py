"""Canonicalisation of constructions (docs/ml/01 §3).

Two constructions related by (a) permuting instances, (b) applying a block's
port-automorphism to an instance, (c) relabelling components induce the *same*
gadget and must get the same key, or every enumeration/search baseline secretly
double-counts isomorphs. ``canon_sys`` brute-forces this (small ``n``); replace with
graph canonical labelling (nauty/bliss) when ``n`` grows.
"""
from itertools import permutations, product
from typing import Dict, List, Tuple

from gadget import Gadget
from .system import BlockSet, Construction

_auto_cache: Dict[Tuple, Tuple[Tuple[int, ...], ...]] = {}


def block_port_automorphisms(g: Gadget, fix_state: int = 0) -> List[Tuple[int, ...]]:
    """Port permutations ``σ`` for which some state permutation ``ρ`` with
    ``ρ(fix_state)=fix_state`` leaves the transition set invariant.

    Fixing the instance's initial state means σ-relabelling its ports preserves the
    instance's behaviour, so it is a genuine symmetry of the construction.
    """
    key = (g.num_states, g.num_locations, tuple(sorted(g.transitions)), fix_state)
    hit = _auto_cache.get(key)
    if hit is not None:
        return list(hit)
    base = set(g.transitions)
    q, ell = g.num_states, g.num_locations
    sigmas = set()
    for rho in permutations(range(q)):
        if rho[fix_state] != fix_state:
            continue
        for sigma in permutations(range(ell)):
            if {(rho[fs], sigma[fl], rho[ts], sigma[tl]) for fs, fl, ts, tl in base} == base:
                sigmas.add(sigma)
    out = tuple(sorted(sigmas))
    _auto_cache[key] = out
    return list(out)


def _rgs(labels) -> Tuple[List[int], Dict[int, int]]:
    """Restricted-growth normalisation of a partition labelling."""
    remap: Dict[int, int] = {}
    out = []
    for l in labels:
        if l not in remap:
            remap[l] = len(remap)
        out.append(remap[l])
    return out, remap


def relabel(c: Construction, perm: Tuple[int, ...],
            sigmas: List[Tuple[int, ...]]) -> Construction:
    """Return the isomorphic construction where new instance position ``j`` is old
    instance ``perm[j]`` with its ports permuted by ``sigmas[j]``."""
    n = len(c.instances)
    ell = [c.blockset.num_locations(t) for t in c.instances]
    newoff, o = [], 0
    for j in range(n):
        newoff.append(o)
        o += ell[perm[j]]
    port_map = [0] * c.num_ports
    for j in range(n):
        oldi, sig = perm[j], sigmas[j]
        for a in range(ell[oldi]):
            port_map[c.gport(oldi, a)] = newoff[j] + sig[a]
    new_wire = [0] * c.num_ports
    for oldp in range(c.num_ports):
        new_wire[port_map[oldp]] = c.wire[oldp]
    new_instances = [c.instances[perm[j]] for j in range(n)]
    new_init = [c.init[perm[j]] for j in range(n)]
    return Construction(c.blockset, new_instances, new_wire, list(c.iface), new_init)


def canon_sys(c: Construction) -> bytes:
    """Canonical key of a construction, minimal over the symmetry group."""
    n = len(c.instances)
    autos = [block_port_automorphisms(c.blockset[c.instances[i]], c.init[i])
             for i in range(n)]
    best = None
    for perm in permutations(range(n)):
        for sig_choice in product(*[autos[perm[j]] for j in range(n)]):
            rc = relabel(c, perm, list(sig_choice))
            rgs, remap = _rgs(rc.wire)
            iface_norm = tuple(remap[comp] for comp in rc.iface)
            key = (tuple(rc.instances), tuple(rc.init), tuple(rgs), iface_norm)
            if best is None or key < best:
                best = key
    return repr(best).encode()
