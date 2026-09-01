"""Claude backend for Fuzz4All, mirroring the existing `ollama/<name>`
convention in Fuzz4All/model.py and Fuzz4All/target/target.py -- a
`model_name` of `claude/<model-id>` (e.g. `claude/claude-opus-5`) selects
this backend instead of the local HuggingFace StarCoder path.

To apply: this file is NOT a drop-in replacement for Fuzz4All/model.py --
it's meant to be installed alongside a real Fuzz4All checkout, patching
`make_model()` there to recognize `is_claude_model()` the same way it
already recognizes `is_ollama_model()`. `fuzz_script_apply_patches.py` in
this directory does this automatically (exact-string-match against
Fuzz4All's real current source, fails loudly if it doesn't match rather
than guessing) -- `fuzz_script_generate_pool.py` runs it, no hand-editing
needed. The patch it applies:

    from claude_model import is_claude_model, get_claude_model_name, ClaudeModel

    def make_model(eos, model_name, device, max_length):
        if is_ollama_model(model_name):
            return None
        elif is_claude_model(model_name):
            return ClaudeModel(get_claude_model_name(model_name), eos, max_length)
        else:
            return StarCoder(model_name, device, eos, max_length)

Fuzz4All/target/target.py's own `auto_prompt()` explicitly raises
NotImplementedError for API-based autoprompting regardless of backend
("Auto-prompting with API requests is disabled. Only Ollama/local models
are supported.") -- unpatched. Set `fuzzing.no_input_prompt: true` (as
config/cpp_demo.yaml already does) or `use_hand_written_prompt: true` in
the target config to avoid hitting that path; this adapter only replaces
the per-iteration *generation* call, not the one-time autoprompting step.
"""
import sys
from pathlib import Path
from typing import List

import anthropic

# fuzz_spend_ledger.py normally lives one directory up (contrib/fuzz/,
# shared with fuzz_code_generate_and_draft.py) -- but
# fuzz_script_generate_pool.py copies it into this file's own directory
# alongside a fresh Fuzz4All clone, so both locations need to be on the
# path for this import to resolve regardless of which context it's
# running in.
_here = Path(__file__).parent
sys.path.insert(0, str(_here))
sys.path.insert(0, str(_here.parent))
import fuzz_spend_ledger  # noqa: E402

DEFAULT_MODEL = "claude-opus-5"

# Per-1M-token USD pricing (input, output). Keep in sync with this repo's
# claude-api skill (the authoritative current table) if models reprice.
MODEL_PRICING_PER_MTOK = {
    "claude-opus-5": (5.00, 25.00),
    "claude-sonnet-5": (2.00, 10.00),
    "claude-haiku-4-5": (1.00, 5.00),
}


def is_claude_model(model_name: str) -> bool:
    return model_name.startswith("claude/")


def get_claude_model_name(model_name: str) -> str:
    if is_claude_model(model_name):
        remainder = model_name.split("/", 1)[1]
        return remainder or DEFAULT_MODEL
    return model_name


class BudgetExceededError(RuntimeError):
    """Raised once fuzz_spend_ledger reports no budget left in this
    calendar month's shared $50 cap (shared with
    fuzz_code_generate_and_draft.py -- see fuzz_spend_ledger.py's own
    docstring for why this is one shared cap, not one per script).

    Fuzz4All has no built-in spend cap and its make_model() call site
    (see this file's own module docstring) doesn't pass extra
    constructor args, so there's nothing to catch this closer to the
    call site than a top-level traceback. Left uncaught, this stops the
    whole `python3 -m Fuzz4All.fuzz` process;
    fuzz_script_generate_pool.py treats that as an expected stop
    condition, not a failure, once the shared budget is spent.
    """


class ClaudeModel:
    def __init__(self, model_name: str, eos: List[str], max_length: int) -> None:
        self.model_name = model_name or DEFAULT_MODEL
        self.eos = eos
        self.max_length = max_length
        self.client = anthropic.Anthropic()

    def _price_per_mtok(self):
        # Falls back to Opus 5 pricing (the most expensive current tier)
        # for an unrecognized model name -- fails toward under-, not
        # over-, estimating remaining budget.
        return MODEL_PRICING_PER_MTOK.get(self.model_name, MODEL_PRICING_PER_MTOK[DEFAULT_MODEL])

    def _cost_of(self, usage) -> float:
        price_in, price_out = self._price_per_mtok()
        return (usage.input_tokens / 1_000_000) * price_in + (usage.output_tokens / 1_000_000) * price_out

    def generate(
        self, prompt: str, batch_size: int = 10, temperature: float = 1.0, max_length: int = 512
    ) -> List[str]:
        if fuzz_spend_ledger.remaining_budget() <= 0:
            raise BudgetExceededError(
                "shared monthly fuzz-generation budget "
                f"(${fuzz_spend_ledger.MONTHLY_CAP_USD:.2f}) already spent this month"
            )
        # The Messages API has no "num completions" parameter the way
        # StarCoder's num_return_sequences does -- batch_size independent
        # requests, issued sequentially. Parallelizing this loop is a
        # reasonable follow-up if generation throughput becomes the
        # bottleneck (it currently will not be latency-competitive with a
        # local batched HuggingFace generate() call).
        outputs = []
        for _ in range(batch_size):
            if fuzz_spend_ledger.remaining_budget() <= 0:
                break
            response = self.client.messages.create(
                model=self.model_name,
                max_tokens=min(self.max_length, max_length),
                temperature=max(temperature, 1e-2),
                messages=[{"role": "user", "content": prompt}],
            )
            # Recorded immediately (real per-call usage, not an estimate)
            # so an interrupted run still leaves accurate shared spend
            # behind for the next invocation -- of either script -- to
            # see, rather than losing it if this process never reaches a
            # clean exit.
            fuzz_spend_ledger.record_spend(self._cost_of(response.usage))
            text = "".join(
                block.text for block in response.content if block.type == "text"
            )
            for stop_string in self.eos:
                if stop_string and stop_string in text:
                    text = text[: text.index(stop_string)]
            outputs.append(text)
        return outputs
