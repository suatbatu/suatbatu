# Humanizer

A portable agent skill that strips the tells of AI-generated writing from text,
so the result reads as if a person wrote it. The runtime artifact is a single
Markdown file, `SKILL.md`, so it runs in any harness that loads skill-style
instructions.

It edits for *specificity*, not for casualness. The goal is prose that commits
to a claim, keeps every fact, and stops sounding like it was generated.

## Installation

### Claude Code plugin

```
/plugin marketplace add suatbatu/suatbatu
/plugin install humanizer@humanizer
```

Invoke it as `/humanizer:humanizer`.

> The marketplace resolves the repository's default branch. Until this branch is
> merged there, install from a local clone instead:
>
> ```
> /plugin marketplace add /path/to/your/clone
> ```

### Skills CLI

```bash
npx skills add suatbatu/suatbatu --global
```

Omit `--global` for a project-local install you can commit and share.

### Manual

Any harness can use the skill directly, because the artifact is just
`SKILL.md`. Drop it wherever your harness looks for skills:

```bash
mkdir -p ~/.claude/skills/humanizer
cp SKILL.md ~/.claude/skills/humanizer/
```

## Usage

```
/humanizer

[paste your text here]
```

```
Please humanize this text: [your text]
```

Point it at a file and it rewrites in place:

```
Humanize docs/overview.md
```

### Modes

| Mode | Ask for | You get |
|---|---|---|
| Rewrite | the default | the edited text, nothing else |
| Review | "review" / "check" | a findings list, no edits |
| File | a file path | an in-place edit plus a summary of patterns applied |
| Score | "how AI does this read?" | a 0–10 estimate and the three strongest tells |

## The 30 patterns

**Substance** — what the sentences claim.

| # | Pattern | Example tell |
|---|---|---|
| 1 | Inflated significance and legacy | "stands as a testament to" |
| 2 | Promotional and brochure language | "nestled", "boasts", "vibrant" |
| 3 | Superficial -ing clauses | ", highlighting the challenges facing" |
| 4 | Vague attribution and weasel words | "many experts believe" |
| 5 | Formulaic balanced sections | "Challenges and Future Prospects" |
| 6 | Undue emphasis on coverage | "garnered widespread media attention" |

**Language** — word and clause choice.

| # | Pattern | Example tell |
|---|---|---|
| 7 | AI vocabulary | delve, tapestry, realm, leverage, robust |
| 8 | Copula avoidance | "serves as" instead of "is" |
| 9 | Negative parallelism | "isn't just X, it's Y" |
| 10 | Rule of three padding | "fast, efficient, and performant" |
| 11 | Elegant variation | "the company / the firm / the tech giant" |
| 12 | False ranges | "everything from parsing to logging" |
| 13 | Passive voice and orphaned subjects | "it was decided that" |
| 14 | Hyphenated modifier stacks | "well-crafted, thought-provoking" |

**Formatting** — how it sits on the page.

| # | Pattern | Example tell |
|---|---|---|
| 15 | Em dash overuse | three dashes in one sentence |
| 16 | Boldface decoration | "**critically important**" |
| 17 | Inline-header lists | "- **Speed:** it is fast" |
| 18 | Title case headings | "Getting Started With The API" |
| 19 | Emoji and icon decoration | "## 🚀 Features" |
| 20 | Typographic artifacts | curly quotes in plain-text docs |

**Voice and stance** — the posture behind the words.

| # | Pattern | Example tell |
|---|---|---|
| 21 | Sycophancy | "Great question!" |
| 22 | Assistant artifacts in prose | "Let me know if you'd like me to expand" |
| 23 | Cutoff disclaimers and speculation | "As of my last update" |
| 24 | Signposting | "In this section, we will explore" |
| 25 | Generic positive conclusions | "Overall, X remains important" |
| 26 | Excessive hedging | "could potentially indicate" |
| 27 | Filler openers | "It's important to note that" |
| 28 | Rhetorical question openers | "Ever wondered why?" |
| 29 | Manufactured drama | "But there's a catch." |
| 30 | Aphorism formulas | "X isn't about Y. It's about Z." |

## What it will not do

The skill is built to under-edit rather than over-edit, because a wrong change
costs more than a missed one. It leaves alone:

- quoted and attributed material, code blocks, inline code, URLs, and paths
- technical uses of flagged words (`robust` in statistics, `ecosystem` in biology)
- deliberate promotional copy, where the register is the assignment
- passive voice in method sections and incident reports
- formal legal, academic, and regulatory register
- the marks of human writing it is meant to protect: odd specifics, stated
  opinions, digressions, uneven sentence length, deliberate repetition

It also never invents a fact to replace a vague one. Where a source is missing,
it flags the gap instead of filling it.

## Development

No build step. `SKILL.md` is the product; everything else is packaging.

```bash
python3 scripts/validate-package.py   # dependency-free metadata checks
npx skills add . --list               # confirm skill discovery
claude plugin validate .              # confirm the plugin manifest
```

See [`AGENTS.md`](AGENTS.md) for the maintenance contract, including the rule
that the pattern list, the README table, and the three version fields move
together.

## Version history

- **1.0.0** — First release. 30 patterns across substance, language, formatting,
  and voice, each with a tell, a before/after pair, and a fix. Includes an
  explicit false-positive list, a "signs of human writing" preservation list,
  and four invocation modes.

## Credits

The pattern taxonomy draws on Wikipedia's
[Signs of AI writing](https://en.wikipedia.org/wiki/Wikipedia:Signs_of_AI_writing),
maintained by WikiProject AI Cleanup and available under CC BY-SA 4.0. The
portable single-file packaging follows the approach used by
[blader/humanizer](https://github.com/blader/humanizer).

## License

MIT — see [`LICENSE`](LICENSE).
