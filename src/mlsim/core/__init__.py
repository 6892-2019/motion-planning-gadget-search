"""mlsim.core — canonical gadget/construction representations and the simulator.

M0 layer (docs/ml/07 §4): correctness-first, pure Python, oracle-parity with the
existing ``src/gadget_system.py``. Fast/compiled variants come later.
"""
import mlsim  # noqa: F401  (ensures src/ is on sys.path)

from .gadget import Gadget, canon, canon_fixed, relabel_locs, is_reversible, is_dag
from .system import BlockSet, Construction
from .simulator import induced, verify, to_gadget_system
from .canonical import block_port_automorphisms, canon_sys, relabel
from .grammar import Action, apply, to_actions

__all__ = [
    "Gadget", "canon", "canon_fixed", "relabel_locs", "is_reversible", "is_dag",
    "BlockSet", "Construction",
    "induced", "verify", "to_gadget_system",
    "block_port_automorphisms", "canon_sys", "relabel",
    "Action", "apply", "to_actions",
]
