# 02 — Simulator, verifier, distance, and equivalence

The simulator is the inner loop of **everything**: data generation, every baseline,
every reward. Its throughput sets the scale of the whole project. Budget it in
"verifier-seconds" and treat that as a first-class resource.

Reference implementation already exists: `src/gadget_system.py`
(`GadgetSystem.induced_gadget`, `verify_simulates`) and
`src/gadget_simulation.py` (`behaviorally_equivalent`). The job here is a **fast,
batched, differentially-tested** reimplementation plus a **distance** function.

---

## 1. `induced(construction) -> (G_ind, defects)`

**Config** = tuple of instance states, `c ∈ ∏_i Q_{t_i}`; config-space size
`C = ∏_i q_{t_i}`.

Algorithm (BFS over `(config, port)`, reference in `gadget_system._explore` /
`induced_gadget`):
```
components = partition of ports        # precomputed union-find
for each reachable config c (BFS at config level from c0):
  for each external target-port x with component Cx:
    frontier = {(c, p) : p ∈ Cx}       # agent enters, may wire-walk within Cx
    explore (config,port) via:
      traverse: at (c,(i,a)) with c[i]=s, for each (s,a,s',b)∈Δ(t_i):
                 -> (c[i:=s'], (i,b))
      wire:     at (c,p), for q in component(p): -> (c,q)
    whenever agent stands on a port in an external component Cy after ≥1 move:
      if y == x: if config changed -> record DEFECT; else ignore (no-op)
      else: record induced transition (state(c), x, state(c'), y)
G_ind states = reachable configs (discovery order); locations = target-ports.
```
Output the induced gadget (as §1 gadget rep) and the defect list.

**Complexity.** Per external entry, BFS is `O(C · P · maxdeg)`; total
`≈ O(C² · k · P)` in the worst case → **exponential in `n`** via `C`. Consequences:
- cheap for the interesting regime (`n ≲ 6`, `C ≲ few·10³`);
- *both* data gen and reward blow up exactly where ML is most interesting (large
  constructions) → cap `n`, and prefer methods that emit small constructions.

### 1.1 Fast implementation
- Backend: **Rust (pyo3)** or **numba/JAX**. Target **≥1e5 small-system inductions /
  s / core**; the pure-Python reference is ~1e3–1e4/s.
- Vectorize the config BFS: index configs by mixed-radix int; represent each
  instance's `Δ` as CSR; batch many independent constructions across cores.
- Early-out: stop and return `⊥` as soon as the partial induced gadget cannot match
  `T` (a mode appears that `T` lacks, under the fixed interface) — turns `verify`
  into a cheap reject for most random samples.

### 1.2 Differential testing (acceptance gate)
`test_simulator_vs_reference`: 1e5 random constructions over each block set; assert
`canon(fast.induced(c)) == canon(reference.induced(c))` and identical defect sets.
Fuzz sizes `n∈[1,6]`, include nondeterministic blocks (e.g. `seven`).

---

## 2. `verify(construction, T, iface) -> bool`

`= (defects == ∅) and behaviorally-equal(induced, T under iface)`.
Two modes:
- **fixed interface** (synthesis with a chosen target-port map): compare
  `canon_fixed(induced_relabelled) == canon_fixed(T)`.
- **free interface** (does *any* labelling work): compare `canon(induced) ==
  canon(T)`; if equal, recover the witnessing port map for reporting.

`verify` is the **oracle**; count every call. Exact — a positive is a real theorem.

---

## 3. Behavioural **distance** `d(G, T) ∈ [0,1]`  (shaped reward / MCMC energy)

Baselines #2 (annealing) and E4 (RL) need a smooth "how close is the induced gadget to
`T`". Provide several; pick by ablation.

- **`d_mode`** (cheap, default): over the best interface alignment (greedy or, for
  `k≤5`, exact over `k!`), `1 − |modes(G)∩modes(T)| / |modes(G)∪modes(T)|` on the
  closure-level traversal modes (Jaccard on `(s,a,s',b)` after a state correspondence
  found by matching per-state canonical languages). Fast, but ignores sequential
  structure.
- **`d_lang`** (faithful): symmetric difference of the two languages truncated to
  words of length `≤ L` (from the determinized-minimized DFAs), normalized. Captures
  the *iff* that `d_mode` misses; `O(|Σ|^L)`, keep `L≈3–4`.
- **`d_spec`** (structural): normalized edit/graph distance between the canonical
  minimal automata.

Rules: `d(G,T)=0 ⇔ verify` (with defects treated as `d=1`). Report which distance was
used; never let a shaped reward award credit to a defected construction.

---

## 4. `canon` / `canon_fixed`  (see 01 §1.1)

Extend `behaviorally_equivalent` to emit bytes keys. Add an LRU cache keyed by the raw
edge list. This is called once per induced gadget in data gen (millions of times) so
it must be fast; cache determinize/minimize results per subset.

---

## 5. Throughput budget (worked numbers, revise after E0)

| item | rate (per core) | cost for 1e7 |
|---|---|---|
| sample construction | 1e6/s | 10 s |
| `induced` (n≤6) | 1e5/s (target) | ~100 s |
| `canon` induced | 3e5/s (cached) | ~30 s |
| `d_mode` | 1e6/s | 10 s |

So a 1e7-example forward corpus is **minutes on a multicore box** once the Rust/numba
simulator lands. If the simulator stays pure-Python (~1e4/s), it's ~15 min–hours —
usable for E0/E1, too slow for E4. **The compiled simulator is the gating engineering
task; do it first.**
