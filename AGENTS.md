# AGENTS.md

Guidance for AI coding agents working in this repository.

## What this repo is

A portable agent skill implemented entirely as Markdown. The runtime artifact is
`SKILL.md`: the harness reads its YAML frontmatter and the editor prompt beneath.
There is no build step and no runtime dependency. Keep the wording
harness-neutral, so the skill is not described as working only in one or two
tools.

## Key files

- `SKILL.md` — the skill itself, and the source of truth. Portable frontmatter
  (`name`, `description`, `license`, `metadata.version`) followed by the numbered
  pattern catalogue with before/after examples.
- `README.md` — for humans: installation, usage, the pattern table, version history.
- `.claude-plugin/plugin.json` — Claude Code plugin manifest. Owns the version.
- `.claude-plugin/marketplace.json` — single-repo marketplace entry so
  `/plugin marketplace add suatbatu/suatbatu` resolves. Deliberately carries no
  version field.
- `scripts/validate-package.py` — dependency-free consistency checks, run in CI.

## The maintenance contract

`SKILL.md` and `README.md` must stay in sync. The validator enforces most of
this, so run it before you push.

- **Patterns.** The catalogue is numbered from 1 with no gaps. If you add,
  remove, or renumber one, update the README table and the "The N patterns"
  heading in the same change. Keep existing numbers stable unless you are
  deliberately renumbering, since users and issues cite them.
- **Version.** Three places hold it: `metadata.version` in `SKILL.md`
  frontmatter, the top entry of the README version history, and `version` in
  `plugin.json`. Bump them together. Do not add a version to `marketplace.json`.
- **Line budget.** `SKILL.md` stays under 500 lines. It is loaded into a context
  window on every invocation, so length is a real cost. Cut before you add.
- **Portability.** No `compatibility:`, `allowed-tools:`, `model:`, or top-level
  `version:` keys in frontmatter. They are rejected by the validator because
  they bind the skill to one harness.
- **Non-obvious fixes.** If you change the prompt to correct a specific failure
  (a repeated mis-edit, a tone shift, a false positive), add a line to the README
  version history saying what broke and what fixed it.

## Editing SKILL.md

The prompt below the frontmatter is the product. Edit it like a careful
instruction document, not like code.

- Preserve valid YAML frontmatter, including indentation.
- Every pattern entry carries a **Tell**, and where it helps, a **Before**, an
  **After**, and a **Fix**. Examples do more work than rules; keep them concrete
  and short.
- Additions to the false-positive list are as valuable as new patterns. The skill
  fails most often by over-editing.
- The file argues against em dashes, filler openers, and rule-of-three padding.
  Do not introduce them into its own prose. Examples demonstrating a bad pattern
  are the exception.

## Validation

```bash
python3 scripts/validate-package.py
npx skills add . --list
claude plugin validate .
```
