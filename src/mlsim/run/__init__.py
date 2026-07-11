"""mlsim.run — resumable-run infrastructure for long experiments.

Everything that takes real time (enumeration/labeling, data generation, baselines,
training) runs through this so it can be paused (Ctrl-C / laptop sleep), survives a
hard kill, and resumes from the last checkpoint. See ``checkpoint.py``.
"""
