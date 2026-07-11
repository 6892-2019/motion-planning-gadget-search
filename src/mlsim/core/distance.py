"""Behavioural distance between an induced gadget and a target (docs/ml/02 §3).

Used as the energy for the annealing baseline (B2b) and as a shaped reward later.
``d_mode`` is 0 iff the gadgets are behaviourally equal (``canon`` match) and grows
smoothly as they diverge, so it gives search a gradient. Exact "solved" detection is
always done separately via ``canon`` — the distance is only a guide.
"""
from collections import Counter
from itertools import permutations
from typing import Dict, List

from gadget import Gadget
from gadget_simulation import _canonical_language
from .gadget import relabel_locs


def state_language_multiset(g: Gadget) -> Counter:
    """Multiset of per-state canonical languages — the behaviour fingerprint used by
    ``canon_fixed`` (docs/ml/01 §1.1), here kept as a multiset for a graded distance."""
    return Counter(_canonical_language(g, s) for s in range(g.num_states))


def _jaccard_distance(a: Counter, b: Counter) -> float:
    inter = sum((a & b).values())
    union = sum((a | b).values())
    return 1.0 - inter / union if union else 0.0


def d_mode(g: Gadget, target: Gadget,
           target_langs: Counter = None) -> float:
    """Distance in [0, 1]: minimum over port relabelings of the Jaccard distance
    between the two gadgets' per-state canonical-language multisets. 0 iff equal.

    ``target_langs`` may be precomputed (``state_language_multiset(target)``) to avoid
    recomputing it every call. Ports must match to align; otherwise distance is 1.
    """
    if g.num_locations != target.num_locations:
        return 1.0
    if target_langs is None:
        target_langs = state_language_multiset(target)
    best = 1.0
    for perm in permutations(range(g.num_locations)):
        d = _jaccard_distance(state_language_multiset(relabel_locs(g, perm)), target_langs)
        if d < best:
            best = d
            if best == 0.0:
                break
    return best
