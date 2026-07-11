#!/usr/bin/env python3
"""Robustness tests for the resumable-run framework and the exhaustive labeling job.

The core guarantee: a run interrupted arbitrarily often and resumed produces exactly
the same labels as an uninterrupted run, and never corrupts its checkpoints.
"""
import os
import shutil
import tempfile
import unittest

from mlsim.bench.exhaustive import run
from mlsim.run.checkpoint import (RunDir, atomic_write_json, config_hash,
                                  read_json)

IDENTITY = {"job": "exhaustive", "blocks": ["toggle", "dicrumbler"],
            "max_instances": 2, "target_max_states": 4, "target_max_ports": 4}
# smaller instance for the O(N^2) every-candidate-interrupt stress tests
SMALL = {"job": "exhaustive", "blocks": ["toggle"],
         "max_instances": 2, "target_max_states": 4, "target_max_ports": 4}


def _full(d, identity=IDENTITY):
    status = run(identity, d, checkpoint_seconds=0.0)
    assert status == "done"
    return read_json(os.path.join(d, "labels.json"))


class TestResumable(unittest.TestCase):
    def setUp(self):
        self.dirs = []

    def tearDown(self):
        for d in self.dirs:
            shutil.rmtree(d, ignore_errors=True)

    def _tmp(self):
        d = tempfile.mkdtemp(prefix="mlsim-test-")
        self.dirs.append(d)
        return d

    def test_interrupted_equals_uninterrupted(self):
        full = _full(self._tmp())
        d = self._tmp()
        status, guard = "paused", 0
        while status == "paused":
            status = run(IDENTITY, d, checkpoint_seconds=0.0, stop_after=3)
            guard += 1
            self.assertLess(guard, 100000)
        resumed = read_json(os.path.join(d, "labels.json"))
        self.assertEqual(status, "done")
        self.assertEqual(full, resumed)
        # labels are non-empty (the job actually did something)
        self.assertGreater(len(full), 0)

    def test_interrupt_after_every_single_candidate(self):
        full = _full(self._tmp(), SMALL)
        d = self._tmp()
        status, guard = "paused", 0
        while status == "paused":
            status = run(SMALL, d, checkpoint_seconds=0.0, stop_after=1)
            guard += 1
            self.assertLess(guard, 100000)
        self.assertEqual(read_json(os.path.join(d, "labels.json")), full)

    def test_determinism(self):
        self.assertEqual(_full(self._tmp()), _full(self._tmp()))

    def test_labels_are_monotone_during_resume(self):
        # min_size for a target may only decrease or stay; the target set only grows.
        d = self._tmp()
        prev = {}
        status = "paused"
        while status == "paused":
            status = run(SMALL, d, checkpoint_seconds=0.0, stop_after=5)
            cur = read_json(os.path.join(d, "labels.json"))
            for k, v in prev.items():
                self.assertIn(k, cur)
                self.assertLessEqual(cur[k]["min_size"], v["min_size"])
            prev = cur

    def test_config_mismatch_is_refused(self):
        d = self._tmp()
        run(IDENTITY, d, stop_after=1)
        bad = dict(IDENTITY, max_instances=3)
        with self.assertRaises(SystemExit):
            run(bad, d, stop_after=1)

    def test_status_files_written(self):
        d = self._tmp()
        _full(d)
        self.assertTrue(os.path.exists(os.path.join(d, "summary.json")))
        summ = read_json(os.path.join(d, "summary.json"))
        self.assertEqual(summ["num_targets"], len(read_json(os.path.join(d, "labels.json"))))


class TestCheckpointPrimitives(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(prefix="mlsim-ckpt-")

    def tearDown(self):
        shutil.rmtree(self.d, ignore_errors=True)

    def test_atomic_write_roundtrip(self):
        p = os.path.join(self.d, "x.json")
        atomic_write_json(p, {"a": 1})
        atomic_write_json(p, {"a": 2})       # overwrite atomically
        self.assertEqual(read_json(p), {"a": 2})
        # no leftover temp files
        self.assertEqual([f for f in os.listdir(self.d) if f.startswith(".tmp-")], [])

    def test_read_json_missing_returns_default(self):
        self.assertEqual(read_json(os.path.join(self.d, "nope.json"), {"d": 1}), {"d": 1})

    def test_config_hash_stable_and_order_independent(self):
        self.assertEqual(config_hash({"a": 1, "b": 2}), config_hash({"b": 2, "a": 1}))
        self.assertNotEqual(config_hash({"a": 1}), config_hash({"a": 2}))

    def test_rundir_resume_vs_fresh(self):
        cfg = {"blocks": ["toggle"], "max_instances": 2}
        self.assertFalse(RunDir(self.d, cfg).resumed)
        self.assertTrue(RunDir(self.d, cfg).resumed)
        with self.assertRaises(SystemExit):
            RunDir(self.d, {"blocks": ["toggle"], "max_instances": 3})


if __name__ == "__main__":
    unittest.main()
