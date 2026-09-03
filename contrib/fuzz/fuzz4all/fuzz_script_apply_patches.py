#!/usr/bin/env python3
# Copyright (c) 2026 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Applies the two small, mechanical source patches a fresh Fuzz4All
checkout needs to use claude_model.py's ClaudeModel backend and the
TAPYRUSSCRIPT target -- replacing the old "patch it by hand, press enter
when done" manual step in fuzz_script_generate_pool.py.

Confirmed against Fuzz4All's actual current source
(github.com/fuzz4all/fuzz4all, main branch, cloned and read directly this
session -- not guessed from the README or from memory). Every patch below
is an exact-string match against that confirmed source: if the match
fails, this exits with a clear error rather than silently skipping the
patch or guessing at a fuzzy replacement -- that means upstream has
changed since this was written, and this script needs a matching update,
not a workaround.

Usage: fuzz_script_apply_patches.py <fuzz4all-checkout-dir>
"""
import sys
from pathlib import Path


def _replace_exact(path: Path, text: str, original: str, patched: str) -> str:
    if original not in text:
        sys.exit(
            f"error: expected block not found in {path} -- Fuzz4All's "
            "source has likely changed since this patch was written; "
            "update fuzz_script_apply_patches.py to match, don't skip this step."
        )
    return text.replace(original, patched, 1)


def patch_model_py(fuzz4all_dir: Path) -> None:
    path = fuzz4all_dir / "Fuzz4All" / "model.py"
    text = path.read_text()

    original = (
        "def make_model(eos: list, model_name: str, device: str, max_length: int):\n"
        "    if is_ollama_model(model_name):\n"
        "        return None\n"
        "    else:\n"
        "        return StarCoder(model_name, device, eos, max_length)\n"
    )
    patched = (
        "def make_model(eos: list, model_name: str, device: str, max_length: int):\n"
        "    if is_ollama_model(model_name):\n"
        "        return None\n"
        "    elif is_claude_model(model_name):\n"
        "        return ClaudeModel(get_claude_model_name(model_name), eos, max_length)\n"
        "    else:\n"
        "        return StarCoder(model_name, device, eos, max_length)\n"
    )
    text = _replace_exact(path, text, original, patched)

    import_line = "from claude_model import is_claude_model, get_claude_model_name, ClaudeModel\n"
    if import_line not in text:
        text = import_line + text

    path.write_text(text)
    print(f"patched {path}")


def patch_make_target_py(fuzz4all_dir: Path) -> None:
    path = fuzz4all_dir / "Fuzz4All" / "make_target.py"
    text = path.read_text()

    anchor_import = "from Fuzz4All.target.target import Target\n"
    import_line = "from Fuzz4All.target.TAPYRUSSCRIPT.TAPYRUSSCRIPT import TAPYRUSSCRIPTTarget\n"
    if anchor_import not in text:
        sys.exit(f"error: expected import anchor not found in {path}")
    if import_line not in text:
        text = text.replace(anchor_import, import_line + anchor_import, 1)

    # make_target() -- CLI-args dispatch, not what fuzz_script_generate_pool.py
    # actually uses (that's make_target_with_config() below, via --config),
    # but patched too for anyone invoking Fuzz4All the other way.
    text = _replace_exact(
        path,
        text,
        '    elif language == "java":\n'
        "        return JAVATarget(**kwargs)\n"
        "    else:\n"
        '        raise ValueError(f"Invalid target {language}")\n',
        '    elif language == "java":\n'
        "        return JAVATarget(**kwargs)\n"
        '    elif language == "tapyrusscript":\n'
        "        return TAPYRUSSCRIPTTarget(**kwargs)\n"
        "    else:\n"
        '        raise ValueError(f"Invalid target {language}")\n',
    )

    # make_target_with_config() -- the --config dispatch path
    # fuzz_script_generate_pool.py's `python3 -m Fuzz4All.fuzz --config ...`
    # actually goes through.
    text = _replace_exact(
        path,
        text,
        '    elif target["language"] == "java":\n'
        "        return JAVATarget(**target_compat_dict)\n"
        "    else:\n"
        "        raise ValueError(f\"Invalid target {target['language']}\")\n",
        '    elif target["language"] == "java":\n'
        "        return JAVATarget(**target_compat_dict)\n"
        '    elif target["language"] == "tapyrusscript":\n'
        "        return TAPYRUSSCRIPTTarget(**target_compat_dict)\n"
        "    else:\n"
        "        raise ValueError(f\"Invalid target {target['language']}\")\n",
    )

    path.write_text(text)
    print(f"patched {path}")


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit("usage: fuzz_script_apply_patches.py <fuzz4all-checkout-dir>")
    fuzz4all_dir = Path(sys.argv[1])
    patch_model_py(fuzz4all_dir)
    patch_make_target_py(fuzz4all_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
