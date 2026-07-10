# 01 — Formalism, representations, and canonicalization

This is the load-bearing document. Get the representations and the canonical forms
right and everything else (baselines, data gen, ML inputs, dedup) follows; get them
wrong and every baseline is secretly weak and every metric is inflated.

Conventions: `q = |Q|` states, `ℓ = |L|` ports. Ports/locations are entrances **and**
exits. Current repo model forbids `from_loc == to_loc`; we keep that (call it the
*tunnel* restriction) but flag every place it could be relaxed.

---

## 1. Gadget (the objects being simulated)

A gadget `G = (q, ℓ, Δ)` with transition relation
`Δ ⊆ Q × L × Q × L`, a transition `(s, a, s', b)` meaning "enter `a` in state `s`,
exit `b` in state `s'`" (`a ≠ b`). Not necessarily deterministic.

**In-memory reps** (keep all three; convert freely):
- **edge list**: sorted tuple of `(s,a,s',b)` — canonical for hashing raw structure.
- **dense tensor** `Δ[s,a,s',b] ∈ {0,1}`, shape `q×ℓ×q×ℓ` — for vectorized simulate
  and NN input (pad to `Qmax×Lmax`).
- **NFA-per-state view** `δ[s][(a,b)] = set(s')` — for determinize/minimize.

**Derived predicates** (all cheap, all invariants of behaviour):
- `deterministic`: ∀(s,a) at most one `(s',b)`.
- `reversible`: `(s,a,s',b) ∈ Δ ⇒ (s',b,s,a) ∈ Δ`.
- `dag`: state-graph `{s→s'}` acyclic (self-loops count).
- degree/mode histograms (features for E1).

### 1.1 Behavioural canonical form  `canon(G) → bytes`
The single most-used primitive: two gadgets are the *same gadget* iff `canon` matches
(up to a port relabelling we either fold in or expose). Algorithm (already prototyped
in `src/gadget_simulation.py:behaviorally_equivalent`; here we need a *key*, not a
boolean):

1. For each state `s`, compute the language `L_s` of executable `(a,b)` sequences via
   subset construction (determinize from `{s}`), minimize (partition refinement, all
   states accepting, missing edge = dead sink), canonically BFS-renumber → a string
   `key_s`. (This is exactly `gadget_simulation._canonical_language`.)
2. `canon_fixed(G) = ( ℓ, sorted **set** {key_s : s ∈ Q} )` — canonical **with ports
   fixed**. Note: a **set**, not a multiset, and **no raw `q`** — behaviourally
   indistinguishable states (equal `key_s`) collapse, exactly as
   `behaviorally_equivalent` uses `frozenset(...)`. `ℓ` **is** included (differing port
   counts are different gadgets, even if the extra ports are dead — you can still wire
   to them).
3. `canon(G) = min over port permutations π of canon_fixed(π·G)` — canonical **up to
   port relabelling**. For `ℓ ≤ ~5` brute-forcing `ℓ!` is fine (our targets are small);
   for larger `ℓ` replace the `ℓ!` loop with iterated port-colour refinement
   (nauty-style) seeded by per-port mode/degree signatures. Memoize on the raw edge list.

**Equality contract (the acceptance test):**
`canon(A) == canon(B) ⇔ behaviorally_equivalent(A, B)` and
`canon_fixed(A) == canon_fixed(B) ⇔ behaviorally_equivalent(A, B, fixed_locations=True)`.
Proof sketch: `canon_fixed` equal ⇔ equal per-state-language sets ⇔ fixed-loc
equivalence; `canon` mins over π, so equal ⇔ ∃π with equal sets ⇔ free equivalence.

Emit both: `canon_fixed` (ports have identity — the simulation interface) and `canon`
(abstract gadget identity, for enumeration/dedup). Store `canon` as the benchmark
**target id**.

> Correctness note: the set (not multiset-with-explicit-transition-structure) of
> per-state canonical languages determines the minimal automaton because residuals of
> `L_s` are themselves `L_{s'}` — see `reversibility-and-simulation` notes and the
> equivalence proof in `gadget_simulation.py`. Nondeterminism is handled by the subset
> construction inside step 1.

---

## 2. System / construction (the objects being searched)

A construction over block set `B = {b_0, …}` is:
- **instances** `I = [t_0, …, t_{n-1}]`, `t_i ∈ B` (a multiset of block types);
- global ports `P = { (i, a) : 0 ≤ i < n, 0 ≤ a < ℓ(t_i) }`, `|P| = Σ_i ℓ(t_i)`;
- **wiring** = a partition of `P` into *components* (junctions);
- **interface** = an injective map `x ↦ component`, from target-port indices
  `x ∈ {0,…,k-1}` to distinct components, `k = ℓ(T)`.

> **Key reduction (prove/assert in tests):** the induced gadget depends on the wiring
> **only through its component partition**, and on the interface **only through which
> component each target-port attaches to** — not through which specific port in a
> component is "the" external one, because wire-walk makes all ports in a component
> mutually reachable. So the canonical construction object is
> `(multiset I, partition of P, injective target-port→component)`. Represent the
> partition as a restricted-growth string `w[p] ∈ {0..W-1}`.

