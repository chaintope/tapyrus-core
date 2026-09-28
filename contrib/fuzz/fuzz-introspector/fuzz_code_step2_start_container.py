#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Step 2 of 3 in the split, locally-run introspector gap-analysis
pipeline. See fuzz_code_step1_build_image.py's docstring for the overall
rationale.

Starts one persistent, named container from the image fuzz_code_step1_
build_image.py built, bind-mounting a live tapyrus-core checkout (so
edits on the host are visible inside the container immediately, no
rebuild needed), a local output directory (so step 3's results are
readable from the host afterward), and a local ccache directory (shared
across every container this script starts, regardless of --name, since
ccache is content-addressed -- a wider cache only ever helps hit rate;
mounted at /ccache/cache, this base image's own configured ccache_dir --
confirmed via `ccache -p`, not ccache's own usual ~/.cache/ccache
default).

Both the output and ccache directories live under ~/.tapyrus-fuzz-
introspector/, deliberately OUTSIDE the git checkout -- not merely for
tidiness. The coverage build's own `cp -rL --parents /src ... /out` (a
normal part of the base image's own compile script, for later coverage
reporting) copies the whole /src tree, /usr/include, and /usr/local/
include into $OUT; bind-mounting $OUT to a path *inside* the checkout
meant that copy (23,451 files, 695MB, confirmed directly) sat inside the
same tree fuzz_code_step3_analyze_incontainer.py's own analysis step
then scanned again on the next run -- almost certainly the dominant cause
of that step taking multiple hours (its own tree-sitter parse over the
real, unrelated 745 project files takes ~3 minutes). Keeping $OUT
entirely outside the checkout makes this class of self-inflicted
scan-your-own-output-forever bug structurally impossible, regardless of
whatever scoping fuzz_code_step3_analyze_incontainer.py's own target_dir
logic does or doesn't do in the future.

Idempotent: running this again with the same --name while that container
is already up on the current image is a no-op; if it exists but points
at a stale image (step 1 rebuilt since), it's recreated automatically.

--memory/--memory-swap/--cpus: unset by default (no limit, matching
Docker's own default and this script's original behavior) -- pass them
explicitly to test a resource-constrained run without changing what an
existing default-named container gets. --memory-swap is the TOTAL
memory+swap ceiling Docker enforces (not an additional amount on top of
--memory) -- e.g. --memory 8g --memory-swap 24g gives the container 8GB
of RAM plus 16GB of disk-backed swap to spill into under pressure,
rather than a hard 8GB OOM-kill wall. This still borrows from the Docker
Desktop VM's own swap allocation (a host-level setting, Resources ->
Advanced in the Docker Desktop app) -- if the VM itself has little or no
swap configured, the container's usable swap is capped by that
regardless of what --memory-swap requests here.

Deliberately NOT torn down automatically after step 3 -- the whole point
of this split is to let step 3 (and ad-hoc `docker exec` debugging) run
many times against the same warm container. Tear it down explicitly with
--stop when you're done, or leave it running between sessions (it costs
near-zero CPU idling on `sleep infinity`).

Usage:
  fuzz_code_step2_start_container.py <path-to-tapyrus-core-checkout> [--name NAME] [--memory 8g --memory-swap 24g --cpus 2]
  fuzz_code_step2_start_container.py --status [--name NAME]
  fuzz_code_step2_start_container.py --stop [--name NAME]
