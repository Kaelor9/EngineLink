# Claude Code skills and Ponytail — on-demand reference

Read only when a task calls for selecting a specialist skill or verifying Ponytail's rules. `CLAUDE.md` remains the always-on source for mission, safety, GitHub and release rules.

### Agent Skills (`.claude/skills/`)

Addy Osmani's 25 Agent Skills are installed project-wide (source, version and licence: `.claude/skills/README.md`; shared checklists: `.claude/references/`). Claude Code activates them from their descriptions; do not run `using-agent-skills` as a second always-on router. Load a skill only when its trigger clearly matches, usually 0–2 per task, and skip them for tiny fixes and pure explanations.

**Precedence:** the owner's latest request → §6 constraints and `HANDOVER.md` → the rest of this file → skill text. A skill is a process aid; where it conflicts, follow EngineLink's rules and say so briefly. Skills never authorise new transmit paths, data migrations, releases, or extra questions.

| EngineLink situation | Skill | EngineLink adaptation |
|---|---|---|
| Bug, CI failure, field/device report | `debugging-and-error-recovery` | Read the GitHub device/engine reports first; reproduce before fixing |
| Change to host-testable logic (`fuelcal.h`, `diagrules.h`, `shiftrules.h`, `tracefold.h`, `httputil.h`, `otaheader.h`, `ledpatterns.h`) | `test-driven-development` | Tests live in `tools/test_*.cpp` / `tools/test_*.py` and run in CI |
| Multi-file feature or fix | `planning-and-task-breakdown`, `incremental-implementation` | Short internal plan; keep each slice building and the board usable |
| Phone app (`firmware/ui/app.html`) | `frontend-ui-engineering` | iPhone PWA safe areas, reduced motion, `—` for missing data; regenerate `ui.h` |
| Checking the app or website in a browser | `browser-testing-with-devtools` | Use the available browser tooling (Playwright in cloud sessions) and the demo |
| New or changed `/api/...` endpoint or JSON | `api-and-interface-design` | Keep existing endpoints and fields compatible with the app and demo |
| Review before opening/merging a PR | `code-review-and-quality` | Owner-invoked `/code-review` or `/simplify` take priority when requested |
| Wi-Fi, OTA, GitHub token, HTTP parsing, J1708 input | `security-and-hardening` | Never log or expose secrets; keep bounds on all parsed input |
| Throughput, heap/PSRAM, task timing | `performance-optimization` | Measure with device reports or host tests before and after |
| Reports, logs, counters | `observability-and-instrumentation` | Bounded buffers; no flash/SD wear from chatty logs |
| ESP-IDF / Arduino core / library APIs | `source-driven-development` | Check official docs (Context7) for ESP32 Arduino core 3.3.x |
| High-risk firmware, concurrency, OTA, KAD polling | `doubt-driven-development` | Independent review within the §3 agent limits |
| Workflows in `.github/` | `ci-cd-and-automation` | Only when asked or when CI is broken |
| Commits, branches, PRs | `git-workflow-and-versioning` | §7 and the session's branch rules win |
| Release / update requested | `shipping-and-launch` | `docs/RELEASING.md` is authoritative; no feature flags or staged-rollout infrastructure |
| Removing a feature, setting or storage format | `deprecation-and-migration` | Needs explicit approval and a recovery path (§6 data integrity) |
| Docs and recorded decisions | `documentation-and-adrs` | Update `HANDOVER.md`, `docs/STATUS.md`, `docs/TODO.md`; no new ADR folder unless asked |
| Large new feature with unclear scope | `spec-driven-development`, `interview-me`, `idea-refine` | Write the spec yourself from the owner's Danish description; at most one focused question (§2); ideas go to `docs/TODO.md` |
| Context feels thin or stale | `context-engineering` | `HANDOVER.md` first, then Graphify as described above |
| Code clarity, only in code you are already changing | `code-simplification` | No unrelated refactors |
| Defining a quality bar | `constraint-driven-development` | §5/§6 already are the bar; don't add constraint files unless asked |

Skills may suggest new folders (`docs/ideas/`, `docs/intent/`, `docs/runbooks/`, ADRs, specs); create them only when the owner asks. Talk to the owner in Danish whatever a skill's templates say. Update the skills only on request: `npx skills update -p` (pinned in `skills-lock.json`), then review the diff.

### Ponytail (`.claude/skills/ponytail/`)

Ponytail (DietrichGebert/ponytail, MIT, `LICENSE.ponytail`) is installed as one project skill: only the core `ponytail` prompt. Its plugin hooks, band, statusline and the `ponytail-review`/`-audit`/`-debt`/`-gain`/`-help` skills are **not** installed, so there is no persistent mode and nothing is sent anywhere. The same precedence as above applies.

- **Use it on every implementation or fix** (it may load automatically, or via `/ponytail`): understand the problem first, then take the first rung that fully works — not needed → already in EngineLink (an existing helper, endpoint, pattern, test harness) → standard library / Arduino-ESP32 core → an already used library → one clear line → only then the minimum new code. Prefer deleting or reusing over adding; keep the existing structure, endpoints, storage formats and file layout.
- **Default level `full`.** `lite` when the owner wants options. `ultra` only when the owner asks for it; even then, push back at most with the one focused question from §2.
- **Small, not incomplete:** the change still reaches every caller, test, `ui.h` regeneration and doc that §5/§7 require. Ponytail's "never cut" list is extended for EngineLink with all of §6, the bounds/timeouts/`engineMx` discipline in §5, `—` for missing data, OTA rollback, and offline/AP recovery. Never shrink code by removing a safeguard, a check or a test.
- **Tests:** its "one small test for non-trivial logic" goes in the existing `tools/test_*` harnesses run by CI; no new test frameworks.
- **Overlap:** Ponytail sizes the change; `code-simplification` is only for code already being changed; `code-review-and-quality` (or `/code-review`) still reviews before a PR. For visual choices `HANDOVER.md`'s "show alternatives first" rule wins over "smallest version".
- **`shortcut:` comments** only for a real, known limit, in the form `shortcut: <limit>, <when to upgrade>`.
- **Reply:** Ponytail's closing "what was skipped/not checked and any risk" goes in the Danish **Mangler** section (§8), not as an extra English trailer.
- Update only on request: `npx skills update -p` (also pinned in `skills-lock.json`), then review the diff.
