#!/usr/bin/env python3
"""Run a baseline solver over a benchmark's SIM targets, measuring solve-rate and
verifier-calls (docs/ml/06). Resumable and signal-safe (mlsim.run).

    python3 src/mlsim/eval/run_baseline.py \
        --benchmark runs/ex-toggle-dicrumbler-n4 --solver anneal \
        --budget-calls 2000 --out runs/base-anneal
    python3 src/mlsim/eval/run_baseline.py --out runs/base-anneal --status
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from mlsim.baselines import AnnealSolver, CanonicalRandomSolver, RandomSolver
from mlsim.eval.protocol import run_baseline
from mlsim.run.checkpoint import install_signal_handlers, read_json

SOLVERS = {
    "random": RandomSolver,
    "canonical-random": CanonicalRandomSolver,
    "anneal": AnnealSolver,
}


def _print_metrics(out_dir):
    m = read_json(os.path.join(out_dir, "metrics.json"))
    if not m:
        print("[mlsim] no metrics yet")
        return
    print(f"[mlsim] {out_dir}: {m['num_solved']}/{m['num_targets']} solved "
          f"(rate {m['solve_rate']}), VC*={m['vc_star']}")
    print(f"  solve_rate_at_calls={m['solve_rate_at']}")
    print("  by m*-tier: " + "  ".join(
        f"m*={t}:{d['solved']}/{d['n']}(VC*={d['vc_star']})"
        for t, d in m["by_tier"].items()))


def main():
    import argparse
    p = argparse.ArgumentParser(description="Run a baseline solver over a benchmark.")
    p.add_argument("--out", required=True, help="run dir (resumes if it exists)")
    p.add_argument("--benchmark", help="benchmark run dir (with labels.json + manifest.json)")
    p.add_argument("--solver", choices=list(SOLVERS), default="anneal")
    p.add_argument("--budget-calls", type=int, default=2000)
    p.add_argument("--max-inst", type=int, default=6)
    p.add_argument("--seed", type=int, default=0)
    p.add_argument("--checkpoint-seconds", type=float, default=10.0)
    p.add_argument("--status", action="store_true")
    args = p.parse_args()

    if args.status:
        _print_metrics(args.out)
        return

    if not args.benchmark:
        raise SystemExit("[mlsim] --benchmark is required to start a run")
    bench_manifest = read_json(os.path.join(args.benchmark, "manifest.json"))
    if bench_manifest is None:
        raise SystemExit(f"[mlsim] no manifest.json in {args.benchmark!r}")
    blocks = bench_manifest["config"]["blocks"]

    identity = {
        "job": "baseline",
        "solver": args.solver,
        "blocks": blocks,
        "benchmark": bench_manifest["config_hash"],
        "budget_calls": args.budget_calls,
        "max_inst": args.max_inst,
        "seed": args.seed,
    }
    solver = SOLVERS[args.solver](max_inst=args.max_inst)

    resuming = os.path.exists(os.path.join(args.out, "manifest.json"))
    stop = install_signal_handlers()
    print(f"[mlsim] {'resuming' if resuming else 'starting'} baseline "
          f"'{args.solver}' over {args.benchmark} -> {args.out} "
          f"(budget {args.budget_calls} calls; Ctrl-C to pause)")
    status = run_baseline(identity, args.out, solver, args.benchmark,
                          seed=args.seed, checkpoint_seconds=args.checkpoint_seconds,
                          stop_flag=stop)
    if status == "paused":
        done = len(read_json(os.path.join(args.out, "results.json"), {}))
        print(f"[mlsim] paused & checkpointed ({done} targets done). Re-run to resume.")
    else:
        _print_metrics(args.out)


if __name__ == "__main__":
    main()
