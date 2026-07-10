# mlsim — ML-assisted gadget-simulation search

Research code implementing the plan in [`docs/ml/`](../../docs/ml/). Lives under
`src/` so it can import the reference toolkit (`from gadget import Gadget`) directly.

## Status: **M0 complete** (core representations + oracle parity)

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
