#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Shared async subprocess runner for contrib/fuzz's Python tooling.
Every fuzz_code_step*.py script shells out to real external commands
(docker, cmake, python3 subprocesses) -- this wraps
asyncio.create_subprocess_exec once so each script isn't reimplementing
output streaming, exit-code checking, and environment inheritance on its
own. Leading underscore: an internal helper module, not one of this
directory's own entry points.
"""
import asyncio
import os
import platform
from pathlib import Path
from typing import Optional, Sequence, Union

# fuzz_code_step1_build_image.py's `docker build` call doesn't pass
# --platform itself, so on an arm64 host (Apple Silicon) Docker's builder
# defaults to the host's native linux/arm64 -- producing an arm64-tagged
# image that fuzz_code_step2_start_container.py's own `docker run
# --platform linux/amd64` can't find ("Unable to find image ...:latest
# locally"). DOCKER_DEFAULT_PLATFORM makes the Docker CLI use amd64 for
# any command in this process tree that doesn't pass --platform itself.
# A no-op on an actual x86_64 host, so this isn't gated to only affect
# behavior that needs changing; setdefault() still lets an explicit
# override win.
if platform.machine() in ("arm64", "aarch64"):
    os.environ.setdefault("DOCKER_DEFAULT_PLATFORM", "linux/amd64")


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

    async def _spawn_and_wait(self) -> int:
        proc = await asyncio.create_subprocess_exec(
            *self.args, cwd=self.cwd, env=self.env,
        )
        return await proc.wait()
