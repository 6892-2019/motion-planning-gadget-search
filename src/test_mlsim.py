#!/usr/bin/env python3
"""M0 acceptance tests for mlsim.core (docs/ml/01 §6, docs/ml/07 §5).

The five invariants that make everything downstream trustworthy:
  1. canon(A)==canon(B)          <=> behaviorally_equivalent(A, B)
     canon_fixed(A)==canon_fixed(B) <=> behaviorally_equivalent(A, B, fixed_locations=True)
  2. mlsim.induced == reference GadgetSystem.induced_gadget  (differential, canon-level)
  3. canon(induced(c)) invariant under construction symmetries (relabel)
  4. canon_sys invariant under construction symmetries
  5. apply(to_actions(c)) canon_sys-equals c   (grammar round-trip)
"""
import random
import unittest

from gadget import Gadget
from gadget_simulation import behaviorally_equivalent
from mlsim.core import (BlockSet, Construction, canon, canon_fixed, induced,
                        verify, to_gadget_system, canon_sys, relabel,
                        block_port_automorphisms, apply, to_actions)


# ---- building blocks --------------------------------------------------------
def toggle():
    g = Gadget(2, 2); g.add_transition(0, 0, 1, 1); g.add_transition(1, 1, 0, 0); return g

def dicrumbler():
    g = Gadget(2, 2); g.add_transition(0, 0, 1, 1); return g

def two_toggle():
    g = Gadget(2, 4)
    for t in [(0, 0, 1, 1), (0, 2, 1, 3), (1, 1, 0, 0), (1, 3, 0, 2)]:
        g.add_transition(*t)
    return g

def seven():
    g = Gadget(2, 2)
    for t in [(0, 0, 1, 1), (1, 1, 0, 0), (1, 0, 1, 1), (1, 1, 1, 0)]:
        g.add_transition(*t)
    return g

BS = BlockSet([toggle(), dicrumbler(), two_toggle(), seven()])


# ---- random generators ------------------------------------------------------
def random_gadget(rng, qmax=3, lmax=3):
    q = rng.randint(1, qmax); ell = rng.randint(2, lmax)
    g = Gadget(q, ell)
    for _ in range(rng.randint(0, q * ell)):
        s, a, s2, b = rng.randrange(q), rng.randrange(ell), rng.randrange(q), rng.randrange(ell)
        if a != b:
            g.add_transition(s, a, s2, b)
    return g

def random_construction(rng, bs=BS, max_inst=4, random_init=False):
    n = rng.randint(1, max_inst)
    inst = [rng.randrange(len(bs)) for _ in range(n)]
    nports = sum(bs.num_locations(t) for t in inst)
    W = rng.randint(1, nports)
    wire = [rng.randrange(W) for _ in range(nports)]
    comps = sorted(set(wire))
    k = rng.randint(1, len(comps))
    iface = rng.sample(comps, k)
    init = [rng.randrange(bs.num_states(t)) if random_init else 0 for t in inst]
    return Construction(bs, inst, wire, iface, init)

def random_symmetry(rng, c):
    n = len(c.instances)
    perm = list(range(n)); rng.shuffle(perm)
    autos = [block_port_automorphisms(c.blockset[c.instances[perm[j]]], c.init[perm[j]])
             for j in range(n)]
    sigmas = [rng.choice(a) for a in autos]
    return relabel(c, tuple(perm), sigmas)


class TestCanonMatchesEquivalence(unittest.TestCase):
    def test_contract(self):
        rng = random.Random(0)
        gs = [random_gadget(rng) for _ in range(60)]
        for i in range(len(gs)):
            for j in range(len(gs)):
                A, B = gs[i], gs[j]
                if A.num_locations != B.num_locations:
                    continue
                self.assertEqual(canon(A) == canon(B),
                                 behaviorally_equivalent(A, B),
                                 msg=f"canon vs equiv mismatch {i},{j}")
                self.assertEqual(canon_fixed(A) == canon_fixed(B),
                                 behaviorally_equivalent(A, B, fixed_locations=True),
                                 msg=f"canon_fixed vs fixed-equiv mismatch {i},{j}")

    def test_self_equal(self):
        rng = random.Random(1)
        for _ in range(50):
            g = random_gadget(rng)
            self.assertEqual(canon(g), canon(g))
            self.assertEqual(canon_fixed(g), canon_fixed(g))


class TestSimulatorVsReference(unittest.TestCase):
    def _check(self, c):
        g_ml, def_ml = induced(c)
        ref = to_gadget_system(c)
        g_ref, _cfgs, _labels, def_ref = ref.induced_gadget()
        self.assertEqual(canon_fixed(g_ml), canon_fixed(g_ref), msg=f"induced mismatch: {c}")
        # defect sets (normalise reference's string labels to ints)
        ref_defects = {(cfg, int(x), ce) for cfg, x, ce in def_ref}
        self.assertEqual(set(def_ml), ref_defects, msg=f"defect mismatch: {c}")

    def test_random_zero_init(self):
        rng = random.Random(2)
        for _ in range(800):
            self._check(random_construction(rng))

    def test_random_random_init(self):
        rng = random.Random(3)
        for _ in range(400):
            self._check(random_construction(rng, random_init=True))


class TestSymmetryInvariance(unittest.TestCase):
    def test_induced_and_canon_sys_invariant(self):
        rng = random.Random(4)
        for _ in range(300):
            c = random_construction(rng, max_inst=3, random_init=True)
            c2 = random_symmetry(rng, c)
            self.assertEqual(canon_sys(c), canon_sys(c2), msg=f"canon_sys not invariant: {c}")
            self.assertEqual(canon(induced(c)[0]), canon(induced(c2)[0]),
                             msg=f"induced not symmetry-invariant: {c}")


class TestGrammarRoundtrip(unittest.TestCase):
    def test_roundtrip(self):
        rng = random.Random(5)
        for _ in range(400):
            c = random_construction(rng, max_inst=3, random_init=True)
            self.assertEqual(canon_sys(apply(c.blockset, to_actions(c))), canon_sys(c))


class TestKnownConstructions(unittest.TestCase):
    def test_two_toggles_in_series_is_a_toggle(self):
        # g0 ports (0,1); g1 ports (2,3); wire g0.1--g1.0; expose L=port0, R=port3
        c = Construction(BS, instances=[0, 0], wire=[0, 1, 1, 2], iface=[0, 2])
        self.assertTrue(verify(c, toggle()))
        self.assertFalse(verify(c, dicrumbler()))   # reversible can't be a dicrumbler

    def test_two_dicrumblers_parallel_is_two_use(self):
        # shared entry + shared exit: g0(1),g1(1) dicrumblers; ports 0,1 and 2,3
        # wire entries 0--2 (comp A), exits 1--3 (comp B); expose L=A, R=B
        c = Construction(BS, instances=[1, 1], wire=[0, 1, 0, 1], iface=[0, 1])
        two_use = Gadget(3, 2); two_use.add_transition(0, 0, 1, 1); two_use.add_transition(1, 0, 2, 1)
        self.assertTrue(verify(c, two_use))

    def test_toggle_block_automorphisms_trivial_from_state0(self):
        # A toggle's port swap needs the state swap, which moves init 0 -> 1,
        # so only the identity port-automorphism fixes state 0.
        self.assertEqual(block_port_automorphisms(toggle(), fix_state=0), [(0, 1)])


if __name__ == "__main__":
    unittest.main()
