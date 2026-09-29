# CI execution and cost controls

Jarvis-HOOK — 2026-09-29. These workflows validate source and build candidates;
they do not deploy a DLL, run a live game session or publish a release.

## Automatic checks

Push checks are limited to `main`. Pull requests use the same file filters, so
feature branches do not run both push and pull-request copies of each workflow.

| Changed files | Automatic work |
| --- | --- |
| Root READMEs, ordinary documentation or roadmap only | No build or artifact upload |
| Runtime, contracts, Workshop or language-test code | One Windows 2022 job for native MSVC regressions and the Release DLL; one Linux job for portable contracts and sanitizers |
| `src/sin/**` or its root .NET build configuration | One Linux .NET 8 compilation; no DLL build |
| Context tooling, agent policy, `docs/ai/**` or `docs/superpowers/**` | Development repository only: one Linux/Python 3.13 job for policy, retrieval and navigation checks |
| A workflow file | Its own workflow, to validate the changed configuration |

Markdown files within runtime, Workshop, language tools or SIN do not trigger
native compilation. Mixed changes run the union of the applicable workflows.
The public repository uses the three build/language workflows; it does not
contain the development-only context workflow. Each repository validates its
own source revision, which may differ from the other repository.

The Windows language harness moved from `text-languages.yml` into `build.yml`.
It still checks the MSVC adapters, bounded file IO, F8/Workshop and native
language regressions before the full DLL build. The pinned vcpkg baseline and
dependency setup are retained. The same DLL is no longer compiled and uploaded
by two workflows for a single push.

## Manual checks and packaging

Open **Actions → the workflow → Run workflow**, select the desired source ref:

- `build`: runs native tests and creates the Release DLL. The optional
  `package_arcana` checkbox defaults to **false**; enable it to also build and
  upload the large standalone Arcana ZIP. Routine pushes never produce this ZIP.
- `text-languages`: runs the portable contracts and memory sanitizers.
- `build-sin`: compiles the offline SIN injector. Linux compilation does not
  establish that the Windows injector runs correctly against a game process.
- `context-tools` (development repository): defaults to one Linux/Python 3.13
  job. Enable `full_matrix` for the original six combinations: Linux and Windows,
  each with Python 3.11, 3.13 and 3.14. Use this for context compatibility changes.

Before publishing a candidate, validate the chosen revision with the applicable
native and portable checks, request Arcana packaging if needed, and retain the
release's source/hash/provenance checks. A passing job is RT0/RT1 evidence only;
RT2 and promotion remain separate. These controls do not waive release checks.

## Runtime and storage bounds

- A newer run cancels older running/pending work for the same workflow and ref.
  Different workflows keep separate concurrency groups.
- The combined Windows job has a **25-minute** timeout. Every Linux job and each
  optional context-matrix job has a **10-minute** timeout. Matrix timeouts apply
  per job, not to the sum of the six jobs.
- The small DLL artifact is retained for **7 days**. Native/portable validation
  logs and manually requested Arcana ZIPs are retained for **3 days**. Native
  test executables and duplicate DLL artifacts are no longer uploaded.
- There are no scheduled runs or automatic retries. Manual full-matrix runs and
  repeated manual builds still consume runner time.
- Retention changes apply to new workflow artifacts. Existing artifacts and
  published release assets are not deleted or rewritten.

These are execution/storage controls, not a monetary spending cap. GitHub
account billing and spending-limit settings are not changed. Do not retry all
historical billing-blocked jobs; validate the current affected revision once.

Neither repository required status checks on `main` when this policy was
introduced. If protection is added later, account for GitHub's pending-check
behavior on path-filtered workflows; do not require a filtered workflow on
every pull request without an always-present aggregate check.

References: [GitHub path filters and manual inputs](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax),
[workflow concurrency](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency).
