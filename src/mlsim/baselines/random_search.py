"""B1 uniform random search, and B2a canonical (dedup) random search (docs/ml/03).

Both sample constructions that expose exactly ``k = target.num_locations`` components
(otherwise the induced gadget can't match the target's port count). B2a additionally
skips canonically-isomorphic duplicates so budget isn't wasted re-evaluating relabels
of already-tried constructions — a strictly stronger floor than B1.
"""
import time
from typing import Optional

from mlsim.core import BlockSet, Construction, canon_sys
from mlsim.eval.protocol import Budget, Oracle, Result, Solver


def sample_construction(blocks: BlockSet, k: int, rng, max_inst: int,
                        tries: int = 30) -> Optional[Construction]:
    """A random construction exposing exactly ``k`` distinct components (or None)."""
    for _ in range(tries):
        n = rng.randint(1, max_inst)
        inst = [rng.randrange(len(blocks)) for _ in range(n)]
        nports = sum(blocks.num_locations(t) for t in inst)
        if nports < k:
            continue
        w = rng.randint(k, nports)                 # target number of components
        wire = [rng.randrange(w) for _ in range(nports)]
        comps = sorted(set(wire))
        if len(comps) < k:
            continue
        iface = rng.sample(comps, k)
        return Construction(blocks, inst, wire, iface)
    return None


class RandomSolver(Solver):
    name = "random"

    def __init__(self, max_inst: int = 6):
        self.max_inst = max_inst

    def solve(self, target, blocks, budget: Budget, rng) -> Result:
        oracle = Oracle(target)
        t0 = time.time()
        k = target.num_locations
        while oracle.calls < budget.max_calls and time.time() - t0 < budget.max_wall_s:
            c = sample_construction(blocks, k, rng, self.max_inst)
            if c is None:
                continue
            if oracle.check(c):                 # cheap exact test (no distance)
                return self._result(oracle, t0, True, c, self.name)
        return self._result(oracle, t0, False, None, self.name)


class CanonicalRandomSolver(RandomSolver):
    name = "canonical-random"

    def solve(self, target, blocks, budget: Budget, rng) -> Result:
        oracle = Oracle(target)
        t0 = time.time()
        k = target.num_locations
        seen = set()
        stale = 0
        while oracle.calls < budget.max_calls and time.time() - t0 < budget.max_wall_s:
            c = sample_construction(blocks, k, rng, self.max_inst)
            if c is None:
                continue
            key = canon_sys(c)
            if key in seen:
                stale += 1
                if stale > 5000:            # space looks exhausted at this size; stop early
                    break
                continue
            stale = 0
            seen.add(key)
            if oracle.check(c):
                return self._result(oracle, t0, True, c, self.name)
        return self._result(oracle, t0, False, None, self.name)
