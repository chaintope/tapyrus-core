#!/usr/bin/env python3
#
# Copyright (c) 2025 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Check Python files for:
1. Syntax errors using AST parsing
2. Proper shebang (#!/usr/bin/env python3)
3. UTF-8 encoding specified in open() calls

No external dependencies required.
"""

import ast
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


def check_shebang(filename):
    """Check that Python files with shebangs use #!/usr/bin/env python3."""
    try:
        with open(filename, 'rb') as f:
            first_two = f.read(2)
            if first_two != b'#!':
                # No shebang, that's okay
                return True, None

            # Has shebang, check if it's correct
            f.seek(0)
            first_line = f.readline().decode('utf-8', errors='ignore').rstrip()

            if first_line != '#!/usr/bin/env python3':
                return False, f"{filename}: Missing shebang \"#!/usr/bin/env python3\" (do not use python or python2)"

        return True, None
    except Exception as e:
        return False, f"{filename}: Error checking shebang: {str(e)}"


def check_utf8_encoding(filename):
    """Check that open() calls specify encoding parameter for text files."""
    try:
        with open(filename, 'r', encoding='utf-8') as f:
            content = f.read()

        # Parse the file as AST
        tree = ast.parse(content, filename=filename)
        errors = []

        # Walk through all nodes in the AST
        for node in ast.walk(tree):
            # Look for Call nodes where the function is 'open'
            if isinstance(node, ast.Call):
                # Check if this is a call to 'open'
                is_open_call = False
                if isinstance(node.func, ast.Name) and node.func.id == 'open':
                    is_open_call = True

                if is_open_call:
                    # Check if 'encoding' keyword argument is present
                    has_encoding = any(kw.arg == 'encoding' for kw in node.keywords)

                    # Check if mode argument contains 'b' (binary mode)
                    is_binary = False
                    if len(node.args) >= 2:
                        # Second argument is the mode
                        mode_arg = node.args[1]
                        if isinstance(mode_arg, ast.Constant) and isinstance(mode_arg.value, str):
                            if 'b' in mode_arg.value:
                                is_binary = True

                    # Also check if mode is passed as keyword
                    for kw in node.keywords:
                        if kw.arg == 'mode' and isinstance(kw.value, ast.Constant):
                            if isinstance(kw.value.value, str) and 'b' in kw.value.value:
                                is_binary = True

                    if not has_encoding and not is_binary:
                        line_num = node.lineno
                        errors.append(f"{filename}:{line_num}: open() call without explicit encoding")

        if errors:
            return False, errors

        return True, None
    except SyntaxError:
        # If there's a syntax error, it will be caught by check_syntax
        return True, None
    except Exception as e:
        return False, [f"{filename}: Error checking UTF-8 encoding: {str(e)}"]


def main():
    files = get_python_files()
    if not files:
        print("No Python files found")
        return 0

    all_errors = []

    # Check syntax errors
    for filename in files:
        success, error = check_syntax(filename)
        if not success:
            all_errors.append(error)

    # Check shebang
    for filename in files:
        success, error = check_shebang(filename)
        if not success:
            all_errors.append(error)

    # Check UTF-8 encoding in open() calls
    for filename in files:
        success, errors = check_utf8_encoding(filename)
        if not success:
            if isinstance(errors, list):
                all_errors.extend(errors)
            else:
                all_errors.append(errors)

    if all_errors:
        print("Python linting errors found:")
        print()
        for error in all_errors:
            print(error)
        print()
        print("✗ lint-python-syntax: FAILED")
        return 1

    print("✓ lint-python-syntax: PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
