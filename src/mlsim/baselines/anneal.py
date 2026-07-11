"""B2b: simulated annealing / MCMC over constructions (docs/ml/03).

The strong learning-free baseline. Walks the space of constructions with local moves,
accepting by Metropolis on the behavioural distance (docs/ml/02 §3), with restarts on
stagnation. Exact solved-detection is via the Oracle (canon), so the distance only has
to give a useful gradient. This is the bar the ML methods must beat at matched budget.
"""
import math
import time
from typing import List, Optional

from mlsim.core import BlockSet, Construction
from mlsim.eval.protocol import Budget, Oracle, Result, Solver
from .random_search import sample_construction


def _instance_port_ranges(blocks: BlockSet, inst: List[int]):
    ranges, o = [], 0
    for t in inst:
        w = blocks.num_locations(t)
        ranges.append((o, o + w))
        o += w
    return ranges


def neighbour(cur: Construction, blocks: BlockSet, k: int, rng,
              max_inst: int) -> Optional[Construction]:
    """One local move; returns a valid construction exposing exactly ``k`` components,
    or None if the move was rejected (caller treats None as a no-op)."""
    inst = list(cur.instances)
    wire = list(cur.wire)
    iface = list(cur.iface)
    r = rng.random()
    try:
        if r < 0.25 and len(inst) < max_inst:
            # add an instance; its ports become fresh singleton components
            t = rng.randrange(len(blocks))
            base = (max(wire) + 1) if wire else 0
            inst.append(t)
            wire.extend(base + a for a in range(blocks.num_locations(t)))
        elif r < 0.45 and len(inst) > 1:
            # remove an instance (only if it doesn't drop an exposed component)
            i = rng.randrange(len(inst))
            lo, hi = _instance_port_ranges(blocks, inst)[i]
            keep = [p for p in range(len(wire)) if not (lo <= p < hi)]
            new_wire = [wire[p] for p in keep]
            if not set(iface).issubset(set(new_wire)):
                return None
            inst = inst[:i] + inst[i + 1:]
            wire = new_wire
        elif r < 0.70:
            # merge two components
            comps = sorted(set(wire))
            if len(comps) <= k:
                return None
            a, b = rng.sample(comps, 2)
            wire = [a if x == b else x for x in wire]
            iface = [a if c == b else c for c in iface]
            if len(set(iface)) < k:
                return None
        elif r < 0.85:
            # split: move one port to a fresh component
            p = rng.randrange(len(wire))
            wire[p] = (max(wire) + 1)
            if not set(iface).issubset(set(wire)):
                return None
        else:
            # reassign which components are exposed
            comps = set(wire)
            unexposed = [c for c in comps if c not in iface]
            if not unexposed or not iface:
                return None
            iface[rng.randrange(len(iface))] = rng.choice(unexposed)
        return Construction(blocks, inst, wire, iface)
    except Exception:
        return None


class AnnealSolver(Solver):
    name = "anneal"

    def __init__(self, max_inst: int = 6, t0: float = 0.3, alpha: float = 0.999,
                 restart_after: int = 250):
        self.max_inst = max_inst
        self.t0 = t0
        self.alpha = alpha
        self.restart_after = restart_after

    def _fresh(self, oracle, blocks, k, rng):
        for _ in range(50):
            c = sample_construction(blocks, k, rng, self.max_inst)
            if c is not None:
                solved, e = oracle.evaluate(c)
                return c, e, solved
        return None, 1.0, False

    def solve(self, target, blocks, budget: Budget, rng) -> Result:
        oracle = Oracle(target)
        t0 = time.time()
        k = target.num_locations

        cur, cur_e, solved = self._fresh(oracle, blocks, k, rng)
        if solved:
            return self._result(oracle, t0, True, cur, self.name)
        if cur is None:
            return self._result(oracle, t0, False, None, self.name)

        best_e = cur_e
        temp = self.t0
        stale = 0
        while oracle.calls < budget.max_calls and time.time() - t0 < budget.max_wall_s:
            cand = neighbour(cur, blocks, k, rng, self.max_inst)
            if cand is None:
                stale += 1
            else:
                solved, e = oracle.evaluate(cand)
                if solved:
                    return self._result(oracle, t0, True, cand, self.name)
                if e <= cur_e or rng.random() < math.exp(-(e - cur_e) / max(temp, 1e-6)):
                    cur, cur_e = cand, e
                if cur_e < best_e:
                    best_e, stale = cur_e, 0
                else:
                    stale += 1
            temp *= self.alpha
            if stale > self.restart_after:
                cur, cur_e, solved = self._fresh(oracle, blocks, k, rng)
                if solved:
                    return self._result(oracle, t0, True, cur, self.name)
                if cur is None:
                    break
                temp, stale, best_e = self.t0, 0, cur_e
        return self._result(oracle, t0, False, None, self.name)
