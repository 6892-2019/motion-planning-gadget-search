#!/usr/bin/env python3
"""Exhaustive enumeration + m* labeling  (baseline B3, the gold standard).

Enumerates canonical constructions in increasing number of instances, computes each
one's induced gadget, and records, for every reachable target gadget, the minimum
number of instances that realizes it (``m*``) plus a witness. This is the label
source for the benchmark (docs/ml/04 §3) and the exhaustive baseline (docs/ml/03 B3).

Resumable and laptop-safe (mlsim.run): pausing (Ctrl-C), a laptop sleep, or a hard
kill loses at most the work since the last periodic checkpoint. Re-running the same
command resumes. The labels map is idempotent (min over sizes), so reprocessing a few
candidates after a crash is harmless.

Restrictions (documented; relax later): all instances start in state 0; ``m*`` is the
minimum instance count within this construction model and block set.

    python3 src/mlsim/bench/exhaustive.py --out runs/ex1 --blocks toggle dicrumbler \
        --max-instances 4 --target-max-states 4 --target-max-ports 4
    python3 src/mlsim/bench/exhaustive.py --out runs/ex1 --status
"""
import os
import sys
import time
from collections import Counter
from itertools import combinations, combinations_with_replacement
from typing import Dict, Iterator, List, Optional

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from mlsim.blocks import make_block
from mlsim.core import (BlockSet, Construction, canon, canon_sys,
                        induced, minimized_shape, to_actions)
from mlsim.run.checkpoint import (RunDir, StopFlag, atomic_write_json,
                                  install_signal_handlers, read_json)


# ---- deterministic candidate enumeration ------------------------------------
def restricted_growth_strings(m: int) -> Iterator[tuple]:
    """All set partitions of ``range(m)`` as restricted-growth strings, in a fixed
    (lexicographic) order so candidate indices are stable across runs."""
    if m == 0:
        yield ()
        return

    def rec(i: int, mx: int, cur: List[int]):
        if i == m:
            yield tuple(cur)
            return
        for v in range(mx + 2):          # 0 .. mx+1
            cur.append(v)
            yield from rec(i + 1, v if v > mx else mx, cur)
            cur.pop()
    yield from rec(0, -1, [])


def generate_level(bs: BlockSet, n: int, pcap: int) -> Iterator[Construction]:
    """Every construction with exactly ``n`` instances (init all 0), in a fixed order.
    Isomorphic duplicates are removed by the caller via ``canon_sys``.

    The interface uses ``combinations`` (one target-port ordering per chosen set of
    components), not ``permutations``: reordering the exposed components just relabels
    the induced gadget's ports, and target identity (``canon``) is relabeling-invariant,
    so every reachable target is still found — at ~k! less work. The recorded witness
    therefore realizes *a* relabeling of the target (which is the same target)."""
    for M in combinations_with_replacement(range(len(bs)), n):
        nports = sum(bs.num_locations(t) for t in M)
        for wire in restricted_growth_strings(nports):
            comps = sorted(set(wire))
            for k in range(1, min(pcap, len(comps)) + 1):
                for iface in combinations(comps, k):
                    yield Construction(bs, list(M), list(wire), list(iface))


# ---- the job ----------------------------------------------------------------
def _process(c: Construction, labels: Dict[str, dict], scap: int, pcap: int) -> None:
    g, defects = induced(c)
    if defects:
        return
    q_min, ell = minimized_shape(g)
    if ell > pcap or q_min > scap:
        return
    key = canon(g).hex()
    n = len(c.instances)
    entry = labels.get(key)
    if entry is None or n < entry["min_size"]:
        labels[key] = {"min_size": n, "q": q_min, "ell": ell,
                       "witness": [list(a) for a in to_actions(c)]}


def _write_summary(rd: RunDir, labels: Dict[str, dict], identity: dict) -> dict:
    by_size = Counter(v["min_size"] for v in labels.values())
    by_shape = Counter((v["q"], v["ell"]) for v in labels.values())
    summary = {
        "num_targets": len(labels),
        "by_min_size": {str(s): c for s, c in sorted(by_size.items())},
        "by_shape": {f"{q}x{l}": c for (q, l), c in sorted(by_shape.items())},
        "identity": identity,
    }
    atomic_write_json(rd.file("summary.json"), summary)
    return summary


