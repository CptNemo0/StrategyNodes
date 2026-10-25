"""Summarize a stats file written by data_feed's timing::Aggregator.

Each line of the file is `[name] [nanoseconds]`; lines starting with `#` are
notes (the closing `# dropped [count]`). Prints, per name, the sample count and
the p50 / p99 / p99.9 / p99.99 latencies, using the nearest-rank method.

Usage:
    python timing_report.py timings.txt
    python timing_report.py timings.txt --unit us
"""

from __future__ import annotations

import argparse
import math
import sys
from collections import defaultdict
from pathlib import Path

PERCENTILES = (50.0, 99.0, 99.9, 99.99)
UNITS = {"ns": 1, "us": 1_000, "ms": 1_000_000}


def nearest_rank(sorted_values: list[int], percentile: float) -> int:
    """The smallest sample with at least `percentile`% of samples <= it."""
    rank = math.ceil(percentile / 100 * len(sorted_values))
    return sorted_values[max(rank, 1) - 1]


def read_samples(path: Path) -> tuple[dict[str, list[int]], list[str]]:
    samples: dict[str, list[int]] = defaultdict(list)
    notes: list[str] = []
    with path.open(encoding="utf-8-sig") as file:
        for number, line in enumerate(file, start=1):
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                notes.append(line.lstrip("# "))
                continue
            try:
                name, nanos = line.split()
                samples[name].append(int(nanos))
            except ValueError:
                print(f"{path}:{number}: skipping malformed line {line!r}",
                      file=sys.stderr)
    return samples, notes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("path", type=Path, help="stats file to summarize")
    parser.add_argument("--unit", choices=UNITS, default="ns",
                        help="unit to report latencies in (default: ns)")
    args = parser.parse_args()

    samples, notes = read_samples(args.path)
    if not samples:
        print(f"No samples in {args.path}", file=sys.stderr)
        return 1

    divisor = UNITS[args.unit]
    headers = ["interval", "count",
               *(f"p{p:g} [{args.unit}]" for p in PERCENTILES)]
    rows = []
    for name, values in sorted(samples.items()):
        values.sort()
        rows.append([name, str(len(values)),
                     *(f"{nearest_rank(values, p) / divisor:,.3f}".rstrip("0")
                       .rstrip(".") for p in PERCENTILES)])

    widths = [max(len(row[i]) for row in [headers, *rows])
              for i in range(len(headers))]
    print("  ".join(h.ljust(w) if i == 0 else h.rjust(w)
                    for i, (h, w) in enumerate(zip(headers, widths))))
    for row in rows:
        print("  ".join(c.ljust(w) if i == 0 else c.rjust(w)
                        for i, (c, w) in enumerate(zip(row, widths))))
    for note in notes:
        print(note)
    return 0


if __name__ == "__main__":
    sys.exit(main())
