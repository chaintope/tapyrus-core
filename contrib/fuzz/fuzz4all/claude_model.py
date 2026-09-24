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

No spend tracking or budget cap here -- this repo doesn't monitor or
gate this script's API usage; whoever runs it locally watches their own
Anthropic account for actual spend.
"""
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import List

import anthropic

DEFAULT_MODEL = "claude-opus-5"


def is_claude_model(model_name: str) -> bool:
    return model_name.startswith("claude/")


def get_claude_model_name(model_name: str) -> str:
    if is_claude_model(model_name):
        remainder = model_name.split("/", 1)[1]
        return remainder or DEFAULT_MODEL
    return model_name


class ClaudeModel:
    def __init__(self, model_name: str, eos: List[str], max_length: int) -> None:
        self.model_name = model_name or DEFAULT_MODEL
        self.eos = eos
        self.max_length = max_length
        self.client = anthropic.Anthropic()

    def generate(
        self, prompt: str, batch_size: int = 10, temperature: float = 1.0, max_length: int = 512
    ) -> List[str]:
        # The Messages API has no "num completions" parameter the way
        # StarCoder's num_return_sequences does -- batch_size independent
        # requests. Issued concurrently via a thread pool (each call is a
        # blocking network round-trip with no dependency on any other
        # call's result, so threads genuinely overlap here despite the
        # GIL -- it releases during socket I/O).
        def call_one() -> str:
            response = self.client.messages.create(
                model=self.model_name,
                max_tokens=min(self.max_length, max_length),
                temperature=max(temperature, 1e-2),
                messages=[{"role": "user", "content": prompt}],
            )
            text = "".join(
                block.text for block in response.content if block.type == "text"
            )
            for stop_string in self.eos:
                if stop_string and stop_string in text:
                    text = text[: text.index(stop_string)]
            return text

        outputs = []
        with ThreadPoolExecutor(max_workers=batch_size) as executor:
            futures = [executor.submit(call_one) for _ in range(batch_size)]
            # future.result() re-raises any exception from call_one(),
            # so a failed call aborts generate() rather than being
            # silently dropped.
            for future in as_completed(futures):
                outputs.append(future.result())
        return outputs
