"""mlsim.baselines — classical (learning-free) solvers. These are the bar every ML
method must beat at matched budget (docs/ml/03). Keep them strong."""
from .random_search import RandomSolver, CanonicalRandomSolver
from .anneal import AnnealSolver

__all__ = ["RandomSolver", "CanonicalRandomSolver", "AnnealSolver"]
