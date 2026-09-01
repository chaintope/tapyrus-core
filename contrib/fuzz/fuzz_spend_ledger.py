#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Shared monthly spend ledger for this repo's two fuzz-generation
pipelines: fuzz_script_generate_pool.py (via claude_model.py, real
per-call token usage) and fuzz_code_generate_and_draft.py (via
OSS-Fuzz-Gen, an external tool we don't instrument internally, so that
side is necessarily an ESTIMATE -- see that script's own comments). Both
draw from the SAME $50/month cap, not $50 each -- this is one line item
("AI spend on this repo's fuzz testing"), not two independently-budgeted
ones.

State lives in a plain key=value text file (gitignored -- local machine
state, not synced across machines or people; if multiple people run
these scripts, each gets their own independent monthly cap, this doesn't
coordinate across users). Resets automatically when the calendar month
changes. Location defaults to next to this file, but callers whose
working copy of this module has been copied elsewhere (fuzz_script_
generate_pool.sh copies claude_model.py plus this file into a temporary
Fuzz4All clone) should set FUZZ_SPEND_LEDGER_STATE to the persistent
path explicitly -- otherwise state would be written into that temp
directory and lost when it's cleaned up.

claude_model.py imports remaining_budget()/record_spend() directly.
fuzz_code_generate_and_draft.py shells out to this file's CLI
(`remaining` / `record`).
"""
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

MONTHLY_CAP_USD = 50.0


def _state_file() -> Path:
    override = os.environ.get("FUZZ_SPEND_LEDGER_STATE", "").strip()
    if override:
        return Path(override)
    return Path(__file__).parent / "fuzz_spend_ledger.state"


def _current_month() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m")


def _read_state() -> "tuple[str, float]":
    path = _state_file()
    if not path.exists():
        return _current_month(), 0.0
    values = {}
    for line in path.read_text().splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            values[k.strip()] = v.strip()
    month = values.get("month", "")
    try:
        spent = float(values.get("spent_usd", "0") or "0")
    except ValueError:
        spent = 0.0
    if month != _current_month():
        return _current_month(), 0.0
    return month, spent


def _write_state(month: str, spent_usd: float) -> None:
    path = _state_file()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"month={month}\nspent_usd={spent_usd:.4f}\n")


def remaining_budget() -> float:
    """Remaining USD in the current calendar month's shared cap."""
    _, spent = _read_state()
    return max(0.0, MONTHLY_CAP_USD - spent)


def record_spend(amount_usd: float) -> None:
    """Adds amount_usd to this month's running total (creating/resetting
    the ledger first if the calendar month has rolled over)."""
    month, spent = _read_state()
    _write_state(month, spent + amount_usd)


def main() -> int:
    if len(sys.argv) < 2:
        sys.exit("usage: fuzz_spend_ledger.py remaining|record <amount_usd>")
    cmd = sys.argv[1]
    if cmd == "remaining":
        print(f"{remaining_budget():.4f}")
    elif cmd == "record":
        if len(sys.argv) != 3:
            sys.exit("usage: fuzz_spend_ledger.py record <amount_usd>")
        record_spend(float(sys.argv[2]))
    else:
        sys.exit(f"unknown command: {cmd}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
