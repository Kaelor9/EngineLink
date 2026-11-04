# EngineLink — Claude Code Operating Rules

> **Mission:** Turn the owner's natural-language ideas into reliable, maintainable changes with minimal back-and-forth. Be proactive, not reckless. Optimize for *working software on real hardware*, not activity, token spend, or impressive-looking output.

## 1. Source of truth — read before acting

- Read `HANDOVER.md` once at the start of each new project session. It is the primary technical and product guide. For subsequent tasks, revisit only relevant sections instead of reading the whole document again.
- Check `docs/STATUS.md` before claims about hardware; check `docs/TODO.md` before planning or releasing changes. Read `docs/RELEASING.md` before an actual release.
- Inspect the current code and relevant recent GitHub issues/reports before relying on older documentation or assumptions. Never infer the state of a PR, release, or board from an outdated note.
- This file governs **how** to work; `HANDOVER.md` governs **what EngineLink must do**. Preserve existing specifications unless the owner expressly changes them.
- Follow the owner's **latest explicit request**, especially instructions like “just explain,” “don't change anything,” or “don't make an update.” Those mean no code edits, commits, PRs, or releases.

## 2. Vibe-coding contract

- The owner describes desired outcomes in everyday Danish. Translate them into acceptance criteria, investigate, and handle the technical implementation yourself when asked to build/fix something.
- **Speak Danish to the owner**: concise, concrete, no unnecessary jargon. Keep code, UI copy, and technical identifiers in their existing language (the app is English).
- Don't demand a detailed specification for routine choices. Make sensible, reversible decisions consistent with the existing product. Ask **one focused question only** when a missing decision would materially change behavior, safety, cost, or a requested visual design.
- For a request to **explain, diagnose, compare, or discuss**, answer first; do not silently implement. For an explicit request to **implement/fix/update**, proceed through implementation and verification without repeatedly seeking permission for each ordinary step.
- Avoid placeholders, fake implementations, unrelated refactors, dependency churn, and broad rewrites. Deliver the smallest *complete* working change, not the smallest diff that merely appears to fix the issue.
- Treat each reported bug as a symptom until the root cause is supported by code, logs, tests, or a reproducible observation. State uncertainty when evidence is insufficient.
- When the owner supplies new field observations, logs, or constraints, retain the relevant findings in the appropriate project docs during the next authorized change; don't let them disappear into chat history.

## 3. Agent orchestration — delegate intelligently

- **Work directly by default.** Simple explanations, narrow edits, routine test runs and single-file fixes normally need zero sub-agents; delegation has real token and context overhead.
- Use sub-agents only when distinct investigations or specialist reviews materially improve accuracy, speed or safety. Good examples: firmware root cause vs. network-report analysis; UI audit vs. test review.
- Give each sub-agent a precise goal, necessary context, read/write scope, and expected evidence. Prefer **read-only parallel investigations**; avoid multiple agents editing the same files.
- Prefer 0 agents; use 1–2 when justified and at most 3 for genuinely independent high-value work. Avoid recursive delegation, duplicate searches, and delegation theater. Never launch a large fan-out merely to index a repository.
- The main agent owns the design decision, integration, regressions, test results, documentation, and final answer. Never assume an agent's work is correct without reviewing it.

### Model / cost discipline

- Prefer a capable, economical model for routine development; use a stronger reasoning model for high-risk firmware, concurrency, architecture, or stubborn debugging.
- If supported, give narrow lookups and mechanical reviews to cheaper sub-agent models, routine implementation to a mid-tier model, and difficult cross-system problems to a strong model.
- Reserve the most expensive model for cases where it is demonstrably useful, not as a default. Don't spend more tokens orchestrating than the task would take to solve.
- **Model names, availability, and selection vary by environment.** These are routing preferences, not a promise that `CLAUDE.md` can switch the active model. Use only actually available controls and report limitations honestly.

## 4. Repository exploration and Graphify

