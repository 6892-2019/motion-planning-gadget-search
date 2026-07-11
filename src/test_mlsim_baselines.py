#!/usr/bin/env python3
"""Tests for the Solver protocol, the baselines, and the resumable eval harness."""
import os
import random
import shutil
import tempfile
import unittest

from gadget import Gadget
from mlsim.baselines import AnnealSolver, CanonicalRandomSolver, RandomSolver
from mlsim.blocks import make_blockset
from mlsim.core import apply, canon, induced
from mlsim.core.distance import d_mode
from mlsim.eval.protocol import Budget, Oracle, run_baseline
from mlsim.bench.exhaustive import run as run_exhaustive
from mlsim.run.checkpoint import read_json

BLOCKS = make_blockset(["toggle", "dicrumbler"])


def toggle():
    g = Gadget(2, 2); g.add_transition(0, 0, 1, 1); g.add_transition(1, 1, 0, 0); return g

def dicrumbler():
    g = Gadget(2, 2); g.add_transition(0, 0, 1, 1); return g

def two_use():
    g = Gadget(3, 2); g.add_transition(0, 0, 1, 1); g.add_transition(1, 0, 2, 1); return g


class TestDistance(unittest.TestCase):
    def test_zero_iff_equal(self):
        self.assertEqual(d_mode(toggle(), toggle()), 0.0)
        self.assertEqual(d_mode(dicrumbler(), dicrumbler()), 0.0)
        self.assertGreater(d_mode(toggle(), dicrumbler()), 0.0)

    def test_port_count_mismatch_is_one(self):
        g4 = Gadget(2, 4); g4.add_transition(0, 0, 1, 1)
        self.assertEqual(d_mode(g4, toggle()), 1.0)


class TestOracle(unittest.TestCase):
    def test_counts_and_detects(self):
        o = Oracle(toggle())
        # a single toggle exposing both ports induces a toggle
        c = apply(BLOCKS, [("ADD", 0, 0), ("EXPOSE", 0, 0), ("EXPOSE", 1, 1), ("STOP",)])
        solved, dist = o.evaluate(c)
        self.assertTrue(solved)
        self.assertEqual(dist, 0.0)
        self.assertEqual(o.calls, 1)


class TestSolversSolveEasyTargets(unittest.TestCase):
    def _check(self, solver, target, budget=1500):
        rng = random.Random(0)
        res = solver.solve(target, BLOCKS, Budget(budget), rng)
        self.assertTrue(res.solved, f"{solver.name} failed on {canon(target).hex()[:8]}")
        # witness re-verifies exactly
        c = apply(BLOCKS, [tuple(a) for a in res.witness])
        g, _ = induced(c)
        self.assertEqual(canon(g), canon(target))
        self.assertLessEqual(res.calls, budget)

    def test_random_solves_m1(self):
        for tgt in (toggle(), dicrumbler()):
            self._check(RandomSolver(), tgt)

    def test_canonical_random_solves_m1(self):
        for tgt in (toggle(), dicrumbler()):
            self._check(CanonicalRandomSolver(), tgt)

    def test_anneal_solves_m1(self):
        for tgt in (toggle(), dicrumbler()):
            self._check(AnnealSolver(), tgt)

    def test_anneal_solves_two_use(self):
        self._check(AnnealSolver(), two_use(), budget=6000)


class TestEvalHarnessResumable(unittest.TestCase):
    """Build a tiny benchmark, then run a baseline over it with interruptions."""

    def setUp(self):
        self.dirs = []
        self.bench = self._tmp()
        ident = {"job": "exhaustive", "gen_version": 2, "blocks": ["toggle", "dicrumbler"],
                 "max_instances": 2, "target_max_states": 4, "target_max_ports": 4}
        self.assertEqual(run_exhaustive(ident, self.bench, checkpoint_seconds=0.0), "done")

    def tearDown(self):
        for d in self.dirs:
            shutil.rmtree(d, ignore_errors=True)

    def _tmp(self):
        d = tempfile.mkdtemp(prefix="mlsim-base-")
        self.dirs.append(d)
        return d

    def _identity(self):
        return {"job": "baseline", "solver": "canonical-random",
                "blocks": ["toggle", "dicrumbler"], "budget_calls": 400,
                "max_inst": 5, "seed": 1}

    @staticmethod
    def _stable(results):
        # drop wall_s (wall-clock is inherently non-deterministic); the rest of a
        # seeded run must be reproducible across interruptions.
        return {k: {kk: vv for kk, vv in v.items() if kk != "wall_s"}
                for k, v in results.items()}

    def test_interrupted_equals_uninterrupted(self):
        ident = self._identity()
        d_full = self._tmp()
        self.assertEqual(
            run_baseline(ident, d_full, CanonicalRandomSolver(max_inst=5), self.bench,
                         seed=1, checkpoint_seconds=0.0), "done")
        full = read_json(os.path.join(d_full, "results.json"))

        d_int = self._tmp()
        status, guard = "paused", 0
        while status == "paused":
            status = run_baseline(ident, d_int, CanonicalRandomSolver(max_inst=5),
                                  self.bench, seed=1, checkpoint_seconds=0.0, stop_after=2)
            guard += 1
            self.assertLess(guard, 1000)
        self.assertEqual(self._stable(read_json(os.path.join(d_int, "results.json"))),
                         self._stable(full))
        self.assertGreater(len(full), 0)

    def test_metrics_written_and_consistent(self):
        d = self._tmp()
        run_baseline(self._identity(), d, CanonicalRandomSolver(max_inst=5), self.bench,
                     seed=1, checkpoint_seconds=0.0)
        m = read_json(os.path.join(d, "metrics.json"))
        res = read_json(os.path.join(d, "results.json"))
        self.assertEqual(m["num_targets"], len(res))
        self.assertEqual(m["num_solved"], sum(1 for r in res.values() if r["solved"]))
        # easy benchmark: a canonical-random baseline should solve a good chunk
        self.assertGreater(m["num_solved"], 0)


if __name__ == "__main__":
    unittest.main()
