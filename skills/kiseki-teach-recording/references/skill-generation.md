# Teaching Bundle To Skill

Use this workflow when the user asks to teach an agent from a Kiseki recording, create a reusable skill from a teaching bundle, or convert an Agivar-style bundle into a Codex skill.

The invoked agent is already LLM-driven. Do not build a separate fake `teach_agent` inside the C++ CLI. The skill is the teach-agent definition: the LLM agent follows this workflow, while helper scripts provide deterministic validation, indexing, and draft file generation.

## Inputs

Required bundle files:

- `manifest.json`
- `frames.json`
- `actions.json`
- `timeline.json`
- `events.jsonl`
- `annotations.json`
- selected keyframes referenced by `manifest.keyframes[]`

Optional but useful:

- `instruction.txt`
- transcript file referenced by `manifest.media.transcript`
- review video/keyframes referenced by `manifest.media.video` and `manifest.media.videoKeyframes`

## Process

1. Validate the bundle using `references/bundle-validation.md` and the deterministic validator:

```bash
python3 skills/kiseki-teach-recording/scripts/validate_bundle.py <bundle-dir>
```

2. Read the bundle in this order: `manifest.json`, `instruction.txt`, `annotations.json`, `actions.json`, `timeline.json`, selected keyframes, transcript when present.
3. Identify the stable task being taught. Prefer the human instruction and annotations over raw pointer motion when they disagree.
4. Convert raw events into a reusable procedure:
   - Keep app/session prerequisites, target discovery, required permissions, mode family, coordinates only when they are intentionally stable, and verification commands.
   - Drop incidental focus changes, exploratory pointer motion, terminal typos, timing noise, and repeated low-value mouse moves.
   - Preserve important waits, tool selections, text input, hotkeys, drag paths, and before/after verification points.
5. Create or update a skill directory. For repo-local skills, default to `skills/<skill-name>`. For personal global skills, default to `${CODEX_HOME:-$HOME/.codex}/skills/<skill-name>`.
6. Write the new skill as reusable operating knowledge, not as a transcript. The skill should tell another agent what to do next time, not merely describe what happened once.
7. Validate the new skill with Codex's `quick_validate.py`.

## Scaffold Helper

Use the helper to create a first draft from bundle metadata:

```bash
python3 skills/kiseki-teach-recording/scripts/draft_skill_from_bundle.py \
  artifacts/live-test/teach-demo \
  --output-root skills \
  --name <new-skill-name>
```

The helper writes:

- `SKILL.md`
- `agents/openai.yaml`
- `references/teaching-evidence.md`

The generated skill is a draft. The LLM agent must edit it before claiming completion.

Script responsibilities:

- `validate_bundle.py`: fail fast on malformed or incomplete bundle files and emit a machine-readable summary.
- `draft_skill_from_bundle.py`: create a valid skill folder with evidence references.
- LLM agent: inspect the evidence and selected keyframes, remove incidental actions, infer stable intent, write the final procedure, and decide whether clarification is required.

## New Skill Shape

The final `SKILL.md` should include:

- YAML frontmatter with `name` and a trigger-rich `description`.
- Purpose: what workflow the skill performs.
- Prerequisites: app, OS/session, permissions, files, command family, and setup state.
- Procedure: concise stable steps extracted from the teaching bundle.
- Verification: screenshots, structured observation, output files, or UI state checks needed before success claims.
- Failure handling: what to inspect or retry when app state differs.
- Source evidence: bundle path and the key files used.

Use `references/teaching-evidence.md` only for detailed evidence and raw action summaries. Keep the main `SKILL.md` compact.

## Quality Bar

- Do not copy large video/audio/model files into the new skill.
- Do not embed sensitive screenshot text or personal data from keyframes. If a keyframe contains sensitive content, reference its role without copying it.
- Do not make the new skill depend on exact screen coordinates unless the recorded app workflow truly requires fixed coordinates.
- Do not claim the skill is generally valid for other apps unless the bundle demonstrates a general API or command path.
- If the bundle lacks enough intent to infer a reusable workflow, ask for one short clarification or add an explicit "human instruction required" prerequisite.
