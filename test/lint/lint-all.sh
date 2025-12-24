#!/usr/bin/env bash
#
# Copyright (c) 2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# This script runs all contrib/devtools/lint-*.sh files, and fails if any exit
# with a non-zero status code.

# This script is intentionally locale dependent by not setting "export LC_ALL=C"
# in order to allow for the executed lint scripts to opt in or opt out of locale
# dependence themselves.

export LC_ALL=C

set -u

SCRIPTDIR=$(dirname "${BASH_SOURCE[0]}")
LINTALL=$(basename "${BASH_SOURCE[0]}")

EXIT_CODE=0
FAILED_SCRIPTS=()
PASSED_SCRIPTS=()
TOTAL_SCRIPTS=0

echo "========================================="
echo "Running all lint checks..."
echo "========================================="
echo ""

# Run all scripts, collecting results (do not exit early on failure)
for f in "${SCRIPTDIR}"/lint-*.sh; do
  if [ "$(basename "$f")" != "$LINTALL" ] && [ -f "$f" ]; then
    SCRIPT_NAME=$(basename "$f")
    TOTAL_SCRIPTS=$((TOTAL_SCRIPTS + 1))

    # Run script and capture exit code, but continue regardless of result
    set +e
    "$f"
    SCRIPT_EXIT_CODE=$?
    set -e

    if [ $SCRIPT_EXIT_CODE -ne 0 ]; then
      FAILED_SCRIPTS+=("$SCRIPT_NAME")
      EXIT_CODE=1
    else
      PASSED_SCRIPTS+=("$SCRIPT_NAME")
    fi

    echo ""  # Add blank line between scripts
  fi
done

# Run the python syntax checking script
SCRIPT_NAME="lint-python-syntax.py"
TOTAL_SCRIPTS=$((TOTAL_SCRIPTS + 1))

set +e
"${SCRIPTDIR}"/"$SCRIPT_NAME"
SCRIPT_EXIT_CODE=$?
set -e

if [ $SCRIPT_EXIT_CODE -ne 0 ]; then
  FAILED_SCRIPTS+=("$SCRIPT_NAME")
  EXIT_CODE=1
else
  PASSED_SCRIPTS+=("$SCRIPT_NAME")
fi

echo "========================================="
echo "Lint Summary"
echo "========================================="
echo "Total scripts run: $TOTAL_SCRIPTS"
echo "Passed: ${#PASSED_SCRIPTS[@]}"
echo "Failed: ${#FAILED_SCRIPTS[@]}"
echo "========================================="
if [ $EXIT_CODE -eq 0 ]; then
  echo "Result: ALL LINT CHECKS PASSED"
else
  echo "Result: SOME LINT CHECKS FAILED"
fi
echo "========================================="

exit $EXIT_CODE
