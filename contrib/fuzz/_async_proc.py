#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Shared async subprocess runner for contrib/fuzz's Python tooling.
Every fuzz_code_*/fuzz_script_* orchestration script shells out to real
external commands (git, cmake, pip, python3 subprocesses) -- this wraps
asyncio.create_subprocess_exec once so each script isn't reimplementing
output streaming, exit-code checking, and environment inheritance on its
own. Leading underscore: an internal helper module, not one of this
directory's own entry points (same convention as fuzz_spend_ledger.py's
neighbors that import it directly rather than shelling out to it).
"""
import asyncio
from pathlib import Path
from typing import Optional, Sequence, Union


class CommandError(RuntimeError):
    """Raised when a Command exits non-zero -- mirrors bash -e's behavior
    of stopping the whole pipeline on the first failing command."""

    def __init__(self, args: Sequence[str], returncode: int):
        self.args = list(args)
        self.returncode = returncode
        super().__init__(f"command failed ({returncode}): {' '.join(str(a) for a in args)}")


class Command:
    """One external command, run asynchronously. Output streams live to
    this process's own stdout/stderr (the same behavior every converted
    .sh script had by default, since bash never captured a command's
    output unless a caller explicitly asked it to) rather than being
    buffered until the process exits."""

    def __init__(self, *args: Union[str, Path], cwd: Optional[Path] = None,
                 env: Optional[dict] = None):
        self.args = [str(a) for a in args]
        self.cwd = str(cwd) if cwd is not None else None
        self.env = env

    async def run(self) -> None:
        """Runs the command, raising CommandError on a non-zero exit."""
        returncode = await self._spawn_and_wait()
        if returncode != 0:
            raise CommandError(self.args, returncode)

    async def run_allowing_failure(self) -> int:
        """Like run(), but returns the exit code instead of raising --
        for the one case (Fuzz4All hitting its own BudgetExceededError) a
        non-zero exit is an expected outcome, not an exceptional one."""
        return await self._spawn_and_wait()

    async def _spawn_and_wait(self) -> int:
        proc = await asyncio.create_subprocess_exec(
            *self.args, cwd=self.cwd, env=self.env,
        )
        return await proc.wait()
