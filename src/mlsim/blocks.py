"""Block-type registry shared by the labeler and the evaluators."""
from mlsim.core import Gadget

BLOCK_NAMES = ["toggle", "dicrumbler", "2-toggle", "seven"]


def make_block(name: str) -> Gadget:
    if name == "toggle":
        g = Gadget(2, 2); g.add_transition(0, 0, 1, 1); g.add_transition(1, 1, 0, 0)
    elif name == "dicrumbler":
        g = Gadget(2, 2); g.add_transition(0, 0, 1, 1)
    elif name == "2-toggle":
        g = Gadget(2, 4)
        for t in [(0, 0, 1, 1), (0, 2, 1, 3), (1, 1, 0, 0), (1, 3, 0, 2)]:
            g.add_transition(*t)
    elif name == "seven":
        g = Gadget(2, 2)
        for t in [(0, 0, 1, 1), (1, 1, 0, 0), (1, 0, 1, 1), (1, 1, 1, 0)]:
            g.add_transition(*t)
    else:
        raise SystemExit(f"[mlsim] unknown block {name!r} (known: {', '.join(BLOCK_NAMES)})")
    return g


def make_blockset(names):
    from mlsim.core import BlockSet
    return BlockSet([make_block(n) for n in names])
