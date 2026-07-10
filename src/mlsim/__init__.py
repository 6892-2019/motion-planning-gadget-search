"""mlsim — research code for ML-assisted gadget-simulation search.

See docs/ml/ for the design notes. This package sits under src/ so it can import
the reference toolkit (``from gadget import Gadget``) directly; the shim below puts
the parent ``src/`` directory on sys.path regardless of how mlsim is imported.
"""
import os as _os
import sys as _sys

_SRC = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))
if _SRC not in _sys.path:
    _sys.path.insert(0, _SRC)
