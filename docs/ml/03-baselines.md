# 03 — Baselines (make them genuinely strong)

The whole project hinges on these. With a cheap exact verifier, **naive** search is
already decent; if we compare ML only to uniform-random we will "win" meaninglessly.
Every ML claim is measured against the *strongest* baseline at **matched budget**
(verifier-calls and compute). Build and tune #2–#4 *before* touching ML.

Shared harness (`eval/protocol.py`): every baseline and ML method implements
```
solve(target, blocks, budget) -> Result(solved: bool, construction|None,
                                         verifier_calls: int, wall_s: float,
                                         found_size: int|None, trace: list)
```
Budget is given in **verifier-calls** (primary) and a wall-clock cap. Fixed seeds,
fixed benchmark, per-tier reporting.

---

## B1 — Uniform random  (floor)
Sample `n ~ P(n)` (e.g. geometric truncated at `n_max`), sample types iid from `B`,
sample a partition of ports via a random restricted-growth string, sample an
injective target-port→component map; `verify`; repeat until solved or budget.
- **Purpose:** absolute floor and a sanity check that the space is non-degenerate.
- **Do not** let this be the headline comparison.

## B2 — Symmetry-reduced sampling + simulated annealing  (the strong "dumb" search)
This is the baseline ML must beat. Two variants, report both:

**B2a canonical rejection sampling:** as B1 but `canon_sys`-dedup with a seen-set;
each distinct canonical construction verified once. Removes the exponential
double-counting that makes B1 look worse than it is.

**B2b annealing / MCMC** (usually strongest classical without heavy machinery):
```
c = small random construction
E = d(induced(c), T)                     # behavioural distance, doc 02 §3
for step in budget:
  c' = neighbour(c)                      # one local move (below)
  E' = d(induced(c'), T)
  if E'==0: return c'                    # verify() is exact here
  accept c' with prob min(1, exp(-(E'-E)/temp))    # temp on a schedule
  periodically: restart from best-so-far / random (basin hopping)
```
Neighbourhood moves (all `canon_sys`-normalised after applying):
`ADD instance`, `REMOVE instance`, `MERGE two components`, `SPLIT a component`,
`MOVE a port between components`, `re-EXPOSE a target-port`. Keep a tabu set of recent
canonical forms.
- **Tuning (must do, log it):** geometric temp schedule `T_k = T_0·α^k`; grid
  `T_0 ∈ {.05,.1,.3}`, `α ∈ {.99,.999}`, restart period; move-type weights. Report the
  *tuned* configuration; an untuned annealer is a fake-strong baseline.
- **Distance choice** (`d_mode` vs `d_lang`) is itself an ablation — a good distance is
  most of the strength here.

## B3 — Exhaustive BFS/IDDFS with canonicalization  (GOLD; also the labeller)
Enumerate constructions in nondecreasing size, **canonical forms only**, verify each,
and record `induced → min construction`. This simultaneously (a) solves optimally
below the frontier, (b) produces the `m*(T)` labels, (c) certifies **impossible**
(when combined with invariant pruning and exhaustion to the size cap), (d) defines the
**frontier** (largest size reachable in the compute budget).
```
seen = ∅; queue = canonical constructions of size 1
for size = 1..cap:
  for c in orbit_reps(size, B):          # canonical generation (doc 01 §3,5)
    if pruned(c): continue               # invariant + partial-induced pruning
    G = induced(c); verify/record canon(G) -> min(size)
```
- **Canonical generation** via the LINK grammar with the canonical action order
  (doc 01 §5) so isomorphs are never generated (orderly generation), *not*
  generate-then-filter.
- **Pruning:** drop `c` if `refute_simulation`-style invariants already rule out every
  target of interest; drop partial constructions whose induced modes can't extend to
  any target.
- **Reuse the C++ search:** the thesis' `toggles-*` pipeline already does exhaustive
  canonical gadget combination with LMDB dedup. Where feasible, **bridge to it** (call
  the binary / read its DB) as B3 rather than reimplementing — it is a mature, fast,
  correct gold standard. Reimplement in-process only for tight ML-loop integration
  (E3 needs the same canonicalization the net sees).
- **Frontier is the headline for claim F:** the size at which B3 exhausts the compute
  budget is exactly what ML must exceed.

## B4 — SAT / CP / ILP  (strong classical, high effort — stretch)
"∃ construction with ≤ n instances of given types whose induced gadget ≡ T?"
The hard part is encoding **induced ≡ T** (a reachability/behavioural constraint), not
the wiring. Recommended staging:
- **CP-SAT with the simulator as a propagator / lazy clause** (CP-SAT or a custom
  DPLL(T)): decision vars = wiring (`same_component[p,q]`) + interface; a theory
  propagator runs `induced` on the partial assignment and blocks conflicts. Most
  practical; leans on our fast simulator.
- **Bounded ILP for the deterministic case:** fix a candidate config↔target-state
  correspondence (`≤ C·|Q_T|` guesses or a matching variable), then each target
  transition becomes a bounded agent-path existence constraint over the wiring;
  unroll paths to length `≤ Lpath`. Sound but incomplete unless `Lpath` ≥ diameter.
- **Priority:** implement only if B2/B3 leave a gap ML claims to fill; a good CP-SAT
  baseline is a serious competitor and a strong negative-result anchor. Mark as
  optional in the first pass; note the effort (1–2 wk).

## B5 — UCT / MCTS without learning  (search baseline for E3)
Plain UCT over the LINK action tree; random rollouts; terminal reward `1[verify]`,
optional shaped `−d` at cutoff. Identical action space, canonicalization, and budget
accounting as the *guided* version in E3 — so any E3 improvement is attributable to
**learning the prior/value**, not to MCTS itself. Tune `c_uct`, rollout depth,
progressive widening.

## B6 — Database-as-oracle  (the honesty check)
For every in-distribution target below the B3 frontier, a lookup in the B3/C++ DB is
an O(1) perfect solver. Report it explicitly so ML is never credited for cases a table
already solves. ML value lives strictly **outside** this oracle: held-out targets,
above-frontier sizes, or amortized total cost across many targets.

---

## Baseline tuning & fairness checklist (gate before any ML comparison)
- [ ] `canon_sys` dedup active in B1/B2/B3 (no isomorph double-counting).
- [ ] B2b annealer hyperparameters grid-searched and logged; best config frozen.
- [ ] B3 uses orderly generation (no generate-and-filter waste) and invariant pruning.
- [ ] All baselines share the exact verifier, distance, budget unit, and seeds.
- [ ] Per-tier (T0–T3, TI) curves produced: SolveRate@N, VC\*, wall-s.
- [ ] Frontier size for B3 recorded per block set (the bar for claim F).
- [ ] Compute of each baseline logged (verifier-calls + wall + FLOPs) for
      compute-matched ML comparison later.
Only after this checklist passes do baseline numbers count as "strong".
