# 07 — Codebase layout, interfaces, and milestones

Buildable from the other docs. New package **`src/mlsim/`** (revised from a top-level
`mlsim/` so it sits beside the existing flat `src/*.py` modules and imports the
reference oracle — `from gadget import Gadget` — with no path gymnastics; `mlsim`'s
`__init__` puts `src/` on `sys.path`). Pure research code; keeps the existing `src/`
toolkit as the reference oracle. Python now; a compiled simulator later (M1+).

## 1. Layout  (under `src/`)
```
mlsim/
  core/
    gadget.py         # Gadget rep + canon()/canon_fixed() (bytes keys)  [doc 01 §1]
    system.py         # Construction rep, LINK grammar apply/to_actions   [doc 01 §2,5]
    canonical.py      # canon_sys(), orbit_reps(), Aut(block) precompute   [doc 01 §3]
    graphrep.py       # heterogeneous graph encoding                       [doc 01 §4]
    simulator.py      # induced() python reference + bindings to fast core [doc 02 §1]
    simulator_rs/     # Rust(pyo3) or numba fast induced() + canon cache    [doc 02 §1.1]
    distance.py       # d_mode/d_lang/d_spec                               [doc 02 §3]
    invariants.py     # reversible/dag/... extensible                      [doc 01 §1]
  bench/
    enumerate.py      # canonical target universe                         [doc 04 §2]
    label.py          # m*/status via exhaustive(+C++ bridge)+invariants  [doc 04 §3]
    splits.py         # S-rand/size/shape/block, leakage audit            [doc 04 §4]
    datagen.py        # forward corpus + HER synth pairs + classifier data[doc 04 §5]
    cpp_bridge.py     # optional: drive toggles-* binaries / read LMDB     [doc 03 B3]
  baselines/
    random_search.py  # B1, B2a                                           [doc 03]
    anneal.py         # B2b MCMC/annealing
    exhaustive.py     # B3 orderly generation + pruning
    cpsat.py          # B4 (optional)
    mcts.py           # B5 unguided UCT
  ml/
    encoders.py       # GadgetEncoder, SystemEncoder                      [doc 05]
    e1_classifier.py
    e2_synthesis.py   # pointer decoder + HER trainer
    e3_guided.py
    e4_rl.py
    e5_certificates.py
  eval/
    protocol.py       # run(method, split, budget, seeds), re-verify, log [doc 06]
    metrics.py        # SolveRate@N, VC*, break-even, gaps
    report.py         # tables/plots per tier & split
  tests/
    test_canon.py               # invariants doc 01 §6
    test_simulator_vs_reference.py   # doc 02 §1.2  (vs src/gadget_system.py)
    test_grammar_roundtrip.py
    test_split_leakage.py
  configs/            # caps, hyperparams, budgets, thresholds (preregistered)
  README.md           # points back to docs/ml/*
```

## 2. Key interfaces (freeze these first)
```python
# core
def canon(g: Gadget) -> bytes                      # abstract identity
def canon_fixed(g: Gadget) -> bytes                # ports fixed
def induced(c: Construction) -> tuple[Gadget, list[Defect]]
def verify(c: Construction, T: Gadget, iface: Iface|None) -> bool
def canon_sys(c: Construction) -> bytes
def to_actions(c) -> list[Action]; def apply(actions) -> Construction
def d(g: Gadget, T: Gadget) -> float

# solver protocol (every baseline AND ml method implements this)
class Solver(Protocol):
    def solve(self, T: Gadget, B: BlockSet, budget: Budget) -> Result: ...
# Result(solved, construction, verifier_calls, wall_s, gpu_s, found_size, trace)
```
The `Solver` protocol is what makes baselines and ML directly comparable — write it
before anything else.

## 3. Dependencies
`numpy`, `torch` (+`torch_geometric` or DGL), `pynauty`/`bliss` (canonicalization),
`ortools` (CP-SAT, optional), `polars`/`pyarrow` (datasets), `hydra`/`yaml` (configs),
`wandb`/`tensorboard` (logging). Rust toolchain + `maturin`/`pyo3` **or** `numba` for
`simulator_rs`. All CPU-friendly except training.

## 4. Milestones with go/no-go gates
- **M0 — core + oracle parity (wk 1).** `gadget/system/canonical/graphrep`, python
  `induced`, `canon`. Gate: the five invariants in doc 01 §6 + differential test vs
  `src/gadget_system.py` pass. *Now the ground truth is trustworthy.*
- **M1 — fast simulator + benchmark v1 (wk 1–2).** `simulator_rs` at ≥1e5/s; enumerate
  targets ≤3×3 then ≤4×4; B3 labels + tiers + splits; `Solver` protocol + B1/B2a/B2b/B3
  runners; baseline cost-frontier tables. Gate: **H0** (annealing ≫ random). *The bar
  is now measured.*
- **M2 — learnability probe (wk 2).** E1 + hand-feature baseline. Gate: **H1** ΔAUC
  threshold. *Decisive go/no-go for heavy ML.*
- **M3 — amortized synthesis (wk 3–4).** datagen forward+HER; E2 pointer decoder;
  eval vs B2b matched-K on S-rand. Gate: **H2**. Then run S-size/S-shape/S-block for
  **H3**.
- **M4 — guided search (wk 5–6).** E3 vs B5/B3 compute-matched. Gate: **H4/H5**
  (frontier). Optional B4 CP-SAT here as a stronger classical anchor.
- **M5 — stretch.** E4 (if a gap remains); E5 impossibility certificates (**H7**);
  writeup with break-even (**H6**) and any verified new simulations.

## 5. First PR checklist (M0)
- [ ] `Gadget`, `canon`, `canon_fixed` with LRU cache; unit tests vs
      `behaviorally_equivalent`.
- [ ] `Construction`, `induced` (python), `verify`; differential test harness vs
      `src/gadget_system.py` (1e5 random, incl. nondeterministic blocks).
- [ ] `canon_sys` + `Aut(block)` precompute; symmetry-invariance test.
- [ ] LINK grammar `apply/to_actions` round-trip test.
- [ ] `Solver` protocol + B1 as the first trivial solver end-to-end through
      `eval/protocol.py`.
Only after M0 do any numbers mean anything.

## 6. Reproducibility & scope guards
- every solution re-verified by the exact oracle (no trust in learned outputs);
- seeds + code/data hashes in `manifest.json`; preregistered thresholds/budgets;
- cap system size (verifier is exponential in `n`); budget in verifier-seconds;
- keep `mlsim/` importable without a GPU (baselines + core run CPU-only) so the
  decisive M0–M2 slice needs no accelerator.
