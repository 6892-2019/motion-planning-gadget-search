# ML for gadget-simulation search — design notes

Detailed, implementation-ready notes for testing whether ML can help **find gadget
simulations** (and prove when none exist). Written to be buildable directly.

## Reading order
1. **[00-overview.md](00-overview.md)** — the plan: framing, why it's a good ML
   testbed, success criteria (G/F/A), experiment ladder E0→E5.
2. **[01-formalism-and-representations.md](01-formalism-and-representations.md)** —
   gadget/system/action-grammar reps and **canonicalization + symmetry** (the crux;
   read before writing any baseline).
3. **[02-simulator-and-equivalence.md](02-simulator-and-equivalence.md)** — the
   induced-gadget algorithm, verifier, behavioural distance, throughput budget,
   differential testing.
4. **[03-baselines.md](03-baselines.md)** — B1–B6 with pseudocode, tuning, and the
   fairness checklist. Strong baselines are the whole point.
5. **[04-benchmark-and-datasets.md](04-benchmark-and-datasets.md)** — block sets,
   target universe, `m*` labels, splits (incl. extrapolation), dataset schemas, HER.
6. **[05-ml-methods.md](05-ml-methods.md)** — E1 classifier, E2 amortized synthesis
   (pointer decoder + hindsight), E3 guided search, E4 RL, E5 impossibility
   certificates: architectures, tensors, losses, training.
7. **[06-protocol-metrics-hypotheses.md](06-protocol-metrics-hypotheses.md)** —
   evaluation runner, metrics, fairness rules, and the falsifiable hypotheses
   H0–H7 with decision gates.
8. **[07-codebase-and-milestones.md](07-codebase-and-milestones.md)** — package
   layout, frozen interfaces, dependencies, milestones M0–M5, first-PR checklist.

## The one thing to internalize
This is **synthesis with a cheap, exact verifier**. That gives unlimited perfectly
labelled data (sample wirings, read off what they induce) — the enabling advantage.
But an exhaustive search already solves the small cases, so the *only* interesting
wins are **generalization** to held-out/larger targets and **frontier extension**
beyond what exhaustive search reaches. Build strong classical baselines first
(doc 03), measure everything in **verifier-calls + compute-matched** cost (doc 06),
and re-verify every ML solution exactly.

## Status
Design notes only — no code yet. Start at milestone **M0** (doc 07 §4): the core
reps, `canon`, `induced`, and oracle-parity tests against the existing
`src/gadget_system.py`. Everything downstream depends on M0 passing.
