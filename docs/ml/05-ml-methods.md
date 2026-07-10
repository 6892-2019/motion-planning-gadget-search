# 05 — ML methods (architectures, tensors, losses, training)

All models are small (≤20M params); single GPU. Shapes below assume caps
`Qmax=6, Lmax=8, n_max=8`; adjust in one config. Everything consumes the graph/tensor
reps from doc 01 and is scored by the protocol in doc 06.

Shared encoders (`ml/encoders.py`):
- **GadgetEncoder(G) → h_T ∈ R^{d}** and per-port `h_T^port ∈ R^{k×d}`. Input: the
  gadget graph (states, ports, transition edges typed by `(a,b)`), or the dense tensor
  `Δ[Qmax,Lmax,Qmax,Lmax]` through a small set-transformer. A relational GNN (R-GCN /
  GATv2, 3–5 layers, `d=128`) over states⊕ports with transition edges; mean+max pool →
  `h_T`; per-target-port readout → `h_T^port`. Must be **permutation-equivariant** over
  states and over ports (use the port-orbit colours from doc 01 §4 as input features so
  the net can *break* symmetry only via the interface, not spuriously).
- **SystemEncoder(partial c) → per-port/per-component embeddings** for the decoder /
  policy: heterogeneous GNN over the construction graph (doc 01 §4), 3–4 layers.

---

## E1 — Learnability probe (SAT/UNSAT classifier)
**Goal:** cheap go/no-go on whether simulability is learnable at all.
- Input: `GadgetEncoder(T) ⊕ onehot(B) ⊕ embed(k)`. Output: `σ(MLP) = P(m*(T,B) ≤ k)`.
- Loss: BCE; class-balanced across tiers; `IMPOSSIBLE` as `y=0` at all `k`.
- **Baselines to beat:** (i) base rate; (ii) logistic regression / gradient-boosted
  trees on hand-features (reversible, dag, `q`, `ℓ`, `n_modes`, degree hists, invariant
  flags, `k`). The interesting signal is *beyond* invariants.
- Metric: ROC-AUC and calibration, per tier; **ΔAUC over hand-features** is the gate.
- Compute: minutes–1 GPU-hour. Data: doc 04 §5.3.
- **Interpretation:** big ΔAUC ⇒ there is non-invariant structure the net sees ⇒
  proceed to E2. Near-zero ΔAUC ⇒ likely the invariants are ~all there is ⇒ a strong
  negative result; pivot effort to E5 (learning *new* invariants) and to search
  acceleration only.

---

## E2 — Amortized synthesis (primary method)
**Goal:** learn `π(actions | T, B)` so that sampling K constructions and verifying
beats blind search per verifier-call, and **generalizes** to held-out targets.

**Architecture** — conditional autoregressive **pointer** decoder:
- Condition on `h_T`, `h_T^port`, `onehot(B)`.
- At step `t`, `SystemEncoder(partial c_t)` gives port/component embeddings; the policy
  head factorises:
  `p(op) = softmax over {ADD_t, MERGE, EXPOSE, STOP}`; then op-specific pointers:
  `ADD` → choose `t∈B`; `MERGE` → two pointers over ports (bilinear scores over port
  embeddings, masked to `p<q` and to merges that change the partition); `EXPOSE` →
  pointer over ports × the next target-port `x` (cross-attend `h_T^port[x]`).
- Masks enforce the canonical action order (doc 01 §5) → the model only ever emits
  legal canonical sequences (shrinks the space it must learn).

**Training:**
1. **Supervised (teacher forcing)** on `synth/` pairs (doc 04 §5.2): maximize
   `log π(canonical_actions(c) | induced_id(c), B)`. Cross-entropy per token.
2. **Hindsight self-training loop** (optional, strong): sample from `π`, run `induced`,
   relabel each sample by its *own* induced target, add `(that target → actions)` to
   the pool, re-train. Infinite, on-policy, perfectly-labelled data; no reward sparsity.
   Guard against collapse to trivial targets by re-weighting toward rare `induced_id`
   (inverse fibre-size sampling).

