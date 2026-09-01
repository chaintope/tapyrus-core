"""Fuzz4All target for Tapyrus Script -- install as
Fuzz4All/target/TAPYRUSSCRIPT/TAPYRUSSCRIPT.py in a real Fuzz4All checkout
(register it in Fuzz4All/make_target.py's target dispatch alongside
CPPTarget/GOTarget/etc, the same way every other target/<LANG>/<LANG>.py is
wired in -- not shown here since it's a one-line dispatch-table addition in
a file this session did not modify).

Modeled directly on target/CPP/CPP.py (read from the real Fuzz4All source),
adapted for Tapyrus Script: the LLM is asked to generate a program in
Script's opcode-mnemonic form (e.g. "OP_DUP OP_HASH160 0x<hex>
OP_EQUALVERIFY OP_CHECKSIG" -- note the 0x prefix ParseScript actually
requires for raw hex pushes, unlike the placeholder <hex> this docstring
used to show), which this target's oracle then feeds through Tapyrus's
real EvalScript/VerifyScript.

The oracle is `tapyrus-verify --fuzz <file>` (src/tapyrus-verify.cpp),
built via `cmake -DBUILD_SCRIPT_VERIFY=ON -DBUILD_FUZZ_TEST=ON`. Given
one script, it builds several (to_spend, spending) transaction pairs --
sweeping a handful of nLockTime/nSequence combinations, not just one --
and runs VerifyScript on each; a crash in any of them kills the whole
process. See ../fuzz4all/README.md and src/tapyrus-verify.cpp's own
header comment for the full exit-code contract.
"""
import subprocess
from typing import Tuple

from Fuzz4All.target.target import FResult, Target
from Fuzz4All.util.util import comment_remover

# Path to the tapyrus-verify binary, built with
# -DBUILD_SCRIPT_VERIFY=ON -DBUILD_FUZZ_TEST=ON (see
# contrib/fuzz/fuzz4all/fuzz_script_generate_pool.py, which builds it before
# running Fuzz4All).
VERIFY_BINARY_PATH = "/TODO/build_fuzz/bin/tapyrus-verify"
REJECTED_BY_ASSEMBLER_EXIT_CODE = 2


class TAPYRUSSCRIPTTarget(Target):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.SYSTEM_MESSAGE = "You are a Tapyrus Script fuzzer"
        if kwargs["template"] == "fuzzing_with_config_file":
            config_dict = kwargs["config_dict"]
            self.prompt_used = self._create_prompt_from_config(config_dict)
            self.config_dict = config_dict
        else:
            raise NotImplementedError

    def write_back_file(self, code: str) -> str:
        path = f"/tmp/tapyrus_script_{self.CURRENT_TIME}.fuzz"
        with open(path, "w", encoding="utf-8") as f:
            f.write(code)
        return path

    def wrap_prompt(self, prompt: str) -> str:
        return f"# {prompt}\n{self.prompt_used['separator']}\n{self.prompt_used['begin']}"

    def wrap_in_comment(self, prompt: str) -> str:
        return f"# {prompt}"

    def filter(self, code: str) -> bool:
        clean_code = code.replace(self.prompt_used["begin"], "").strip()
        return self.prompt_used["target_api"] in clean_code

    def clean(self, code: str) -> str:
        return comment_remover(code)

    def clean_code(self, code: str) -> str:
        code = comment_remover(code)
        return "\n".join(
            line for line in code.split("\n")
            if line.strip() and line.strip() != self.prompt_used["begin"]
        )

    def validate_individual(self, filename: str) -> Tuple[FResult, str]:
        try:
            result = subprocess.run(
                [VERIFY_BINARY_PATH, "--fuzz", filename],
                capture_output=True,
                encoding="utf-8",
                timeout=self.timeout,
            )
        except subprocess.TimeoutExpired:
            return FResult.TIMED_OUT, "tapyrus-verify timed out"

        if result.returncode == 0:
            return FResult.SAFE, "its safe"
        if result.returncode == REJECTED_BY_ASSEMBLER_EXIT_CODE:
            return FResult.LLM_WEAKNESS, result.stderr
        # A negative returncode from subprocess means the child was killed
        # by that signal (e.g. -11 for SIGSEGV, -6 for SIGABRT/ASan) --
        # exactly the "found a real bug" case.
        if result.returncode < 0:
            return FResult.ERROR, result.stderr
        return FResult.FAILURE, result.stderr
