#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Step 1 of 3 in the locally-run introspector gap-analysis pipeline.

Builds ../oss-fuzz/project/Dockerfile into a local, reusable image.
Infrequent/manual by design: this is the only one of the three steps
that touches the Dockerfile (system packages, Boost headers, static-link
config) or the pinned base image digest, none of which change often.
Steps 2 (start the persistent container) and 3 (run the analysis) are
meant to run far more often against whatever image this step last built,
without repeating this step's cost each time.

Usage:
  fuzz_code_step1_build_image.py [--base-image <ref>] [--no-cache]
"""
import argparse
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from _async_proc import Command  # noqa: E402

PROJECT_DIR = Path(__file__).resolve().parent.parent / "oss-fuzz" / "project"
IMAGE_TAG = "tapyrus-fuzz-introspector:local"


async def _main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--base-image", default=None,
        help="Override the Dockerfile's pinned BASE_IMAGE build-arg (a specific "
             "gcr.io/oss-fuzz-base/base-builder@sha256:... digest) -- e.g. to try a "
             "newer digest before deciding to bump the Dockerfile's own default. "
             "Omit to build against the pinned default.")
    parser.add_argument("--no-cache", action="store_true", help="Pass --no-cache to docker build.")
    args = parser.parse_args()

    build_args = ["docker", "build", "-t", IMAGE_TAG]
    if args.base_image:
        build_args += ["--build-arg", f"BASE_IMAGE={args.base_image}"]
    if args.no_cache:
        build_args.append("--no-cache")
    build_args += ["-f", str(PROJECT_DIR / "Dockerfile"), str(PROJECT_DIR)]

    await Command(*build_args).run()

    print()
    print(f"Built {IMAGE_TAG}.")
    print("Next: fuzz_code_step2_start_container.py <path-to-tapyrus-core-checkout>")
    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(_main()))
