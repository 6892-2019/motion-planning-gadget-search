"""Canonical gadget keys (docs/ml/01 §1).

We reuse the reference ``Gadget`` and the verified per-state canonical-language
routine from ``gadget_simulation`` (which mirrors the C++ minimize+canonicalize),
and build byte keys on top:

    canon_fixed(G) == canon_fixed(H)  <=>  behaviorally_equivalent(G, H, fixed_locations=True)
    canon(G)       == canon(H)        <=>  behaviorally_equivalent(G, H)

``canon_fixed`` is the *set* (dedup) of per-state canonical languages plus the port
count; ``canon`` minimises it over port relabelings. See the equality contract in
docs/ml/01.
"""
from itertools import permutations
from typing import Dict, Tuple

from gadget import Gadget
from gadget_simulation import _canonical_language, is_reversible, is_dag

__all__ = ["Gadget", "canon", "canon_fixed", "relabel_locs", "minimized_shape",
           "is_reversible", "is_dag"]


def minimized_shape(g: Gadget) -> Tuple[int, int]:
    """(minimized state count, port count). The minimized state count is the number
    of behaviourally-distinct states, i.e. the size of the minimal automaton — the
    right quantity to cap the target universe on (raw reachable configs can be larger)."""
    q_min = len({_canonical_language(g, s) for s in range(g.num_states)})
    return q_min, g.num_locations


def _sig(g: Gadget) -> Tuple:
    """A hashable signature of a gadget's raw structure (for caching)."""
    return (g.num_states, g.num_locations, tuple(sorted(g.transitions)))


def relabel_locs(g: Gadget, perm: Tuple[int, ...]) -> Gadget:
    """Return a copy of ``g`` with location ``a`` renamed to ``perm[a]``."""
    h = Gadget(g.num_states, g.num_locations)
    h.transitions = [(fs, perm[fl], ts, perm[tl]) for fs, fl, ts, tl in g.transitions]
    return h


_canon_fixed_cache: Dict[Tuple, bytes] = {}
_canon_cache: Dict[Tuple, bytes] = {}


def canon_fixed(g: Gadget) -> bytes:
    """Canonical key with ports held fixed (the simulation interface identity)."""
    key = _sig(g)
    hit = _canon_fixed_cache.get(key)
    if hit is not None:
        return hit
    langs = sorted({_canonical_language(g, s) for s in range(g.num_states)})
    out = repr((g.num_locations, langs)).encode()
    _canon_fixed_cache[key] = out
    return out


def canon(g: Gadget) -> bytes:
    """Canonical key up to port relabeling (abstract gadget identity)."""
    key = _sig(g)
    hit = _canon_cache.get(key)
    if hit is not None:
        return hit
    best = None
    for perm in permutations(range(g.num_locations)):
        cf = canon_fixed(relabel_locs(g, perm))
        if best is None or cf < best:
            best = cf
    _canon_cache[key] = best
    return best
