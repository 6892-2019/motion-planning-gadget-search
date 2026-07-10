# 06 — Evaluation protocol, metrics, and hypotheses

## 1. The runner
Single entry point `eval/protocol.py::run(method, split, budget, seeds)`:
- iterates test targets of a split; calls `method.solve(target, B, budget)`;
- logs one row per (target, method, seed):
  `target_id, tier, m*, method, seed, solved, verifier_calls, wall_s, gpu_s,
   found_size, first_solution_actions`;
- **re-verifies** every claimed solution with the exact oracle before it counts;
- accounts compute: `verifier_calls`, wall/GPU-seconds, and for ML the **amortized
  training** cost (params×tokens FLOPs + data-gen verifier-calls) spread over the test
  set.

## 2. Metrics (report all; per tier T0–T3, TI; per split)
- **SolveRate@N** — solved fraction within `N` verifier-calls (sweep `N` → a curve, not
  a point).
- **VC\*** — median verifier-calls-to-first-solution (right-censored at budget; report
  with censoring rate).
- **Wall/GPU-s-to-solution.**
- **Compute-matched SolveRate** — every method gets the same total FLOPs incl. ML
  training/data-gen; compare. This is the fair ML-vs-classical number.
- **Optimality gap** — `found_size / m*` (needs B3 labels; ≥1).
- **Generalization gap** — SolveRate(train targets) − SolveRate(held-out).
- **Amortized break-even** — plot cumulative cost vs #targets for (ML train+infer) and
  (classical per-target); crossover point = headline for claim A.
- **Frontier reached** — max `m*`/size solved per method per block set (headline for F).

## 3. Fair-comparison rules (non-negotiable)
- identical benchmark, verifier, distance, budget unit, seeds across methods;
- baselines tuned (doc 03 checklist) before comparison;
- ML training cost always folded into compute-matched plots;
- headline claims live on extrapolation splits (S-size/S-shape/S-block) and/or above
  the B3 frontier — never on in-distribution small targets (B6 lookup solves those).

## 4. Statistics
- ≥5 seeds per (method, split); report median + IQR; paired comparisons across targets
  (same targets, different methods) with a paired test (Wilcoxon) on VC\* and a
  McNemar test on solved/unsolved; bootstrap CIs on SolveRate@N.

---

## 5. Hypotheses (falsifiable; each has metric + prediction + decision)

**H0 (baseline strength).** With the cheap exact verifier, tuned annealing (B2b) is a
*strong* baseline: on S-rand T0–T1 it solves ≫ uniform-random at matched verifier
calls.
*Metric:* SolveRate@N, VC\* B2b vs B1. *Predict:* B2b ≫ B1. *Decision:* if false, the
space/energy is mis-designed — fix before proceeding. (This guards against the
weak-baseline trap.)

**H1 (learnable signal).** Simulability carries structure beyond hand-coded invariants:
a GNN classifier (E1) predicts `m*≤k` with ΔAUC ≥ (preregister, e.g. +0.05) over
gradient-boosted hand-features, on S-rand.
*Decision:* pass → build E2; fail → pivot to E5 + search-acceleration only. This is the
cheapest, most decisive gate.

**H2 (sample-efficient synthesis).** Amortized `π` (E2) proposes solutions in fewer
verifier-calls than tuned annealing at matched budget, on S-rand.
*Metric:* VC\*, SolveRate@K, E2 vs B2b, paired. *Predict:* E2 < B2b VC\*. *Decision:*
pass → test generalization (H3); fail (annealing wins) → an interesting result about
cheap verifiers making search hard to beat.

**H3 (generalization — the crux).** E2's advantage persists on **held-out** targets and
**extrapolation** splits (S-size k→k+1, S-shape, S-block), not just S-rand.
*Metric:* SolveRate@K and generalization-gap on each split vs B2b and vs B6 oracle
(which cannot solve held-out/above-frontier). *Predict (optimistic):* positive but
decaying transfer; S-block hardest. *Decision:* pass on any extrapolation split ⇒
claim **G**; fail everywhere ⇒ E2 memorizes the reachable distribution (still a clean
boundary result).

**H4 (search acceleration).** Neural-guided search (E3) solves targets in fewer
verifier-calls / larger sizes than unguided UCT (B5) and reaches sizes beyond the B3
frontier, compute-matched.
*Metric:* VC\*, frontier-reached, E3 vs B5/B3. *Decision:* pass ⇒ claim **F**.

**H5 (frontier extension / discovery).** ML (E2 or E3) finds **verified** simulations
for targets B3 could not solve within the compute budget (above-frontier or previously
`UNKNOWN`).
*Metric:* count of verified new simulations; each is a standalone theorem (oracle-
checked). *Decision:* any hit ⇒ concrete mathematical payoff; zero hits with H3 passing
still supports amortization (claim A).

**H6 (amortization).** Across the full benchmark, total ML cost (data-gen + training +
inference) < total classical per-target cost beyond some benchmark size.
*Metric:* break-even plot. *Decision:* crossover exists ⇒ claim **A** (ML as a reusable
solver, not per-instance search).

**H7 (learned impossibility).** E5 produces valid, oracle-checked impossibility
certificates for TI-holdout targets, and certifies at least one `UNKNOWN` target as
impossible.
*Decision:* any new certificate ⇒ novel unsimulatability result.

**H-null (honest negative).** It is a real, citable outcome if H1/H2 fail: "the cheap
exact verifier makes symmetry-reduced search a baseline that current learned methods do
not beat, except on \<splits\>." Preregister this so a null is a result, not a
disappointment.

Preregister the thresholds (the `+0.05`, budgets `N,K`, seeds) in
`bench/vN/manifest.json` before running, so passes/fails are not post-hoc.
