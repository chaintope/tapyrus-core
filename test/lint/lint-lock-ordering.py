#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Detect lock ordering inversions in Tapyrus C++ source files.

Strategy
--------
1. LOCK2(a, b) calls define the canonical ordering a → b.
2. AssertLockHeld(a) at function scope + LOCK(b) anywhere in the
   same function defines a → b (caller must hold a when b is acquired).
3. LOCK(a) appearing inside a braced LOCK(a){...} scope defines a nesting.
4. Any pair (a, b) where both a→b and b→a are found is a potential deadlock.

The script also enforces a hard-coded forbidden list of inversions that violate
the Tapyrus canonical lock hierarchy:

    cs_main > mempool.cs > cs_wallet > cs_KeyStore
    cs_main > g_cs_orphans
    cs_main > cs_LastBlockFile
    cs_inventory must NEVER be held when acquiring cs_main

Usage
-----
    python3 test/lint/lint-lock-ordering.py [--src SRC_DIR] [--verbose]

Exit codes
----------
    0  No violations found
    1  One or more violations found
"""

import argparse
import os
import re
import sys
from collections import defaultdict
from typing import Dict, List, NamedTuple, Set, Tuple  # noqa: F401 (Set used at module level)


# ---------------------------------------------------------------------------
# Canonical lock hierarchy (outer → inner).  Acquiring in any other order
# is a bug.  List each forbidden pair as (held_first, then_acquired).
# ---------------------------------------------------------------------------
FORBIDDEN_PAIRS: List[Tuple[str, str]] = [
    # ---- Wallet locks must not be held when acquiring cs_main ----
    ("cs_wallet",        "cs_main"),
    ("cs_KeyStore",      "cs_main"),
    ("cs_KeyStore",      "cs_wallet"),
    # ---- Mempool must not be held when acquiring cs_main ----
    ("mempool.cs",       "cs_main"),
    # ---- cs_inventory must never be held when acquiring global locks ----
    # (fixed in net_processing.cpp:1392 — keep this to prevent regression)
    ("cs_inventory",     "cs_main"),
    ("cs_inventory",     "mempool.cs"),
    ("cs_inventory",     "cs_wallet"),
    # ---- Other cs_main subordinates ----
    ("g_cs_orphans",     "cs_main"),
    ("cs_LastBlockFile", "cs_main"),
    # ---- Per-node locks must not be held when acquiring global locks ----
    ("cs_feeFilter",     "cs_main"),
    ("cs_feeFilter",     "mempool.cs"),
    ("cs_feeFilter",     "cs_wallet"),
    ("cs_filter",        "cs_main"),
    ("cs_filter",        "mempool.cs"),
    ("cs_filter",        "cs_wallet"),
    ("cs_most_recent_block", "cs_main"),
    ("cs_most_recent_block", "mempool.cs"),
    ("cs_vSend",         "cs_main"),
    ("cs_hSocket",       "cs_main"),
    ("m_misbehavior_mutex", "cs_main"),
    ("m_misbehavior_mutex", "cs_wallet"),
]

# Mutex name aliases — some code uses pwallet->cs_wallet, m_wallet.cs_wallet etc.
# Map every variant to a canonical name used in FORBIDDEN_PAIRS.
ALIAS_RE = [
    (re.compile(r'(?:pwallet|wallet|walletInstance|m_wallet)\s*(?:->|\.)\s*cs_wallet'), 'cs_wallet'),
    (re.compile(r'(?:pfrom|pto|pnode)\s*->\s*cs_inventory'), 'cs_inventory'),
    (re.compile(r'(?:::|[a-zA-Z_][a-zA-Z0-9_]*::)\s*cs_main'), 'cs_main'),
    # Any CTxMemPool instance's .cs field → canonical name
    (re.compile(r'(?:mempool|pool|m_mempool)\s*\.\s*cs\b'), 'mempool.cs'),
]

# TRY_LOCK acquisitions are non-blocking; a thread that fails the TRY_LOCK
# does not hold the mutex, so TRY_LOCK cannot participate in a circular-wait
# deadlock.  We exclude them from nested-brace analysis (but still emit them
# for AssertLockHeld chains, where the lock IS held by the caller).
TRY_LOCK_ONLY_NAMES: Set[str] = {'cs_Shutdown'}

# Mutex names that are clearly test stubs or parser artifacts — skip them.
IGNORED_NAMES: Set[str] = {
    '', 'cs1', 'cs2', 'cs', 'lockShutdown', 'lockMain',
    'lock', 'Lock', 'lockWallet', 'g_cs_status',
    # Test-only dummy node fields — not real per-node mutexes
    'dummyNode.cs_sendProcessing', 'dummyNode1.cs_sendProcessing',
    'dummyNode2.cs_sendProcessing',
}

# ---------------------------------------------------------------------------
# Regex patterns
# ---------------------------------------------------------------------------
RE_LOCK2     = re.compile(r'\bLOCK2\s*\(\s*([^,)]+?)\s*,\s*([^)]+?)\s*\)')
RE_LOCK      = re.compile(r'\bLOCK\s*\(\s*([^)]+?)\s*\)')
RE_TRYLOCK   = re.compile(r'\bTRY_LOCK\s*\(\s*([^,)]+?)\s*,')
RE_ASSERTLHD = re.compile(r'\bAssertLockHeld\s*\(\s*([^)]+?)\s*\)')
RE_EXCL_ANN  = re.compile(r'\bEXCLUSIVE_LOCKS_REQUIRED\s*\(([^)]+)\)')
# Strip C++ line comments
RE_LINE_CMT  = re.compile(r'//.*')


class Ordering(NamedTuple):
    held:     str   # mutex already held
    acquired: str   # mutex being acquired
    file:     str
    line:     int


def normalise(name: str) -> str:
    """Apply alias substitutions and return a canonical mutex name, or '' to skip."""
    name = name.strip()
    # Apply known aliases first (compound expressions like pwallet->cs_wallet)
    for pattern, canonical in ALIAS_RE:
        name = pattern.sub(canonical, name)
    # Strip namespace qualifiers and pointer/member prefixes
    name = re.sub(r'^[!&*\s]+', '', name)               # leading !, &, *
    name = re.sub(r'^(?:[a-zA-Z_]\w*::)+', '', name)    # Foo:: namespace
    name = re.sub(r'^.*?->', '', name)                   # ptr->member
    # "state.m_foo" → "m_foo", but keep "mempool.cs" intact
    if not name.startswith('mempool'):
        name = re.sub(r'^\w+\.((?:m_|cs_)\w+)', r'\1', name)
    # "mempool.cs" shortcut
    name = re.sub(r'^mempool\.(cs)\b', r'mempool.\1', name)
    name = name.strip()
    # Reject obviously synthetic / artifact names
    if name in IGNORED_NAMES:
        return ''
    if not re.match(r'^[a-z_][a-zA-Z0-9_.]*$', name):
        return ''
    return name


def strip_comments(src: str) -> str:
    """Remove block comments /* ... */ and line comments, and blank string literals."""
    # Block comments (non-greedy, allow newlines)
    src = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group().count('\n'), src, flags=re.DOTALL)
    # String literals — replace contents with empty string so that a '}' inside
    # a string does not corrupt the brace-depth counter in iter_function_bodies.
    src = re.sub(r'"(?:[^"\\]|\\.)*"', '""', src)
    lines = []
    for line in src.splitlines():
        lines.append(RE_LINE_CMT.sub('', line))
    return '\n'.join(lines)


# Matches a closing ')' as the last significant character before '{', optionally
# followed only by C++ post-qualifiers.  Used by iter_function_bodies to
# distinguish function/lambda/if bodies from namespace/class/struct blocks.
RE_FN_SIGNATURE_END = re.compile(r'\)\s*(?:(?:const|noexcept|override|final)\s*)*$')


# ---------------------------------------------------------------------------
# Function-body extractor (brace counting)
# ---------------------------------------------------------------------------
def iter_function_bodies(src: str) -> List[Tuple[int, str]]:
    """
    Yield (start_line, body_text) for each function or method body.

    A brace block is treated as a function body when the text between the
    previous '}' (or start of file) and the opening '{' ends with ')' or a
    C++ post-qualifier such as const/noexcept/override/final.  Namespace,
    class, struct, and enum blocks do not match this pattern and are therefore
    recursed into without being yielded — eliminating the false lock-ordering
    edges that arise from treating an entire namespace as one function body.
    """
    bodies = []
    # Stack entries: (is_function_body, body_start_idx, body_start_line)
    stack: List[Tuple[bool, int, int]] = []
    seg_start = 0   # position right after the last '{' or '}'
    line_no = 1

    for i, ch in enumerate(src):
        if ch == '\n':
            line_no += 1
        elif ch == '{':
            preceding = src[seg_start:i]
            is_fn = bool(RE_FN_SIGNATURE_END.search(preceding))
            stack.append((is_fn, i + 1, line_no))
            seg_start = i + 1
        elif ch == '}':
            seg_start = i + 1
            if stack:
                is_fn, body_start, body_start_line = stack.pop()
                if is_fn:
                    bodies.append((body_start_line, src[body_start:i]))

    return bodies


# ---------------------------------------------------------------------------
# Extract orderings from a single source file
# ---------------------------------------------------------------------------
def extract_orderings(filepath: str) -> List[Ordering]:
    orderings: List[Ordering] = []

    with open(filepath, encoding='utf-8', errors='replace') as fh:
        raw = fh.read()

    src = strip_comments(raw)

    # --- Pass 1: LOCK2 anywhere in the file (most reliable) ---
    for m in RE_LOCK2.finditer(src):
        line_no = src[:m.start()].count('\n') + 1
        a = normalise(m.group(1))
        b = normalise(m.group(2))
        orderings.append(Ordering(a, b, filepath, line_no))

    # --- Pass 2: per-function analysis ---
    for func_start_line, body in iter_function_bodies(src):
        # Collect AssertLockHeld / EXCLUSIVE_LOCKS_REQUIRED at top of body
        asserted: Set[str] = set()
        for m in RE_ASSERTLHD.finditer(body):
            asserted.add(normalise(m.group(1)))
        for m in RE_EXCL_ANN.finditer(body):
            for tok in m.group(1).split(','):
                asserted.add(normalise(tok))

        # Every LOCK(b) in this function implies held(a) → acquired(b)
        # for every a in asserted.
        for m in RE_LOCK.finditer(body):
            b = normalise(m.group(1))
            line_no = func_start_line + body[:m.start()].count('\n')
            for a in asserted:
                if a != b:
                    orderings.append(Ordering(a, b, filepath, line_no))

        # TRY_LOCK is also an acquisition (even if it can fail, the lock
        # order detector sees it as an acquisition attempt).
        for m in RE_TRYLOCK.finditer(body):
            b = normalise(m.group(1))
            line_no = func_start_line + body[:m.start()].count('\n')
            for a in asserted:
                if a != b:
                    orderings.append(Ordering(a, b, filepath, line_no))

        # --- Nested brace analysis within this function body ---
        # Walk the body tracking which LOCK()s are "open" at each depth.
        # When we see LOCK(b) while LOCK(a) is open at a shallower depth,
        # record a → b.
        # TRY_LOCK is included: when it succeeds the mutex IS held, so the
        # nesting a→b is a real ordering.  (Pass 2 already records TRY_LOCK
        # edges; this makes Pass 3 consistent with that.)
        depth_locks: List[List[str]] = [[]]  # stack of per-depth mutex lists
        depth = 0
        i = 0
        lines_before = func_start_line
        while i < len(body):
            ch = body[i]
            if ch == '{':
                depth += 1
                if depth >= len(depth_locks):
                    depth_locks.append([])
            elif ch == '}':
                if depth < len(depth_locks):
                    depth_locks[depth] = []
                depth = max(0, depth - 1)
            else:
                # Check for LOCK( or TRY_LOCK( at this position
                m = RE_LOCK.match(body, i) or RE_TRYLOCK.match(body, i)
                if m:
                    b = normalise(m.group(1))
                    if b and b not in TRY_LOCK_ONLY_NAMES:
                        line_no = lines_before + body[:i].count('\n')
                        # All mutexes held at shallower depth define an ordering
                        for d in range(depth):
                            if d < len(depth_locks):
                                for a in depth_locks[d]:
                                    if a != b:
                                        orderings.append(Ordering(a, b, filepath, line_no))
                        # Add this lock to the current depth
                        while len(depth_locks) <= depth:
                            depth_locks.append([])
                        depth_locks[depth].append(b)
            i += 1

    return orderings


# ---------------------------------------------------------------------------
# Main analysis
# ---------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--src', default='src',
                        help='Path to source directory (default: src)')
    parser.add_argument('--verbose', action='store_true',
                        help='Print all orderings, not just violations')
    args = parser.parse_args()

    src_root = args.src
    if not os.path.isdir(src_root):
        # Try relative to script location (repo root)
        repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        src_root = os.path.join(repo_root, 'src')

    # Collect all .cpp/.h files
    source_files: List[str] = []
    for dirpath, _, filenames in os.walk(src_root):
        # skip build / dependency dirs
        skip_dirs = {'secp256k1', 'univalue', 'leveldb', 'crypto', 'tinyformat',
                     'compat', 'config', 'obj', '.git'}
        dirpath_parts = set(os.path.normpath(dirpath).split(os.sep))
        if dirpath_parts & skip_dirs:
            continue
        for fname in filenames:
            if fname.endswith(('.cpp', '.h')):
                source_files.append(os.path.join(dirpath, fname))

    # Extract all orderings
    all_orderings: List[Ordering] = []
    for fpath in sorted(source_files):
        try:
            all_orderings.extend(extract_orderings(fpath))
        except Exception as exc:
            print(f"WARNING: could not parse {fpath}: {exc}", file=sys.stderr)

    # Build per-pair edge sets: pair (a, b) → list of Ordering evidence
    edge: Dict[Tuple[str, str], List[Ordering]] = defaultdict(list)
    for o in all_orderings:
        # Skip trivial self-orderings
        if o.held == o.acquired:
            continue
        # Skip unnamed / empty
        if not o.held or not o.acquired:
            continue
        edge[(o.held, o.acquired)].append(o)

    violations: List[str] = []

    # --- Check 1: Forbidden pair inversions ---
    for held, acquired in FORBIDDEN_PAIRS:
        key = (held, acquired)
        if key in edge:
            locs = edge[key]
            evidence = '; '.join(f"{o.file}:{o.line}" for o in locs[:5])
            if len(locs) > 5:
                evidence += f' … ({len(locs) - 5} more)'
            violations.append(
                f"FORBIDDEN: {held} → {acquired}  "
                f"(violates canonical hierarchy)\n"
                f"  Found at: {evidence}"
            )

    # --- Check 2: Bidirectional (A→B and B→A both exist) ---
    seen_pairs: Set[Tuple[str, str]] = set()
    for (a, b) in list(edge.keys()):
        if (a, b) in seen_pairs or (b, a) in seen_pairs:
            continue
        if (b, a) in edge:
            seen_pairs.add((a, b))
            locs_ab = edge[(a, b)]
            locs_ba = edge[(b, a)]
            ev_ab = '; '.join(f"{o.file}:{o.line}" for o in locs_ab[:3])
            ev_ba = '; '.join(f"{o.file}:{o.line}" for o in locs_ba[:3])
            # Only report if it's not already in FORBIDDEN_PAIRS
            if (a, b) not in FORBIDDEN_PAIRS and (b, a) not in FORBIDDEN_PAIRS:
                violations.append(
                    f"INVERSION: {a} → {b} AND {b} → {a}\n"
                    f"  {a}→{b} at: {ev_ab}\n"
                    f"  {b}→{a} at: {ev_ba}"
                )

    # --- Report ---
    if args.verbose:
        print("=== All detected lock orderings ===")
        for (a, b), locs in sorted(edge.items()):
            print(f"  {a} → {b}  ({len(locs)} sites)")
        print()

    if violations:
        print("=== LOCK ORDERING VIOLATIONS ===")
        for v in violations:
            print(v)
            print()
        print(f"{len(violations)} violation(s) found.")
        return 1

    print("Lock ordering lint: OK (no violations found)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
