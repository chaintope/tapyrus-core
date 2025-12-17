#!/usr/bin/env python3
#
# Copyright (c) 2025 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Check Python files for basic syntax errors using built-in Python tools.
No external dependencies required.
"""

import ast
import os
import subprocess
import sys


def get_python_files():
    """Get list of all Python files in the repository."""
    try:
        result = subprocess.run(
            ['git', 'ls-files', '--', '*.py'],
            capture_output=True,
            text=True,
            check=True
        )
        return [f for f in result.stdout.splitlines() if f]
    except subprocess.CalledProcessError:
        print("Error: Failed to get list of Python files from git")
        return []


def check_syntax(filename):
    """Check a single Python file for syntax errors using AST parsing."""
    try:
        with open(filename, 'r', encoding='utf-8') as f:
            source = f.read()

        # Try to parse the file as an AST
        ast.parse(source, filename=filename)
        return True, None
    except SyntaxError as e:
        return False, f"{filename}:{e.lineno}:{e.offset}: {e.msg}"
    except Exception as e:
        return False, f"{filename}: {str(e)}"


def main():
    files = get_python_files()
    if not files:
        print("No Python files found")
        return 0

    errors = []
    for filename in files:
        success, error = check_syntax(filename)
        if not success:
            errors.append(error)

    if errors:
        print("Python syntax errors found:")
        print()
        for error in errors:
            print(error)
        print()
        print("✗ lint-python-syntax: FAILED")
        return 1

    print("✓ lint-python-syntax: PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
