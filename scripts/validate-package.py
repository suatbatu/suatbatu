#!/usr/bin/env python3
"""Validate the Humanizer package surfaces without external dependencies."""

from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKILL = (ROOT / "SKILL.md").read_text()
README = (ROOT / "README.md").read_text()
PLUGIN = json.loads((ROOT / ".claude-plugin" / "plugin.json").read_text())
MARKETPLACE = json.loads((ROOT / ".claude-plugin" / "marketplace.json").read_text())

LINE_BUDGET = 500


def require(match: re.Match[str] | None, message: str) -> re.Match[str]:
    if match is None:
        raise SystemExit(f"FAIL: {message}")
    return match


frontmatter = require(
    re.match(r"\A---\n(.*?)\n---\n", SKILL, re.DOTALL),
    "SKILL.md must start with YAML frontmatter",
).group(1)

# Keys that bind the skill to a single harness break portability.
for nonportable_key in ("compatibility:", "allowed-tools:", "model:"):
    if re.search(rf"(?m)^{re.escape(nonportable_key)}", frontmatter):
        raise SystemExit(f"FAIL: remove nonportable frontmatter key: {nonportable_key[:-1]}")

# A top-level `version:` is not portable across Agent Skills hosts; it lives under metadata.
if re.search(r"(?m)^version:", frontmatter):
    raise SystemExit("FAIL: move the top-level version key under metadata")

for required_key in ("name:", "description:", "license:"):
    if not re.search(rf"(?m)^{re.escape(required_key)}", frontmatter):
        raise SystemExit(f"FAIL: SKILL.md frontmatter is missing {required_key[:-1]}")

skill_version = require(
    re.search(r'(?m)^\s+version:\s*["\']([^"\']+)["\']\s*$', frontmatter),
    "SKILL.md metadata.version is missing",
).group(1)
readme_version = require(
    re.search(r"(?m)^- \*\*([0-9]+\.[0-9]+\.[0-9]+)\*\*", README),
    "README version history is missing",
).group(1)

versions = {skill_version, readme_version, str(PLUGIN.get("version", ""))}
if len(versions) != 1:
    raise SystemExit(f"FAIL: version mismatch across SKILL/README/plugin: {sorted(versions)}")

# marketplace.json deliberately carries no version so plugin.json stays the single source.
if "version" in MARKETPLACE.get("plugins", [{}])[0]:
    raise SystemExit("FAIL: marketplace.json must not pin a version; plugin.json owns it")

if PLUGIN["name"] != require(
    re.search(r"(?m)^name:\s*(\S+)\s*$", frontmatter), "SKILL.md name is missing"
).group(1):
    raise SystemExit("FAIL: plugin.json name does not match SKILL.md name")

# The numbered pattern list is the contract between SKILL.md and the README table.
pattern_numbers = [int(number) for number in re.findall(r"(?m)^### ([0-9]+)\. ", SKILL)]
if not pattern_numbers:
    raise SystemExit("FAIL: SKILL.md defines no numbered patterns")
expected = list(range(1, len(pattern_numbers) + 1))
if pattern_numbers != expected:
    raise SystemExit(f"FAIL: patterns must be numbered 1-{len(pattern_numbers)}, found {pattern_numbers}")

readme_numbers = {int(number) for number in re.findall(r"(?m)^\| ([0-9]+) \|", README)}
if readme_numbers != set(expected):
    missing = sorted(set(expected) - readme_numbers)
    extra = sorted(readme_numbers - set(expected))
    raise SystemExit(f"FAIL: README pattern table out of sync (missing {missing}, extra {extra})")

if f"## The {len(expected)} patterns" not in README:
    raise SystemExit(f"FAIL: README pattern heading must read 'The {len(expected)} patterns'")

line_count = len(SKILL.splitlines())
if line_count > LINE_BUDGET:
    raise SystemExit(f"FAIL: SKILL.md is {line_count} lines, over the {LINE_BUDGET}-line portability budget")

print(f"Humanizer v{skill_version} is valid: {len(expected)} patterns, {line_count}/{LINE_BUDGET} lines")
