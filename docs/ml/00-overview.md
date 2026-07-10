# Can ML help find gadget simulations? — an experiment plan

> Draft research plan. Deliberately not constrained by the current repo design;
> the existing Python layer is a starting point, not a requirement.

## 0. The one-paragraph version

Finding a simulation is **program synthesis with a cheap, exact verifier**: given a
target gadget `T` and a set of building blocks `B`, find a wiring of `B`-instances
whose *induced* gadget equals `T`. The verifier (`induced_gadget` + behavioural
equivalence) is exact and fast, and the *forward* map (wiring → induced gadget) is
cheap, so we can manufacture unlimited supervised data by sampling wirings and
reading off what they induce. That makes this an unusually clean ML testbed. But it
also raises the bar: for small sizes an **exhaustive search already enumerates every
simulation**, so ML is only interesting if it (a) **generalizes** to targets/sizes
outside the enumerated set, or (b) **accelerates search** past the exhaustive
frontier. The plan below builds strong classical baselines first, then tests ML in
escalating order, always measuring cost in **verifier-calls and compute**, and
always testing **generalization**, not memorization.

---

## 1. Problem definition

**Objects.** A *gadget* is a finite state/location transition system (states `Q`,
locations/ports `L`, transitions `Q×L → Q×L`, possibly nondeterministic). A
*system* is a multiset of gadget instances drawn from a block set `B`, a partition
of their ports into wire-components (junctions), and a labelling of some ports as
external. Its *induced gadget* is the gadget seen from the external ports (compute
by exploring the joint-configuration graph). A system **simulates** `T` iff its
induced gadget is behaviourally equivalent to `T` under some external-port↔`T`-port
map, with no same-port "defect" traversals (the one-player-simulation *iff*).

**Task (synthesis).** Given `(T, B, budget)`, output a system that simulates `T`,
or report/none. Optionally minimize system size.

**Verifier (oracle).** `verify(system, T) ∈ {0,1}` is exact. Cost grows with system
size (the joint-config space is `∏ |Q_i|`), so verifying *small* candidates is cheap
(ms) and verifying large ones is expensive — an important asymmetry.

**Why it's hard.** The wiring space is set-partitions of `n·p` ports (Bell numbers)
× external choices × label maps: `n=2,p=2 → 15`; `n=4,p=4 → ~10^10`;
super-exponential. Exhaustive enumeration with canonicalization (the current C++
search) is complete only up to small sizes.

**What "ML helps" would mean (success criteria).** At least one of:
- **G (generalize):** solve *held-out* targets (unseen in training) at a given
  verifier budget better than the strongest classical baseline at the same budget.
- **F (frontier):** find simulations of sizes/targets the exhaustive search cannot
  reach in comparable compute (verified exact — so any hit is a real theorem).
- **A (amortize):** across a benchmark of many targets, ML total cost
  (training + inference) beats running the classical search per target.

Anything that only reproduces the enumerable set at small size is **not** a win —
the exhaustive database is already a perfect lookup there.

---

## 2. Search-space & difficulty parameterization

Knobs (fix a grid for all experiments):
- block set `B` (see §5 benchmark),
- max instances `n`, ports/instance `p`, external ports `k = |L(T)|`,
- target size: states `|Q(T)|`, ports `k`, and **min-construction-size** `m*(T)`
  (from exhaustive/BFS, when known).

Difficulty tiers by `m*`: **T0** `m*≤2`, **T1** `3–4`, **T2** `5–6`, **T3** `>6 or
beyond-BFS`, **TI** provably impossible (via invariants/exhaustion). Report every
metric per tier.

---

## 3. Baselines (these are the bar; make them strong)

Weak baselines flatter ML; the user explicitly wants strong ones.

1. **Uniform random wiring** — sample systems, verify. (Floor.)
2. **Symmetry-reduced random / MCMC** — sample canonical systems only (dedupe by
   port/instance automorphism); optionally hill-climb on behavioural distance to `T`.
   (Medium; this is what naive "search" really is.)
3. **Exhaustive BFS/IDDFS with canonicalization** — the gold standard and the source
   of `m*` labels and the UNSAT frontier. This is essentially the existing C++
   search; reuse or re-implement cleanly. Gives *optimal* solutions where it finishes.
4. **SAT / CP / ILP encoding of "∃ system of size ≤ k simulating T"** — a strong
   *classical learned-free* baseline. Modern SAT/CP is a serious competitor; if ML
   can't beat a good CP encoding, that's an important negative result.
5. **UCT / MCTS without learning** — search baseline for the guided-search experiments,
   so any neural-guided win is attributable to *learning*, not to search machinery.
6. **The exhaustive database as an oracle** — for in-distribution small targets, a
   lookup table is the true competitor. ML must be compared against it explicitly to
   avoid claiming credit for solvable-by-lookup cases.

---

## 4. Metrics & efficiency protocol

