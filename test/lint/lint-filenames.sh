#!/usr/bin/env bash
#
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Make sure only lowercase alphanumerics (a-z0-9), underscores (_),
# hyphens (-) and dots (.) are used in source code filenames.

export LC_ALL=C

EXIT_CODE=0
# FuzzedDataProvider.h is vendored from LLVM unmodified (see
# doc/dependencies.md's "Vendored source files" section); TAPYRUSSCRIPT.py's
# uppercase name matches Fuzz4All's own target-plugin naming convention,
# which its target-discovery mechanism expects.
OUTPUT=$(git ls-files --full-name -- "*.[cC][pP][pP]" "*.[hH]" "*.[pP][yY]" "*.[sS][hH]" | \
    grep -vE '^[a-z0-9_./-]+$' | \
    grep -vE '^src/(secp256k1|univalue)/' | \
    grep -vE '^src/test/fuzz/FuzzedDataProvider\.h$' | \
    grep -vE '^contrib/fuzz/fuzz4all/TAPYRUSSCRIPT\.py$')

if [[ ${OUTPUT} != "" ]]; then
    echo "Use only lowercase alphanumerics (a-z0-9), underscores (_), hyphens (-) and dots (.)"
    echo "in source code filenames:"
    echo
    echo "${OUTPUT}"
    EXIT_CODE=1
fi

if [ ${EXIT_CODE} -eq 0 ]; then
  echo "✓ lint-filenames: PASSED"
else
  echo "✗ lint-filenames: FAILED"
fi
exit ${EXIT_CODE}
