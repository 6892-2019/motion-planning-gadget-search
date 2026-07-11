# mlsim — ML-assisted gadget-simulation search

Research code implementing the plan in [`docs/ml/`](../../docs/ml/). Lives under
`src/` so it can import the reference toolkit (`from gadget import Gadget`) directly.

## Status: **M0 + M1 baselines** (core, resumable runs, exhaustive labels, B1/B2)

- **M0** — core representations + oracle parity (below).
- **M1 so far** — resumable-run framework (`run/`), the exhaustive `m*` labeler
  (`bench/exhaustive.py`, baseline **B3**), the `Solver` protocol + resumable eval
  harness (`eval/`), and baselines **B1/B2a/B2b** (`baselines/`). First measured
  baseline frontier on 86 targets is in [`docs/ml/results-m1.md`](../../docs/ml/results-m1.md)
  (headline: canonical-random ≈ 0.52 solve-rate is the current bar; the annealer needs
  a better distance — next task). **Next:** faster/compiled simulator, a better `d_lang`
  distance + tuned annealing, then the E1 learnability probe.

All experiments are **pause/restart-safe** (Ctrl-C, laptop sleep, hard kill): re-run
the same command to resume. See `run/checkpoint.py`.

### M0 core (representations + oracle parity)

`mlsim.core` provides the correctness-first, pure-Python foundation everything else
depends on:

| module | what |
|---|---|
| `core/gadget.py` | `canon` / `canon_fixed` byte keys for gadget identity |
| `core/system.py` | `BlockSet`, `Construction` (instances + port partition + interface) |
| `core/simulator.py` | `induced` (the induced gadget), `verify`, `to_gadget_system` |
| `core/canonical.py` | `block_port_automorphisms`, `canon_sys`, `relabel` |
| `core/grammar.py` | LINK action grammar: `apply` / `to_actions` |

The M0 acceptance gate (`src/test_mlsim.py`) checks the five invariants from
[`docs/ml/01 §6`](../../docs/ml/01-formalism-and-representations.md): canon ⇔
behavioural equivalence; **the simulator agrees with the reference
`gadget_system.GadgetSystem`** on random constructions (differential test); `induced`
and `canon_sys` are symmetry-invariant; and the grammar round-trips.

```bash
# from the repo root
python3 -m unittest discover -s src -p 'test_mlsim.py' -v   # M0 tests (~5s)
python3 src/mlsim/soak.py 20000                             # heavier differential soak
```

## Next: M1 (docs/ml/07 §4)
Fast/compiled `induced`, the target-gadget benchmark + `m*` labels + splits, the
`Solver` protocol, and baselines B1–B3. Then the E1 learnability probe (the go/no-go
gate for heavy ML).
