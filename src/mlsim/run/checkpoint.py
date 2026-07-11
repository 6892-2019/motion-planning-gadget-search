"""Checkpointing primitives: atomic writes, config-pinned run directories, and
cooperative stop-on-signal. Designed for laptops — an interrupt, a sleep, or a hard
kill loses at most the work since the last periodic checkpoint, and never corrupts a
checkpoint file.
"""
import hashlib
import json
import os
import signal
import tempfile
import time
from typing import Any, Optional


def atomic_write_bytes(path: str, data: bytes) -> None:
    """Write ``data`` to ``path`` atomically (temp file + fsync + rename), so a crash
    mid-write leaves the previous good file intact."""
    d = os.path.dirname(path) or "."
    os.makedirs(d, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=d, prefix=".tmp-")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)   # atomic on POSIX
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def atomic_write_json(path: str, obj: Any) -> None:
    atomic_write_bytes(path, json.dumps(obj).encode())


def read_json(path: str, default: Any = None) -> Any:
    try:
        with open(path) as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return default


def config_hash(cfg: dict) -> str:
    return hashlib.sha1(json.dumps(cfg, sort_keys=True, default=str).encode()).hexdigest()[:12]


class StopFlag:
    """Set cooperatively by a signal handler; the run loop checks it and exits
    cleanly at the next safe point (after checkpointing)."""

    def __init__(self):
        self.stop = False
        self.signum: Optional[int] = None

    def request(self, signum=None, frame=None):
        self.stop = True
        self.signum = signum


def install_signal_handlers() -> StopFlag:
    """Turn SIGINT/SIGTERM into a cooperative stop request (first signal). A second
    signal restores the default handler so an impatient Ctrl-C still hard-kills."""
    flag = StopFlag()

    def handler(signum, frame):
        if flag.stop:                      # second signal -> hard default behaviour
            signal.signal(signum, signal.SIG_DFL)
            os.kill(os.getpid(), signum)
            return
        flag.request(signum)

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            signal.signal(sig, handler)
        except (ValueError, OSError):
            pass                           # e.g. not in main thread
    return flag


class RunDir:
    """A directory pinned to a config. Re-opening with the same config resumes;
    re-opening with a different config refuses (so you don't silently mix runs)."""

    def __init__(self, path: str, config: dict):
        self.path = path
        os.makedirs(path, exist_ok=True)
        self.manifest_path = os.path.join(path, "manifest.json")
        h = config_hash(config)
        existing = read_json(self.manifest_path)
        if existing is not None:
            if existing.get("config_hash") != h:
                raise SystemExit(
                    f"[mlsim] run dir {path!r} was created with a different config "
                    f"(hash {existing.get('config_hash')} != {h}). Use a fresh --out "
                    f"directory or delete the old one.")
            self.resumed = True
        else:
            atomic_write_json(self.manifest_path, {
                "config_hash": h, "config": config, "created": time.time()})
            self.resumed = False

    def file(self, name: str) -> str:
        return os.path.join(self.path, name)
