# Registers a current Claude model with OSS-Fuzz-Gen's model registry.
#
# Confirmed by reading oss-fuzz-gen's actual llm_toolkit/models.py source
# (not just its docs): Claude only works there through
# `anthropic.AnthropicVertex(region=region, project_id=project_id)` -- there
# is no bare-ANTHROPIC_API_KEY code path. Every registered Claude class is
# also 3.x-era (name='vertex_ai_claude-3-5-sonnet' etc, dated @-suffixed
# _vertex_ai_model), nothing current registered.
#
# Meant to be appended (not sed'ed) onto the end of a freshly-cloned
# oss-fuzz-gen's llm_toolkit/models.py, e.g.:
#   cat fuzz_code_vertex_claude_patch.py >> oss-fuzz-gen/llm_toolkit/models.py
# Append-only is safe here (unlike a line-targeted sed against upstream's
# exact current source) because oss-fuzz-gen's `LLM.setup()` discovers every
# model via `Claude.__subclasses__()` walking (see
# `LLM.all_llm_subclasses()`/`all_llm_names()`) -- no separate registration
# list to edit, so a new subclass just needs to exist somewhere models.py
# imports, in any order, after the `Claude` base class it's defined here.
#
# Vertex AI model ID: current-generation Claude models use the bare
# first-party ID with no `@date` suffix on Vertex AI (per this repo's
# `claude-api` skill, current as of the fuzz-CI design conversation this
# patch was written in) -- unlike the dated `claude-3-5-sonnet@20240620`
# style already registered upstream for the 3.x models.
class ClaudeOpus5(Claude):
  """Claude Opus 5, via Vertex AI."""

  name = 'vertex_ai_claude-opus-5'
  _vertex_ai_model = 'claude-opus-5'