This reduction shrinks the space and removes a whole class of spurious symmetries; it
must be baked into every baseline and the action grammar, or baselines will
double-count and look artificially slow.

### 2.1 Well-formedness / canonical constraints (prune aggressively)
Enforce for every enumerated/sampled construction (cheap, huge pruning):
- no isolated instance (every instance reachable from an external component via
  gadget-traversal+wire, else it can't affect behaviour → drop it);
- every external component distinct; every target-port assigned;
- optional: no component is a single dangling non-external port (unless the block
  needs it) — dangling ports are allowed (see the 2-toggle→toggle example) but flag.

---

## 3. The symmetry group  (why baselines are strong or fake-strong)

Two constructions that induce the same gadget *and* are related by a symmetry must be
identified, or enumeration/random search wastes exponential effort on relabelled
copies and any ML "win" over them is meaningless. The group `Aut` acting on
constructions:

1. **Instance permutations** `∏_t S_{n_t}` — reorder same-type instances (permute `I`
   and the port indices accordingly).
2. **Per-instance block automorphisms** `∏_i Aut(t_i)` — each block type `t` has a
   port-automorphism group `Aut(t)` = permutations `σ` of its ports that (together
   with some state permutation) preserve `Δ(t)`; applying `σ` to instance `i` relabels
   its local ports. Precompute `Aut(t)` per block type once (`ℓ! · q!` brute force).
3. **Component relabelling** — components are unordered (already handled by
   restricted-growth normalisation).

`canon_sys(construction)` = lexicographically-minimal representative under (1)+(2),
after restricted-growth-normalising the partition. Two implementations:
- **Small-`n` brute force**: iterate the (small) group `∏ S_{n_t} × ∏ Aut(t_i)`, apply
  to ports, renormalise partition + interface, take the min tuple. Exact, simple,
  correct for the sizes we care about (`n ≲ 8`).
- **Graph canonical labelling** (scales better, also gives GNN input): build the
  coloured graph in §4 and canonicalise with **nauty/bliss** (`pynauty`), then read
  off a canonical key. Preferred once `n` grows.

Deliverable: `canonical.py` exposing `canon_sys(c) -> bytes` and
`orbit_reps(size, B) -> iterator` used by the exhaustive baseline and dedup.

---

## 4. Graph encoding  (shared by canonicalization and every GNN)

One heterogeneous graph per construction; used verbatim as canonicalization input and
(with learned embeddings) as GNN input:

Nodes (with integer colours):
- **instance** nodes, colour = block-type id;
- **port** nodes, colour = the port's *orbit id under `Aut(block)`* (so symmetric
  ports share a colour — critical for correct canonicalization);
- **component** nodes, colour = 0 (generic) or 1 (external);
- **target-port** nodes, colour = target-port index `x` (these carry the interface).

Edges (typed):
- instance —`has_port`— port;
- port —`in_component`— component;
- component —`exposes`— target-port.

For the ML encoder, also attach the **target gadget** as a separate small graph
(states+ports+transitions, §1) and let the decoder cross-attend to it (E2/E3). Node/
edge feature tensors and exact dtypes are specified in `05-ml-methods.md`.

---

## 5. Action grammar  (autoregressive synthesis, RL, MCTS branching)

A construction is generated by a token sequence. Two equivalent formulations; **pick
LINK as primary** (smaller branching, matches the C++ "combine" op), keep FREE for
ablation.

**LINK grammar** (build by gluing):
```
START(target T, blocks B)
loop:
  op = ADD t            # add an instance of block type t (its ports start isolated)
     | MERGE p q        # union the components of ports p and q      (p<q canonical)
     | EXPOSE p x       # attach target-port x to port p's component (x = next unassigned)
     | STOP
```
`p,q` are pointers into the *current* port set (dynamic vocabulary → pointer net).

**Canonical action order** (one canonical sequence per construction — essential for
supervised learning and to keep the search DAG a tree):
- `ADD`s in nondecreasing block-type id, before any `MERGE`/`EXPOSE` that uses the
  new instance;
- `MERGE p q` only with `p<q` and only if it changes the partition (no redundant
  merges); emit in lexicographic `(p,q)` order;
- `EXPOSE p x` with `x` = the smallest still-unassigned target-port; `p` = the
  lexicographically smallest port of its component.

Provide `to_actions(construction) -> canonical seq` and `apply(actions) ->
construction`; round-trip test `apply(to_actions(c)) == canon_sys(c)`.

Branching factor at each step = `|B|` (ADD) + `O(|P|²)` (MERGE) + `O(|P|)` (EXPOSE)
+ 1 (STOP); bounded by construction-size caps.

---

## 6. What must be true (invariants to assert in tests)

- `canon(induced(c))` is invariant under `canon_sys` (symmetry doesn't change
  behaviour).
- `induced` depends on wiring only via the component partition (§2 reduction).
- `apply ∘ to_actions = canon_sys` (grammar round-trips to canonical form).
- differential: compiled `induced` == `src/gadget_system.py` reference on 10⁵ random
  constructions (see `02-…`).
- `canon(G)` equality ⇔ `behaviorally_equivalent(G, ·)` True (cross-check the key
  against the existing boolean).

These five invariants are the acceptance tests for the core layer; nothing downstream
is trustworthy until they pass.