Report **all** of:
- **SolveRate@N** — fraction of benchmark targets solved within `N` verifier calls.
- **VC\*** — median verifier-calls-to-first-solution (right-censored at budget).
- **Wall/GPU-seconds-to-solution** on fixed hardware (report the hardware).
- **Compute-matched solve rate** — give every method the same total FLOPs (incl.
  ML training) and compare solve rate. This is the fair ML-vs-classical comparison.
- **Optimality gap** — `found_size / m*` (needs BFS labels).
- **Generalization gap** — train-target vs held-out-target solve rate.
- **Amortized break-even** — benchmark size at which (ML train + infer) < (classical
  per-target). Plot cost vs #targets; the crossover is the headline number for claim A.

Primary hardware-independent currency = **verifier calls** (the shared unit of work);
secondary = wall-clock/compute (report both, always with the ML training cost folded
in for compute-matched plots).

---

## 5. Benchmark suite (fixed, versioned)

- **Block sets:** `B1={toggle}`, `B2={dicrumbler}`, `B3={2-toggle}`,
  `B4={toggle,dicrumbler}`, `B5=` a small mixed "expressive" set. Directed vs
  reversible blocks give different reachable classes (a key covariate).
- **Targets:** enumerate all canonical gadgets up to `(|Q|≤S, ports≤P)` for small
  `S,P` (e.g. 4×4), tag each with `m*` (BFS) or **impossible** (invariant/exhaustion)
  or **unknown**.
- **Splits (define up front — this is where the science is):**
  - **S-rand:** random target split (in-distribution generalization).
  - **S-size:** train on `m*≤k`, test on `m*=k+1` (**extrapolation in difficulty**).
  - **S-shape:** train on `|Q|,ports ≤ (s,p)`, test on strictly larger (**structural
    extrapolation**).
  - **S-block:** train on block set `B_i`, test on `B_j` (**transfer**).
- Ship the benchmark + labels as a versioned artifact so every method is scored
  identically.

---

## 6. ML experiments, in escalating order (each has a go/no-go gate)

### E0 — Infrastructure (prereq, ~1 week)
Fast, **batched** forward simulator (`induced_gadget`) and canonical-equivalence in
a compiled backend (Rust/C++/JAX/numba), target ≥1e5 systems/s/core; the current
pure-Python layer is the reference implementation to validate against (differential
testing). Data engine that samples canonical systems and emits `(system → induced
gadget)` with dedup. **Deliverable:** benchmark + labels + baselines #1–#4 running.
*Learn:* the classical difficulty frontier and cost curves — the numbers ML must beat.