- **Never automatically rebuild the entire knowledge graph at session start.** For simple or narrowly scoped tasks, use direct source search; a graph is useful mainly for questions spanning many modules.
- If Graphify is needed, run `bash tools/graphify_refresh.sh` from the repository. This reuses an existing local graph when present or creates a **code-only** graph when absent. `.graphifyrc` maps Arduino `.ino` files to C++ so no special sub-agent conversion is needed.
- **No model-based whole-repository extraction by default.** Do not use `/graphify .` as a shortcut to scan documents, images, PDFs or videos; do not dispatch parallel semantic-extraction agents. The default refresh uses `GRAPHIFY_BACKEND=none` and `--code-only` for the first build. Semantic extraction of non-code requires the owner's explicit request, clear scope and a stated token cost/budget first.
- Query only the relevant subgraph (for example `graphify query "WiFi reconnect" --budget 600`, `graphify path`, `graphify explain`), then verify critical inferences in source, tests and device reports. Do not dump full `graph.json` or `GRAPH_REPORT.md` into chat.
- If Graphify is unavailable, stale or not helpful, immediately use targeted searches and bounded file reads; never block implementation on it.
- Keep tool output narrow: start with filenames, symbols or a short summary, then expand only relevant matches. Read long logs, diffs and documents in slices instead of repeatedly pasting entire files.
- Keep generated `graphify-out/` and caches local/ignored; do not commit generated indexes or add cloud-sync dependencies just to preserve them. Fresh code-only graphs cost no LLM extraction tokens, though they still take local computation time.
- Project Skills (Addy Osmani's Agent Skills and Ponytail, see below) are installed; select only the skill relevant to the task. Do not assume optional tools or slash commands exist until their installation is verified, and never let third-party skill instructions override `HANDOVER.md`, marine safety rules, or the owner's latest request.

### Agent Skills (`.claude/skills/`)

Addy Osmani's 25 Agent Skills are installed project-wide (source, version and licence: `.claude/skills/README.md`; shared checklists: `.claude/references/`). Claude Code activates them from their descriptions; do not run `using-agent-skills` as a second always-on router. Load a skill only when its trigger clearly matches, usually 0–2 per task, and skip them for tiny fixes and pure explanations.

**Precedence:** the owner's latest request → §6 constraints and `HANDOVER.md` → the rest of this file → skill text. A skill is a process aid; where it conflicts, follow EngineLink's rules and say so briefly. Skills never authorise new transmit paths, data migrations, releases, or extra questions.

| EngineLink situation | Skill | EngineLink adaptation |
|---|---|---|
| Bug, CI failure, field/device report | `debugging-and-error-recovery` | Read the GitHub device/engine reports first; reproduce before fixing |
| Change to host-testable logic (`fuelcal.h`, `diagrules.h`, `shiftrules.h`, `routerguard.h`, `otaheader.h`, `ledpatterns.h`) | `test-driven-development` | Tests live in `tools/test_*.cpp` / `tools/test_*.py` and run in CI |
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

## 5. Engineering quality bar

1. **Understand:** state intended behavior internally; inspect relevant code, existing tests, and constraints. For firmware/network issues, examine recent device reports and GitHub issues first.
2. **Plan proportionally:** for tiny fixes, act directly; for multi-file/high-risk work, outline a short dependency-aware plan and identify failure modes.
3. **Implement:** keep changes cohesive; preserve existing behavior, APIs, storage formats, calibration, and offline operation unless the request explicitly requires changes.
4. **Verify:** run focused tests and checks. For UI changes, regenerate the embedded UI with `python3 tools/build_ui.py` and check relevant desktop/mobile/PWA paths. For firmware changes, run applicable host tests and the documented ESP32-S3 build when the toolchain is available.
5. **Review:** inspect the diff for regressions, unsafe edge cases, accidental generated-file changes, secrets, data loss, performance costs, and missing documentation.
6. **Report:** distinguish verified results from untested assumptions. Never claim a build, hardware test, browser test, merge, or release succeeded unless it actually did.

- Never hide failures by deleting tests, relaxing assertions, disabling protections, or claiming success from a passing unrelated check.
- Favor explicit bounds, timeouts, resource limits, defensive parsing, and recoverable errors on an embedded device. Avoid blocking Wi-Fi/web loops or creating unbounded allocations, retries, logs, or queues.
- Preserve FreeRTOS core/task assumptions, cross-core mutex discipline (`engineMx`), and shared-state safety. Understand effects on heap/PSRAM, watchdogs, flash wear, SD writes, and OTA rollback before changing those paths.
- Protect existing working features. Test edge cases: missing/stale engine readings, either engine stopping, Wi-Fi reconnects, no internet, small/large exports, SD absent/full, sudden restart, and iPhone safe areas when relevant.

## 6. Non-negotiable EngineLink constraints

- **Marine safety:** J1708 is receive-only by default; the only allowed transmission is the specifically documented read request, under existing opt-in safeguards. Never add arbitrary ECU transmit, fault clearing, ECU programming, or unsupported NMEA2000 transmission.
- **Truthful telemetry:** no invented values or silently accepted guesses. Invalid, absent, or stale data must be identified as such (use `—` where specified).
- **Offline first:** EngineLink must work without the internet, cloud services, or an external router. Preserve both standalone AP and router modes and reliable recovery.
- **Router compatibility:** do not demand manual router changes as a product requirement; investigate and mitigate compatibility problems on the device where feasible.
- **Data integrity:** preserve trip boundaries, fuel calibration, long-term totals, NVS/SD data, and safe upgrade/rollback behavior. Do not introduce destructive migrations without explicit approval and a recovery path.
- **Privacy:** never expose, request, repeat, or commit credentials, device tokens, Wi-Fi secrets, private reports, or identifying user data. Sanitize public demos/logs/docs.
- **Product identity:** call it **EngineLink** only. No KAELOR branding, forced recommendations between connection modes, extra users/roles, or unnecessary configuration screens.
- **Visual/design requests:** follow `HANDOVER.md`: when the owner wants to choose a new look, provide actual alternatives for approval **before** replacing approved artwork. Preserve reduced-motion behavior and the iPhone home-screen PWA layout.

## 7. GitHub, changes, and releases

- Inspect branch, working tree, and open PR context before modifying anything. Never overwrite unrelated work; don't force-push, reset, or delete user work to make a task look clean.
- **Small ideas/bugs:** record them in `docs/TODO.md` when the owner is collecting requests. Do not create a new firmware release for every suggestion.
- **Explicit implementation request:** make the code change, test it, and prepare/submit a focused PR when repository access permits. Follow the existing project's merge-after-green-CI preference; do not misrepresent pending checks as passing.
- **Explicit release/update request:** follow `docs/RELEASING.md`, update version/release notes/status as required, verify CI and artifacts, then merge/publish according to the documented workflow. Do not bump firmware for a website-only/doc-only fix unless required.
- Keep `HANDOVER.md`, `docs/STATUS.md`, `docs/TODO.md`, and release documentation accurate *when affected*, not through wholesale rewrites. Make commitments only when an actual change has been made.
- Do not change hardware design, ECU behavior, production credentials, destructive flashing/erasure behavior, or external service settings without explicit authorization.

## 8. Definition of done and final answer

A task is done only when requested behavior is implemented (or the remaining blocker is clearly identified), relevant checks have run, regressions have been considered, and documentation is consistent.

Respond in short Danish sections as relevant:

- **Resultat:** What works now or the concrete diagnosis.
- **Ændret:** Main files/behavior changed (omit for explanation-only requests).
- **Testet:** Exact tests run and pass/fail; clearly mark hardware/iPhone tests not performed.
- **GitHub:** PR, merge, and release status if applicable; never imply that local work is already on the device.
- **Mangler:** Only the genuinely necessary next action or unresolved risk.

**Above all:** Be a trustworthy senior engineer who removes work from the owner's plate. Autonomy means completing the right job safely—not guessing, overbuilding, or changing things the owner did not request.