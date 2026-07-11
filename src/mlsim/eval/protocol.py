"""The Solver protocol, the verifier Oracle, and a resumable benchmark runner.

Unit of work = one ``Oracle.evaluate`` call = one ``induced`` evaluation (the shared
dominant cost of both verifying and computing behavioural distance). Budgets and the
primary efficiency metric (verifier-calls) are counted in these units, so every
baseline and ML method is compared on the same currency (docs/ml/06).

Every claimed solution is re-verified by the exact oracle before it counts. The runner
is resumable (mlsim.run): pausing/killing loses at most one target's work.
"""
import os
import time
from dataclasses import dataclass
from typing import List, Optional, Tuple

from gadget import Gadget
from mlsim.core import BlockSet, Construction, apply, canon, induced, to_actions
from mlsim.core.distance import d_mode, state_language_multiset
from mlsim.run.checkpoint import (RunDir, StopFlag, atomic_write_json,
                                  install_signal_handlers, read_json)


@dataclass
class Budget:
    max_calls: int
    max_wall_s: float = float("inf")


@dataclass
class Result:
    solved: bool
    calls: int
    wall_s: float
    found_size: Optional[int] = None
    witness: Optional[list] = None      # list of LINK actions (as lists)
    name: str = ""


class Oracle:
    """Wraps a target gadget; ``evaluate`` returns (solved, distance) and counts one
    unit of work. Exact solved-detection via ``canon`` (relabeling-invariant); distance
    is only a search guide."""

    def __init__(self, target: Gadget):
        self.target = target
        self._tcanon = canon(target)
        self._tell = target.num_locations
        self._tlangs = state_language_multiset(target)
        self.calls = 0

    def evaluate(self, c: Construction) -> Tuple[bool, float]:
        self.calls += 1
        g, defects = induced(c)
        if defects or g.num_locations != self._tell:
            return (False, 1.0)
        if canon(g) == self._tcanon:
            return (True, 0.0)
        return (False, d_mode(g, self.target, self._tlangs))


class Solver:
    """Base class. Subclasses implement ``solve``; they own their Oracle so
    ``result.calls`` is the exact number of evaluations they spent."""

    name = "base"

    def solve(self, target: Gadget, blocks: BlockSet, budget: Budget, rng) -> Result:
        raise NotImplementedError

    @staticmethod
    def _result(oracle: Oracle, t0: float, solved: bool,
                c: Optional[Construction], name: str) -> Result:
        return Result(
            solved=solved, calls=oracle.calls, wall_s=time.time() - t0,
            found_size=(len(c.instances) if solved and c is not None else None),
            witness=([list(a) for a in to_actions(c)] if solved and c is not None else None),
            name=name)


# ---- benchmark runner (resumable) -------------------------------------------
def reconstruct_target(blocks: BlockSet, witness_actions) -> Gadget:
    """Recover a concrete target gadget from a benchmark witness construction."""
    c = apply(blocks, [tuple(a) for a in witness_actions])
    g, _ = induced(c)
    return g


def _metrics(results: dict, budget_calls: int) -> dict:
    thresholds = [n for n in (10, 30, 100, 300, 1000, 3000, budget_calls)
                  if n <= budget_calls]
    thresholds = sorted(set(thresholds))
    solved = [r for r in results.values() if r["solved"]]

    def rate_at(n):
        return round(sum(1 for r in solved if r["calls"] <= n) / max(1, len(results)), 4)

    def vc_star(subset):
        cs = sorted(r["calls"] for r in subset if r["solved"])
        return cs[len(cs) // 2] if cs else None

    by_tier = {}
    tiers = sorted({r["m_star"] for r in results.values()})
    for t in tiers:
        sub = [r for r in results.values() if r["m_star"] == t]
        s = [r for r in sub if r["solved"]]
        by_tier[str(t)] = {"n": len(sub), "solved": len(s),
                           "solve_rate": round(len(s) / max(1, len(sub)), 4),
                           "vc_star": vc_star(sub)}
    return {
        "num_targets": len(results),
        "num_solved": len(solved),
        "solve_rate": round(len(solved) / max(1, len(results)), 4),
        "vc_star": vc_star(list(results.values())),
        "solve_rate_at": {str(n): rate_at(n) for n in thresholds},
        "by_tier": by_tier,
    }


def run_baseline(identity: dict, out_dir: str, solver: Solver, benchmark_dir: str,
                 seed: int = 0, checkpoint_seconds: float = 10.0,
                 stop_flag: Optional[StopFlag] = None,
                 stop_after: Optional[int] = None) -> str:
    """Run (or resume) ``solver`` over every SIM target in ``benchmark_dir``.
    Returns 'done' or 'paused'. Results are keyed by target id, so resuming just skips
    targets already done."""
    from mlsim.blocks import make_blockset
    blocks = make_blockset(identity["blocks"])
    budget = Budget(identity["budget_calls"], identity.get("budget_wall_s", float("inf")))

    labels = read_json(os.path.join(benchmark_dir, "labels.json"))
    if labels is None:
        raise SystemExit(f"[mlsim] no labels.json in {benchmark_dir!r}; run the "
                         f"exhaustive labeler there first.")

    rd = RunDir(out_dir, identity)
    results = read_json(rd.file("results.json"), {})

    def checkpoint():
        atomic_write_json(rd.file("results.json"), results)
        atomic_write_json(rd.file("metrics.json"), _metrics(results, budget.max_calls))

    processed = 0
    last = time.time()
    try:
        for target_id in sorted(labels):
            if target_id in results:
                continue
            meta = labels[target_id]
            target = reconstruct_target(blocks, meta["witness"])
            import random
            rng = random.Random(f"{seed}:{target_id}")
            res = solver.solve(target, blocks, budget, rng)
            if res.solved:                                  # exact re-verification
                c = apply(blocks, [tuple(a) for a in res.witness])
                g, _ = induced(c)
                if canon(g) != canon(target):
                    raise AssertionError(f"solver {solver.name} returned a bogus "
                                         f"solution for {target_id}")
            results[target_id] = {
                "solved": res.solved, "calls": res.calls, "wall_s": round(res.wall_s, 4),
                "found_size": res.found_size, "m_star": meta["min_size"],
                "q": meta["q"], "ell": meta["ell"]}
            processed += 1
            now = time.time()
            if now - last > checkpoint_seconds:
                checkpoint()
                last = now
            if (stop_flag is not None and stop_flag.stop) or \
                    (stop_after is not None and processed >= stop_after):
                checkpoint()
                return "paused"
        checkpoint()
        return "done"
    finally:
        checkpoint()