### E1 — Is there learnable signal? (cheap probe, ~3–5 days)
Train a small GNN classifier `f(T, B, k) → P(∃ simulation of size ≤ k)` on
BFS/SAT labels. Compare AUC to the base rate and to cheap hand-features
(reversible?, DAG?, #states, #ports, invariant checks).
**Gate:** if a small model can't beat base-rate + hand-features by a clear margin,
the structure is weak → deprioritize heavy ML (still a publishable negative result).
*Learn:* whether simulability is ML-predictable at all, and which features carry it.

### E2 — Amortized synthesis (the main event, ~2–3 weeks)
**Generate-and-invert supervised learning.** Sample systems → induced gadgets;
train `p(system | T, B)` (target encoded by GNN or canonical tensor; system decoded
as an action sequence: `ADD_INSTANCE / CONNECT / EXPOSE / STOP`). At test time,
sample `K` systems from the model and verify each.
- **Baselines:** random (#1), symmetry-reduced sampling (#2), and — crucially — the
  same budget `K` of verifier calls given to each.
- **Metrics:** SolveRate@K and VC\* on **held-out** targets (all four splits).
- **Why it should work:** the cheap exact forward model gives unlimited perfectly
  labelled data, sidestepping RL exploration. This is the highest-probability win.
**Gate:** beats symmetry-reduced random at matched `K` on S-rand → proceed to test
S-size/S-shape/S-block (the real prize is extrapolation).
*Learn:* whether learned proposals are more sample-efficient than blind search, and
whether they **generalize** off the training distribution of targets.

### E3 — Neural-guided search (~2 weeks, if E2 promising)
Policy+value GNN over partial systems guiding **best-first search / MCTS**; reward
from the verifier. Compare verifier-calls-to-solution against unguided UCT (#5),
BFS (#3), and CP (#4), **compute-matched**.
*Learn:* whether learned guidance beats classical search *on the same budget* —
the cleanest "search acceleration" claim (F).

### E4 — RL from scratch (~2–3 weeks, only if E2/E3 leave a gap)
PPO/AlphaZero-style agent builds systems incrementally; **shaped reward** = decrease
in behavioural distance between the partial induced gadget and `T` (behavioural
distance = normalized language/trace edit distance from the canonicalization). The
cheap verifier makes millions of episodes affordable.
*Learn:* whether end-to-end RL discovers constructions that supervised amortization
misses (e.g. rare, large, or counterintuitive wirings) — upside on claim F.

### E5 — (Optional, complementary) Learning *impossibility* certificates (~2 weeks)
"Help find simulations" includes deciding when none exists. Train a model to
**propose composition invariants** (monoid homomorphisms / potential functions in
the gizmo framework) that certify non-simulability, generalizing beyond the two
hand-coded invariants (reversibility, DAG). Verify each proposed certificate exactly
(cheap). *Learn:* whether ML can conjecture new unsimulatability arguments — high
mathematical value, and it prunes the search for the other tracks.

---

## 7. What we learn, decision-gated

| Phase | Question answered | Go/no-go |
|---|---|---|
| E0 | What's the classical frontier & cost? | (always) |
| E1 | Is simulability learnable at all? | signal ≫ base rate → continue |
| E2 | Does amortized synthesis beat blind search per verifier-call? Does it generalize? | beats sym-random on held-out → E3 |
| E3 | Does learned guidance beat classical *search* compute-matched? | yes → strong claim F |
| E4 | Does RL find what supervision can't? | (upside only) |
| E5 | Can ML conjecture impossibility proofs? | (independent track) |

A negative at E1/E2 is itself a clean, citable result ("the cheap exact verifier
makes symmetry-reduced random search a strong baseline that learned proposals do not
beat"), because the baselines are strong.

---

## 8. Resource budget (order-of-magnitude, single-GPU project)

Gadgets are tiny; this is a **workstation + one GPU**, weeks-not-months effort.
- **Data gen:** 1e6–1e7 `(system,induced)` pairs → minutes–1 h on a multicore CPU
  once the compiled simulator hits ≥1e5/s.
- **BFS/SAT baselines:** CPU-hours to a few CPU-days for the full benchmark.
- **Models:** GNN/transformer 1–20 M params; 1e6–1e7 examples; **hours/run** on one
  modern GPU; a dozen runs → GPU-days.
- **RL (E4):** 1e6–1e8 env steps; verifier is cheap so CPU-worker-bound; a few
  GPU-days with parallel simulators.
- **Total:** ~2–6 researcher-weeks for the E0→E3 arc; go/no-go by end of week ~2.
  No cluster required; the binding constraints are the compiled simulator's
  throughput and the exhaustive-search frontier, not GPU capacity.

**Scaling caveat.** Verifier cost is exponential in system size (joint configs), so
both data gen and reward get expensive exactly where ML is most interesting (large
constructions). Cap system size; treat "verifier seconds" as a first-class budget;
prefer methods that propose *small* systems (cheap to verify).

---

## 9. Risks, confounds, and controls

- **Weak-baseline trap.** With such a cheap verifier, symmetry-reduced random search
  is strong. *Control:* baselines #2–#5 must be tuned before any ML claim; report
  matched-budget curves, not single points.
- **Memorization vs generalization.** In-distribution small targets are solvable by
  the exhaustive DB. *Control:* headline metrics are on S-size/S-shape/S-block
  (extrapolation), and always compared against the DB-as-oracle.
- **Symmetry leakage.** Un-canonicalized action spaces let a model "win" by exploiting
  a redundant baseline. *Control:* canonicalize systems and search states everywhere;
  give baselines the same canonicalization.
- **Target distribution gaming.** Solve-rate depends on the target mix. *Control:*
  fixed, versioned, tier-stratified benchmark; report per-tier.
- **Verifier as bottleneck at scale.** *Control:* budget in verifier-seconds; measure
  proposed-system-size distribution.
- **Reward sparsity (E4).** Most systems don't simulate `T`. *Control:* shaped
  behavioural-distance reward; curriculum by `m*`; or skip E4 if E2/E3 suffice.
- **"Found a new simulation" claims.** *Control:* every reported simulation is
  verified exactly by the oracle → no false positives possible; a frontier hit is a
  genuine theorem.

---

## 10. Concrete first two weeks (decisive, cheap)

1. Compiled batched `induced_gadget` + equivalence; differential-test against the
   Python reference (E0).
2. Fixed benchmark: enumerate targets ≤4×4, label with BFS `m*` / invariant-impossible;
   implement baselines #1–#4.
3. Plot the classical cost frontier (SolveRate@N, VC\* by tier) — the bar.
4. E1 learnability probe (GNN SAT/UNSAT classifier vs base-rate + hand-features).
5. **Decision:** signal present → build E2 amortized synthesis; signal absent →
   write up the strong-baseline negative result and stop.

---

## 11. Deliverables

- Versioned benchmark (targets, labels, splits) + compiled verifier.
- Baseline results table (§3) with cost curves.
- E1 classifier + learnability report.
- E2 synthesis model + held-out generalization table (four splits).
- If reached: E3 guided-search comparison; any **new verified simulations** beyond
  the exhaustive frontier (each a standalone result); E5 conjectured impossibility
  certificates.