**Inference / evaluation:** for a test target, sample/beam `K` constructions from `π`
(temperature `τ`), `verify` each, stop at first success.
- Metrics: **SolveRate@K** and **VC\*** on held-out targets, all four splits.
- **Baselines at matched K:** B2a (canonical random), B2b (annealer given K verifier
  calls). ML must beat B2b, not just B1.
- Compute: dataset 1e6–1e7; model 2–10M params; **hours/run**; a dozen runs → GPU-days.

**Why it should work / what kills it:** the cheap exact forward model removes the two
classic blockers (no labels, sparse reward). The risk is that annealing is *also* very
good because verification is cheap — hence matched-K comparison and the extrapolation
splits are the real test. If E2 wins on S-rand but not S-size/S-shape, the model
memorizes the reachable distribution but doesn't generalize — an informative,
publishable boundary.

---

## E3 — Neural-guided search
**Goal:** beat classical *search* (B3/B5) compute-matched → the clean "search
acceleration" claim (F).
- Policy prior = E2 `π`; value net `v(partial c, T) → P(completable to sim within
  remaining budget)`, trained on search outcomes (AlphaZero-style) or on B3 traces.
- Drive **best-first / MCTS** over LINK actions; expand by `π`, back up `v`, terminal
  reward from `verify`.
- **Compare against B5 (unguided UCT)** and **B3 (exhaustive)** at equal verifier-calls
  and equal wall/FLOPs. Report VC\*-to-solution and max solved size vs B3 frontier.
- Compute: GPU-days; verifier-call bound dominates, so this is CPU-simulator-bound.

## E4 — RL from scratch (only if E2/E3 leave a gap)
- MDP: state = partial `c`; actions = LINK grammar; terminal reward `1[verify]`; dense
  shaping `−Δ d(induced(partial), T)` (doc 02 §3), zeroed on defect.
- Algo: PPO (simple) or MuZero (uses a learned model; here the true model is cheap, so
  **AlphaZero > MuZero** — no need to learn dynamics). HER as in E2.
- Curriculum by `m*` (easy targets first). Many parallel CPU simulators + 1 GPU learner.
- Compute: 1e6–1e8 env steps; a few GPU-days.
- **Value over E2/E3:** only if it discovers constructions supervised amortization
  can't (rare/large/counterintuitive). Otherwise E2+E3 dominate on effort.

## E5 — Learning impossibility certificates (complementary track)
**Goal:** conjecture *new* composition-invariants that prove non-simulability,
generalizing beyond hand-coded reversibility/DAG. Turns "no simulation found" into
"provably none".
- **Certificate format (the exact checker, cheap):** a finite monoid `M` with a map
  `φ` from each block's *behaviour* (its gizmo) into `M`, such that (a) `φ` is a
  *simulation invariant* — preserved by the wiring/compose operations (check on the
  finite set of composition rules), and (b) `φ(T)` lies outside the submonoid generated
  by `{φ(b) : b∈B}`. A model proposes `(M, φ)`; the checker verifies (a)+(b) exactly.
- **Model/search:** enumerate/RL over small monoids (`|M|≤` a few); GNN scores
  candidate `φ`. Even a guided enumeration is valuable.
- **Payoff:** any accepted certificate is a genuine theorem and immediately prunes
  B2/B3/E2/E3 for a whole family of targets. High math value; more speculative.
- Ground truth: TI-holdout targets (known impossible) for validation; success = also
  certifying `UNKNOWN` targets as impossible.

---

## Cross-cutting implementation notes
- **Pointer/dynamic vocabulary** is the one non-standard piece (ports/components are
  created during decoding); reuse a GNN-per-step + bilinear pointer, or a Transformer
  over the running action sequence with segment embeddings identifying instances/ports.
- **Symmetry:** feed port-orbit colours; canonicalize supervised targets; consider
  averaging loss over the (few) canonical action sequences if you don't enforce a
  single one.
- **Determinism/repro:** seed; log code+data hashes; every reported solution is
  re-verified by the exact oracle (no trust in model outputs).
