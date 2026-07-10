# 04 — Benchmark, labels, splits, and datasets

Everything is scored on one **fixed, versioned** benchmark so methods are comparable.
Version it (`bench/v1/…`) and never mutate in place.

---

## 1. Block sets (`B`)
| id | blocks | character |
|---|---|---|
| B1 | `{toggle}` | reversible, 2-port |
| B2 | `{dicrumbler}` | directed/DAG, 2-port |
| B3 | `{2-toggle}` | reversible, 4-port, shared-state |
| B4 | `{toggle, dicrumbler}` | mixed reversibility |
| B5 | small "expressive" mix (e.g. `{toggle, dicrumbler, 2-toggle, seven}`) | for transfer |

Directed vs reversible blocks reach different classes of targets — a primary covariate.

## 2. Target universe
Enumerate **all canonical gadgets** (doc 01 §1.1 `canon`) with `|Q| ≤ S`, `ℓ ≤ P`,
deterministic **and** nondeterministic, for a small grid. Start `S=3, P=3` (small,
fully labelled), then `S=4, P=4` (labelled where B3 reaches). Record counts:
```
targets/vN/index.parquet:  target_id(canon bytes, hex), q, ell, deterministic,
                           reversible, dag, n_modes, degree_hist, ...
```
Report `|universe|` per `(S,P)`; expect it to grow fast — cap and document.

## 3. Labels (per `(target, block set)`)
From **B3** (exhaustive/C++) + invariants:
- `m_star ∈ ℕ` — minimum #instances of a simulating construction, if `≤ frontier`;
- `status ∈ {SIM(m*), IMPOSSIBLE, UNKNOWN}` — `IMPOSSIBLE` requires an invariant
  certificate (reversibility/DAG/…) **or** exhaustion proof to the size cap;
  `UNKNOWN` = solvable-or-not beyond the frontier.
- store a witnessing construction for `SIM`.
```
labels/vN/{Bid}.parquet:  target_id, status, m_star, witness(canon_sys bytes)
```

**Difficulty tiers:** T0 `m*≤2`, T1 `3–4`, T2 `5–6`, T3 `>6 or UNKNOWN-but-suspected`,
TI `IMPOSSIBLE`. Report every metric per tier; tiers are the axis of the science.

## 4. Splits (define once, enforce no leakage by `target_id`)
- **S-rand** — random 70/15/15 over `SIM` targets. In-distribution generalization.
- **S-size** — train `m*≤k`, val `=k`, test `=k+1` (per `k`). *Difficulty
  extrapolation* — the money split for "does it generalize beyond what it trained on".
- **S-shape** — train `q,ℓ ≤ (s,p)`, test strictly larger. *Structural extrapolation.*
- **S-block** — train on `B_i`, test on `B_j`. *Transfer across building blocks.*
- **TI-holdout** — impossible targets held out entirely (for E1 calibration and E5).

Leakage rule: a `target_id` (abstract `canon`) appears in exactly one of
train/val/test within a split; `S-block` also forbids sharing witness constructions.

## 5. Datasets derived for ML
All emitted by `bench/datagen.py` from the fast simulator; sharded parquet/npz.

### 5.1 Forward corpus (self-supervised, the engine of E2)
Sample canonical constructions (size ≤ cap) over `B`, compute induced gadget:
```
forward/vN/{Bid}.parquet:
  construction(canon_sys bytes), actions(canonical LINK seq, ints),
  induced_id(canon), induced_fixed(canon_fixed), size, defected(bool)
```
Dedup by `(construction)`; keep counts of how many constructions map to each
`induced_id` (the fibre size — useful as a difficulty proxy and for balanced
sampling). Target 1e6–1e7 rows per block set.

### 5.2 Synthesis pairs (E2 supervised, via **hindsight relabelling**)
Every non-defected sampled construction is a *correct* training example for **its own**
induced target — no sample wasted (HER). Build:
```
synth/vN/{Bid}.parquet:  target := induced_id, target_fixed, actions, size
```
For a given `target_id` there are many constructions; keep min-size + a sample of
larger ones (teaches multiple realizations). **Critically**, remove any `target_id`
in the test split of the split-under-eval before training (per-split dataset builds).

### 5.3 Classifier data (E1)
`(target_id, Bid, k) -> y ∈ {0,1}` where `y = 1[m*(target,B) ≤ k]`; balance across
tiers; include `IMPOSSIBLE` as hard negatives at all `k`. Hand-feature table shipped
alongside for the baseline.

## 6. Data-generation pipeline
```
sampler (doc01 §5, canonical order)  ->  fast induced (doc02)  ->  canon/canon_fixed
   ->  HER relabel  ->  shard writer  ->  per-split filter (drop test target_ids)
```
Determinism: seed everything; record sampler config + code hash in `vN/manifest.json`.
Throughput target from doc 02 §5 (1e7 in minutes with compiled simulator).

## 7. Distributional hazards (or metrics lie)
- **Reachability bias:** sampled targets over-represent easy gadgets (large fibres).
  Report solve-rate *stratified by tier and by fibre-size*, and evaluate on the
  *uniform-over-targets* benchmark, not the sampling distribution.
- **Train/test target overlap:** enforce by `canon` id; audit with a leakage test that
  intersects split id-sets (must be empty).
- **Impossible contamination:** never let `IMPOSSIBLE`/`UNKNOWN` targets leak into
  synthesis training as if solvable.
- **Block-set confound:** a target's `m*` differs across `B`; labels are always
  `(target, B)`-keyed.
