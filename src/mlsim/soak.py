#!/usr/bin/env python3
"""Heavier differential soak: many random constructions, mlsim.induced vs the
reference GadgetSystem, at canon level. Also reports induced throughput.

    python3 src/mlsim/soak.py [N] [seed]
"""
import os
import random
import sys
import time

# allow running as a plain script: put src/ (this file's grandparent) on the path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from gadget import Gadget
from mlsim.core import BlockSet, Construction, canon_fixed, induced, to_gadget_system


def _blocks():
    toggle = Gadget(2, 2); toggle.add_transition(0, 0, 1, 1); toggle.add_transition(1, 1, 0, 0)
    dicr = Gadget(2, 2); dicr.add_transition(0, 0, 1, 1)
    twoT = Gadget(2, 4)
    for t in [(0, 0, 1, 1), (0, 2, 1, 3), (1, 1, 0, 0), (1, 3, 0, 2)]:
        twoT.add_transition(*t)
    seven = Gadget(2, 2)
    for t in [(0, 0, 1, 1), (1, 1, 0, 0), (1, 0, 1, 1), (1, 1, 1, 0)]:
        seven.add_transition(*t)
    return BlockSet([toggle, dicr, twoT, seven])


def random_construction(rng, bs, max_inst=5):
    inst = [rng.randrange(len(bs)) for _ in range(rng.randint(1, max_inst))]
    nports = sum(bs.num_locations(t) for t in inst)
    W = rng.randint(1, nports)
    wire = [rng.randrange(W) for _ in range(nports)]
    comps = sorted(set(wire))
    iface = rng.sample(comps, rng.randint(1, len(comps)))
    init = [rng.randrange(bs.num_states(t)) for t in inst]
    return Construction(bs, inst, wire, iface, init)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    rng = random.Random(seed)
    bs = _blocks()

    mismatches = 0
    t_ind = 0.0
    t0 = time.time()
    for i in range(n):
        c = random_construction(rng, bs)
        s = time.time()
        g_ml, def_ml = induced(c)
        t_ind += time.time() - s
        g_ref, _c, _l, def_ref = to_gadget_system(c).induced_gadget()
        ref_defects = {(cfg, int(x), ce) for cfg, x, ce in def_ref}
        if canon_fixed(g_ml) != canon_fixed(g_ref) or set(def_ml) != ref_defects:
            mismatches += 1
            if mismatches <= 5:
                print(f"MISMATCH: {c}")
    dt = time.time() - t0
    print(f"{n} constructions, seed {seed}: {mismatches} mismatch(es) in {dt:.1f}s")
    print(f"  mlsim.induced throughput: {n / t_ind:,.0f}/s")
    sys.exit(1 if mismatches else 0)


if __name__ == "__main__":
    main()
