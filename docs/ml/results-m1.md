# M1 results — the classical baseline frontier (the bar for ML)

First measured results. This is the **bar** the ML methods (E1→E4) must beat.

## Setup
- **Benchmark:** exhaustive `m*` labeling (baseline **B3**) for block set
  `{toggle, dicrumbler}`, up to 4 instances, targets ≤ 4 states × 4 ports.
  → **86 reachable target gadgets**, `m*` = 1/2/3/4 for 3/13/21/49 of them.
  (`runs/ex-toggle-dicrumbler-n4`, regenerate with `mlsim/bench/exhaustive.py`.)
- **Budget:** 2000 verifier-calls per target; `max_inst = 6`; seed 0.
- **Metric currency:** verifier-calls (one = one `induced` evaluation). Every claimed
  solution re-verified exactly.

## Results (solve-rate @ 2000 calls; VC\* = median calls-to-solve among solved)

| solver | solved / 86 | rate | VC\* | m\*=1 | m\*=2 | m\*=3 | m\*=4 |
|---|---|---|---|---|---|---|---|
| **B1 random** | 44 | 0.51 | 74 | 3/3 | 13/13 | 18/21 | 10/49 |
| **B2a canonical-random** | 45 | 0.52 | 71 | 3/3 | 13/13 | 19/21 | 10/49 |
| **B2b anneal** | 35 | 0.41 | 75 | 3/3 | 13/13 | 12/21 | 7/49 |

Solve-rate vs budget (fraction of all 86 solved within N calls):

| N calls | 10 | 30 | 100 | 300 | 1000 | 2000 |
|---|---|---|---|---|---|---|
| random | .11 | .17 | .29 | .34 | .44 | .51 |
| canonical-random | .12 | .17 | .29 | .34 | .44 | .52 |
| anneal | .08 | .13 | .21 | .29 | .35 | .41 |

## Findings
1. **Random search is strong here** — exactly the regime the plan's H0 warned about:
   with a cheap exact verifier and a small space, uniform/canonical random is a
   serious baseline. **This is the bar.** Canonical (dedup) random edges out plain
   random (45 vs 44; +1 on m\*=3) at slightly lower VC\*, as expected.
2. **The annealer (B2b) currently *underperforms* random** (35 vs 45). Cause: the
   `d_mode` distance (Jaccard of per-state canonical-language multisets) is too coarse
   — it saturates near 1.0 until a construction is structurally almost-correct, giving
   almost no gradient, so the local walk is worse than fresh random sampling. **B2b is
   not yet the strong baseline the plan calls for.** Fix before trusting it as the bar:
   implement `d_lang` (symmetric difference of length-≤L traversal words — graded
   partial credit) and tune `t0/alpha/restart` (docs/ml/03 B2b). Tracked as the next
   baseline task.
3. **The frontier is the m\*=4 tier**: best is 10/49 solved. This is where solvers
   struggle and therefore where ML (better proposals / guided search) has the most
   room to help — the right place to focus E2/E3.

## Caveats
- One block set, one budget, `max_inst=6`, seed 0 — not yet a multi-seed, multi-budget
  sweep (docs/ml/06). These numbers are indicative, not final.
- B2a's wall-time is high because it computes `canon_sys` (up to `n!`) per sample for
  dedup; its *verifier-call* efficiency is what the table reports. A faster
  canonicalizer (nauty-style) is the fix.

## Reproduce
```
B=runs/ex-toggle-dicrumbler-n4
python3 src/mlsim/bench/exhaustive.py --out $B --blocks toggle dicrumbler --max-instances 4
for s in random canonical-random anneal; do
  python3 src/mlsim/eval/run_baseline.py --benchmark $B --solver $s --budget-calls 2000 --out runs/base-$s
done
python3 src/mlsim/eval/run_baseline.py --out runs/base-anneal --status
```