"""
import argparse
import asyncio
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from _async_proc import Command  # noqa: E402

IMAGE_TAG = "tapyrus-fuzz-introspector:local"
DEFAULT_CONTAINER_NAME = "tapyrus-fuzz-introspector"
# Outside the checkout -- see module docstring for why this matters, not
# just tidiness.
HOST_BASE_DIR = Path.home() / ".tapyrus-fuzz-introspector"
# Shared across every --name -- see module docstring.
HOST_CCACHE_DIR = HOST_BASE_DIR / "ccache"


def _docker_inspect(name: str, fmt: str) -> str:
    result = subprocess.run(
        ["docker", "inspect", "--format", fmt, name],
        capture_output=True, text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else ""


def _check_secp256k1_submodule_initialized(tapyrus_core_src: Path) -> None:
    """Fails fast, with a clear message, if src/secp256k1 isn't
    initialized on the HOST before it gets bind-mounted in. The
    Dockerfile's own `git submodule update --init` (see its own comment)
    only applies to the git-clone copy that Dockerfile makes for a
    genuine OSS-Fuzz submission -- irrelevant here, since step 2's bind
    mount shadows that clone entirely with the host checkout. An
    uninitialized submodule on the host would otherwise surface much
    later and far less clearly, as a CMake configure error deep inside
    step 3's coverage build (missing secp256k1 headers), rather than
    here, before the container even starts.
    """
    result = subprocess.run(
        ["git", "-C", str(tapyrus_core_src), "submodule", "status", "src/secp256k1"],
        capture_output=True, text=True,
    )
    # A leading '-' on the status line means "not initialized" -- confirmed
    # directly (a fresh clone's src/secp256k1 status line starts with '-'
    # until `git submodule update --init` runs).
    if result.returncode != 0 or result.stdout.strip().startswith("-"):
        sys.exit(f"error: src/secp256k1 submodule is not initialized in {tapyrus_core_src} -- "
                 "run `git submodule update --init --depth=1 src/secp256k1` on the host first. "
                 "(This container bind-mounts your host checkout, so the Dockerfile's own "
                 "submodule init inside a separately-cloned copy doesn't help here.)")


async def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tapyrus_core_src", type=Path, nargs="?",
                         help="Required to start the container; omit only with --status/--stop.")
    parser.add_argument("--name", default=DEFAULT_CONTAINER_NAME,
                         help=f"Container name (default: {DEFAULT_CONTAINER_NAME}). Use a different name "
                              "to run a second container alongside an existing one -- e.g. to test new "
                              "resource limits without disturbing a long-running analysis.")
    parser.add_argument("--memory", default=None, help="Docker --memory value, e.g. 8g. Unset = no limit.")
    parser.add_argument("--memory-swap", default=None,
                         help="Docker --memory-swap value (TOTAL memory+swap ceiling, not additional -- "
                              "see module docstring), e.g. 24g. Unset = no limit.")
    parser.add_argument("--cpus", default=None, help="Docker --cpus value, e.g. 2. Unset = no limit.")
    parser.add_argument("--status", action="store_true", help="Report running/not running, then exit.")
    parser.add_argument("--stop", action="store_true", help="Stop and remove the persistent container.")
    args = parser.parse_args()

    host_out_dir = HOST_BASE_DIR / f"out_{args.name}"

    if args.status:
        running = _docker_inspect(args.name, "{{.State.Running}}") == "true"
        print("running" if running else "not running")
        return 0

    if args.stop:
        if _docker_inspect(args.name, "{{.Id}}"):
            await Command("docker", "rm", "-f", args.name).run()
            print(f"Stopped and removed {args.name}.")
        else:
            print(f"{args.name} was not running.")
        return 0

    if not args.tapyrus_core_src:
        parser.error("tapyrus_core_src is required to start the container (omit only with --status/--stop)")

    _check_secp256k1_submodule_initialized(args.tapyrus_core_src)

    target_image_id = _docker_inspect(IMAGE_TAG, "{{.Id}}")
    if not target_image_id:
        sys.exit(f"error: image {IMAGE_TAG} not found -- run fuzz_code_step1_build_image.py first")

    existing_image_id = _docker_inspect(args.name, "{{.Image}}")
    if existing_image_id:
        already_running = _docker_inspect(args.name, "{{.State.Running}}") == "true"
        if existing_image_id == target_image_id and already_running:
            print(f"{args.name} already running on the current image. Nothing to do.")
            return 0
        print(f"{args.name} exists but is stale (stopped, or built from an image "
              "step 1 has since replaced) -- recreating.")
        await Command("docker", "rm", "-f", args.name).run()

    host_out_dir.mkdir(parents=True, exist_ok=True)
    HOST_CCACHE_DIR.mkdir(parents=True, exist_ok=True)

    run_args = [
        "docker", "run", "-d", "--name", args.name,
        "--platform", "linux/amd64",
        "-v", f"{args.tapyrus_core_src.resolve()}:/src/tapyrus-core",
        "-v", f"{host_out_dir}:/out",
        "-v", f"{HOST_CCACHE_DIR}:/ccache/cache",
    ]
    if args.memory:
        run_args += ["--memory", args.memory]
    if args.memory_swap:
        run_args += ["--memory-swap", args.memory_swap]
    if args.cpus:
        run_args += ["--cpus", args.cpus]
    run_args += [IMAGE_TAG, "sleep", "infinity"]

    await Command(*run_args).run()

    print()
    print(f"Started {args.name}.")
    print(f"  /src/tapyrus-core     -> {args.tapyrus_core_src.resolve()}")
    print(f"  /out                  -> {host_out_dir}")
    print(f"  /ccache/cache         -> {HOST_CCACHE_DIR}")
    if args.memory or args.memory_swap or args.cpus:
        print(f"  limits: memory={args.memory or 'none'} memory-swap={args.memory_swap or 'none'} "
              f"cpus={args.cpus or 'none'}")
    print(f"Next: fuzz_code_step3_analyze.py --container {args.name}"
          if args.name != DEFAULT_CONTAINER_NAME else "Next: fuzz_code_step3_analyze.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(_main()))
