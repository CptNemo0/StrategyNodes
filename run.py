"""Launch the backend (data_feed) and frontend (Vite) each in its own console window.

Each service runs in a separate cmd window so its stdout stays visible. The
windows use `cmd /k`, so they remain open after the process exits (handy for
inspecting build errors or crashes).

Lifetime: both child consoles are placed in a Windows *job object* configured to
kill everything in the job when the last handle to the job closes. This launcher
process holds that handle and blocks until you stop it. So closing the parent
terminal (which kills this script) tears down both child consoles automatically.

Usage:
    python run.py        # build debug backend, then run it + frontend dev server
    python run.py -r     # build/run the release backend instead
    python run.py -n     # skip the CMake build, just run the existing binary
    python run.py -b     # launch only the backend window
    python run.py -f     # launch only the frontend window
"""

from __future__ import annotations

import argparse
import ctypes
import subprocess
import sys
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATA_FEED_DIR = ROOT / "data_feed"
FRONTEND_DIR = ROOT / "frontend"

# --- Win32 job-object plumbing (ctypes; no third-party deps) -----------------

JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000
JobObjectExtendedLimitInformation = 9

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


class _BasicLimits(ctypes.Structure):
    _fields_ = [
        ("PerProcessUserTimeLimit", wintypes.LARGE_INTEGER),
        ("PerJobUserTimeLimit", wintypes.LARGE_INTEGER),
        ("LimitFlags", wintypes.DWORD),
        ("MinimumWorkingSetSize", ctypes.c_size_t),
        ("MaximumWorkingSetSize", ctypes.c_size_t),
        ("ActiveProcessLimit", wintypes.DWORD),
        ("Affinity", ctypes.c_size_t),
        ("PriorityClass", wintypes.DWORD),
        ("SchedulingClass", wintypes.DWORD),
    ]


class _IoCounters(ctypes.Structure):
    _fields_ = [
        (name, ctypes.c_ulonglong)
        for name in (
            "ReadOperationCount",
            "WriteOperationCount",
            "OtherOperationCount",
            "ReadTransferCount",
            "WriteTransferCount",
            "OtherTransferCount",
        )
    ]


class _ExtendedLimits(ctypes.Structure):
    _fields_ = [
        ("BasicLimitInformation", _BasicLimits),
        ("IoInfo", _IoCounters),
        ("ProcessMemoryLimit", ctypes.c_size_t),
        ("JobMemoryLimit", ctypes.c_size_t),
        ("PeakProcessMemoryUsed", ctypes.c_size_t),
        ("PeakJobMemoryUsed", ctypes.c_size_t),
    ]


def create_kill_on_close_job() -> wintypes.HANDLE:
    """Create a job object that kills all member processes when it is closed."""
    kernel32.CreateJobObjectW.restype = wintypes.HANDLE
    kernel32.CreateJobObjectW.argtypes = [wintypes.LPVOID, wintypes.LPCWSTR]
    job = kernel32.CreateJobObjectW(None, None)
    if not job:
        raise ctypes.WinError(ctypes.get_last_error())

    info = _ExtendedLimits()
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    kernel32.SetInformationJobObject.restype = wintypes.BOOL
    kernel32.SetInformationJobObject.argtypes = [
        wintypes.HANDLE,
        ctypes.c_int,
        wintypes.LPVOID,
        wintypes.DWORD,
    ]
    if not kernel32.SetInformationJobObject(
        job, JobObjectExtendedLimitInformation, ctypes.byref(info), ctypes.sizeof(info)
    ):
        raise ctypes.WinError(ctypes.get_last_error())
    return job


def assign_to_job(job: wintypes.HANDLE, proc: subprocess.Popen) -> None:
    kernel32.AssignProcessToJobObject.restype = wintypes.BOOL
    kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    if not kernel32.AssignProcessToJobObject(job, int(proc._handle)):  # pyright: ignore[reportAttributeAccessIssue]
        raise ctypes.WinError(ctypes.get_last_error())


# --- launching ---------------------------------------------------------------


def open_console(title: str, workdir: Path, command: str) -> subprocess.Popen:
    """Open a new console window running `command` in `workdir`.

    The command line is passed as a single raw string (not an argv list) so
    Python does not backslash-escape the embedded quotes — cmd cannot parse
    `\\"path\\"` and would fail with "The filename ... syntax is incorrect".
    `cmd /k` keeps the window alive after the command exits so output stays up.
    """
    inner = f'title {title} && cd /d "{workdir}" && {command}'
    return subprocess.Popen(
        f"cmd /k {inner}",
        creationflags=subprocess.CREATE_NEW_CONSOLE,
    )


def backend_command(config: str, build: bool) -> str:
    preset = "release" if config == "Release" else "debug"
    run = f'"{Path("build") / config / "data_feed.exe"}"'
    if not build:
        return run
    # Build first; only run the binary if the build succeeds.
    return f"cmake --build --preset {preset} && {run}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "-r", "--release", action="store_true", help="use the Release backend build"
    )
    parser.add_argument(
        "-n", "--no-build", action="store_true", help="skip building the backend"
    )
    parser.add_argument(
        "-b", "--backend-only", action="store_true", help="launch only the backend"
    )
    parser.add_argument(
        "-f", "--frontend-only", action="store_true", help="launch only the frontend"
    )
    args = parser.parse_args()

    if sys.platform != "win32":
        print(
            "This launcher targets Windows (uses cmd + job objects).", file=sys.stderr
        )
        return 1

    config = "Release" if args.release else "Debug"
    launch_backend = not args.frontend_only
    launch_frontend = not args.backend_only

    # Hold the job handle for the whole run: closing it (i.e. this process
    # dying) kills every child console. Do NOT close it early.
    job = create_kill_on_close_job()

    procs: list[subprocess.Popen] = []
    if launch_backend:
        cmd = backend_command(config, build=not args.no_build)
        print(f"Launching backend ({config}): {cmd}")
        backend = open_console("data_feed_backend", DATA_FEED_DIR, cmd)
        assign_to_job(job, backend)
        procs.append(backend)

    if launch_frontend:
        print("Launching frontend: npm run dev")
        frontend = open_console("frontend_vite", FRONTEND_DIR, "npm run dev")
        assign_to_job(job, frontend)
        procs.append(frontend)

    print(
        "Child consoles launched. Keep this window open; closing it stops both.\n"
        "(Ctrl+C here also stops them.)"
    )
    try:
        for proc in procs:
            proc.wait()
    except KeyboardInterrupt:
        pass  # falling out closes `job`, which kills the children.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
