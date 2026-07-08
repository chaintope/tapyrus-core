#!/usr/bin/env python3
# Copyright (c) 2025 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Maintain and chart the daily-test benchmark history.

The history CSV (one row appended per benchmark per daily-test run) has
columns: date,commit,benchmark,median

Two subcommands:
  append  Parse a bench_tapyrus ConsolePrinter output file (`# Benchmark,
          evals, iterations, total, min, max, median` header followed by one
          comma-separated row per benchmark) and append today's median for
          each benchmark to the history CSV.
  plot    Render one PNG with one line per benchmark, one point per row
          (normally one per day, since the history file gains rows only on
          scheduled/dispatched daily-test runs).
"""

import argparse
import csv
import sys
from collections import defaultdict


def append_results(bench_output_path, history_csv_path, date, commit):
    rows = []
    with open(bench_output_path, newline="") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = [field.strip() for field in line.split(",")]
            if len(fields) != 7:
                continue
            name, _evals, _iters, _total, _min, _max, median = fields
            rows.append((date, commit, name, median))

    if not rows:
        print("No benchmark result rows parsed from {}".format(bench_output_path), file=sys.stderr)
        return

    with open(history_csv_path, "a", newline="") as f:
        writer = csv.writer(f)
        writer.writerows(rows)


def load_history(csv_path):
    series = defaultdict(list)
    with open(csv_path, newline="") as f:
        for row in csv.reader(f):
            if len(row) != 4:
                continue
            date, commit, benchmark, median = row
            try:
                series[benchmark].append((date, float(median)))
            except ValueError:
                continue
    for points in series.values():
        points.sort(key=lambda p: p[0])
    return series


def plot(series, out_path, title):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(12, 6))
    for benchmark in sorted(series):
        points = series[benchmark]
        dates = [p[0] for p in points]
        medians = [p[1] for p in points]
        ax.plot(dates, medians, marker="o", markersize=3, linewidth=1, label=benchmark)

    ax.set_xlabel("Date")
    ax.set_ylabel("Median time per iteration (s)")
    ax.set_title(title)
    ax.set_yscale("log")
    ax.grid(True, which="both", axis="y", linestyle="--", alpha=0.4)
    fig.autofmt_xdate(rotation=45)
    ax.legend(fontsize="x-small", ncol=2, loc="upper left", bbox_to_anchor=(1.01, 1.0))
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    append_parser = subparsers.add_parser("append", help="Append today's medians to the history CSV")
    append_parser.add_argument("--bench-output", required=True, help="Path to bench_tapyrus console output")
    append_parser.add_argument("--csv", required=True, help="Path to the history CSV file")
    append_parser.add_argument("--date", required=True, help="Date to record (e.g. YYYY-MM-DD)")
    append_parser.add_argument("--commit", required=True, help="Commit SHA to record")

    plot_parser = subparsers.add_parser("plot", help="Render the per-benchmark trend chart")
    plot_parser.add_argument("--csv", required=True, help="Path to the history CSV file")
    plot_parser.add_argument("--out", required=True, help="Path to write the output PNG")
    plot_parser.add_argument("--title", default="Benchmark history", help="Chart title")

    args = parser.parse_args()

    if args.command == "append":
        append_results(args.bench_output, args.csv, args.date, args.commit)
        return 0

    series = load_history(args.csv)
    if not series:
        print("No benchmark history rows found; skipping chart generation.", file=sys.stderr)
        return 0

    plot(series, args.out, args.title)
    return 0


if __name__ == "__main__":
    sys.exit(main())