def run(identity: dict, run_dir: str, checkpoint_seconds: float = 5.0,
        stop_flag: Optional[StopFlag] = None, stop_after: Optional[int] = None) -> str:
    """Run (or resume) the exhaustive labeling. Returns 'done' or 'paused'."""
    bs = BlockSet([make_block(nm) for nm in identity["blocks"]])
    scap, pcap = identity["target_max_states"], identity["target_max_ports"]
    maxn = identity["max_instances"]

    rd = RunDir(run_dir, identity)
    labels: Dict[str, dict] = read_json(rd.file("labels.json"), {})
    progress = read_json(rd.file("progress.json"),
                         {"completed_levels": [], "level": 1, "cursor": 0})
    errors_path = rd.file("errors.log")

    def checkpoint():
        atomic_write_json(rd.file("labels.json"), labels)      # labels FIRST ...
        atomic_write_json(rd.file("progress.json"), progress)  # ... then progress

    processed = 0
    last = time.time()
    try:
        for n in range(1, maxn + 1):
            if n in progress["completed_levels"]:
                continue
            if progress["level"] != n:
                progress["level"], progress["cursor"] = n, 0
            cursor = progress["cursor"]
            seen = set()
            for idx, c in enumerate(generate_level(bs, n, pcap)):
                if idx < cursor:                       # fast-forward: rebuild dedup only
                    seen.add(canon_sys(c))
                    continue
                key = canon_sys(c)
                progress["cursor"] = idx + 1
                if key in seen:
                    continue
                seen.add(key)
                try:
                    _process(c, labels, scap, pcap)
                except Exception as e:                  # one bad candidate can't kill the run
                    with open(errors_path, "a") as f:
                        f.write(f"{c!r}\t{e!r}\n")
                processed += 1
                now = time.time()
                if now - last > checkpoint_seconds:
                    checkpoint()
                    last = now
                if (stop_flag is not None and stop_flag.stop) or \
                        (stop_after is not None and processed >= stop_after):
                    checkpoint()
                    return "paused"
            progress["completed_levels"].append(n)
            progress["level"], progress["cursor"] = n + 1, 0
            checkpoint()
        _write_summary(rd, labels, identity)
        checkpoint()
        return "done"
    finally:
        checkpoint()      # always leave a consistent checkpoint (incl. on exceptions)


# ---- CLI --------------------------------------------------------------------
def _identity_from_args(args) -> dict:
    return {
        "job": "exhaustive",
        "gen_version": 2,          # bump when the candidate enumeration order changes
        "blocks": list(args.blocks),
        "max_instances": args.max_instances,
        "target_max_states": args.target_max_states,
        "target_max_ports": args.target_max_ports,
    }


def main():
    import argparse
    p = argparse.ArgumentParser(description="Exhaustive gadget-simulation labeling (B3).")
    p.add_argument("--out", required=True, help="run directory (resumes if it exists)")
    p.add_argument("--blocks", nargs="+", default=["toggle", "dicrumbler"])
    p.add_argument("--max-instances", type=int, default=4)
    p.add_argument("--target-max-states", type=int, default=4)
    p.add_argument("--target-max-ports", type=int, default=4)
    p.add_argument("--checkpoint-seconds", type=float, default=5.0)
    p.add_argument("--status", action="store_true", help="print progress and exit")
    args = p.parse_args()

    if args.status:
        prog = read_json(os.path.join(args.out, "progress.json"))
        lab = read_json(os.path.join(args.out, "labels.json"), {})
        summ = read_json(os.path.join(args.out, "summary.json"))
        print(f"[mlsim] {args.out}: progress={prog}, targets_so_far={len(lab)}")
        if summ:
            print(f"[mlsim] DONE summary: {summ['num_targets']} targets, "
                  f"by_min_size={summ['by_min_size']}, by_shape={summ['by_shape']}")
        return

    identity = _identity_from_args(args)
    resuming = os.path.exists(os.path.join(args.out, "manifest.json"))
    stop = install_signal_handlers()
    print(f"[mlsim] {'resuming' if resuming else 'starting'} exhaustive run "
          f"at {args.out}  (Ctrl-C to pause & checkpoint)")
    status = run(identity, args.out, checkpoint_seconds=args.checkpoint_seconds,
                 stop_flag=stop)
    if status == "paused":
        prog = read_json(os.path.join(args.out, "progress.json"))
        print(f"[mlsim] paused & checkpointed at {prog}. Re-run the same command to resume.")
    else:
        summ = read_json(os.path.join(args.out, "summary.json"), {})
        print(f"[mlsim] DONE. {summ.get('num_targets')} targets  "
              f"by_min_size={summ.get('by_min_size')}  by_shape={summ.get('by_shape')}")


if __name__ == "__main__":
    main()
