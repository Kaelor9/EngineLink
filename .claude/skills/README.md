# Project skills

These 25 skills come from [addyosmani/agent-skills](https://github.com/addyosmani/agent-skills)
(MIT licence, see `LICENSE.agent-skills`). They are committed here so every Claude Code
session on this repository, cloud sessions included, loads them from the checkout.

- Installed with the official CLI:
  `npx skills add addyosmani/agent-skills --skill '*' -a claude-code --copy -y`
- Upstream commit: `301b02b12bff98e4688e6984af6556802322bbde` (2026-11-01). Per-skill
  content hashes are in `/skills-lock.json`.
- `../references/` holds the pack's shared checklists. The skills link to them as
  `../../references/…`, which matches the upstream layout. The CLI does not copy them
  (upstream issue #361), so they were copied by hand from the same commit.
- Not installed: upstream personas (`agents/`), slash commands (`.claude/commands/`) and
  hooks. EngineLink's own agent rules are in `CLAUDE.md` §3.

How and when the skills are used in EngineLink, and which rules take precedence, is set
in `CLAUDE.md` ("Agent Skills"). Do not edit the skill files by hand. EngineLink-specific
adaptations go in `CLAUDE.md`, so that updates stay clean.

Update (only when the owner asks): run `npx skills update -p`, re-copy `references/` from
the same upstream commit, review the diff, then open a PR.
