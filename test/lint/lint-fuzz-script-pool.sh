#!/usr/bin/env bash
#
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Check the fuzz_script candidate pool's batch-file format, so a malformed
# batch fails its PR rather than the nightly fuzz-script-sweep job.

export LC_ALL=C

TOPDIR=$(git rev-parse --show-toplevel)

EXIT_CODE=0
if ! python3 "${TOPDIR}/contrib/fuzz/fuzz_script/fuzz_script_pool.py" --pool-dir "${TOPDIR}/src/test/fuzz/fuzz_scripts" check; then
    echo
    echo "See contrib/fuzz/fuzz_script/fuzz_script_pool.py for the batch-file format."
    EXIT_CODE=1
fi

if [ ${EXIT_CODE} -eq 0 ]; then
  echo "✓ lint-fuzz-script-pool: PASSED"
else
  echo "✗ lint-fuzz-script-pool: FAILED"
fi
exit ${EXIT_CODE}
