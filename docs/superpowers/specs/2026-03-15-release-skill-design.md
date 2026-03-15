# Release Skill Design

**Date:** 2026-03-15
**Type:** Project-specific Claude Code Skill
**Location:** `~/.claude/skills/release/SKILL.md`

## Purpose

Interactive release checklist skill for TTCut-ng that guides the user step-by-step through the entire release process — from pre-flight checks to GitHub Release publication. Invoked via `/release`.

## Design Principles

- **Interactive**: Every step requires user confirmation before execution
- **Idempotent**: Can be re-run if a step fails — checks current state before acting
- **No destructive defaults**: Push, tag, and release always ask first
- **Single Source of Truth**: Version lives in `ttcut-ng.pro`, all other locations are derived

## Scope

### In Scope
- Pre-flight validation (branch, working tree, open branches, security issues, translations)
- Semantic version suggestion based on commit history
- Version bump across all files
- CHANGELOG.md entry generation from commits
- README.md review
- Wiki update check and push (`/usr/local/src/TTCut-ng.wiki`)
- Full rebuild and smoke test
- Debian package build (`build-package.sh`)
- Git commit, tag, push
- GitHub Release creation (`gh release create`)

### Out of Scope
- .deb upload to external repositories
- Automated testing beyond smoke test (binary starts)
- Branch creation or merge (assumed done before `/release`)
- Wiki content writing (skill prompts user to update, doesn't auto-generate)

## Phases and Steps

### Phase 1 — Pre-Flight Checks

| # | Step | Action | Failure |
|---|------|--------|---------|
| 1 | Branch check | Verify on `master` | Abort with message |
| 2 | Working tree | `git status` — must be clean | Show dirty files, ask to proceed |
| 3 | Open branches | List feature branches not yet merged | Warn, ask to proceed |
| 4 | Security issues | Grep TODO.md for CRITICAL/HIGH security items | Warn with count |
| 5 | Translations | Run `lrelease` on `.ts` files, check for untranslated strings | Warn with details |
| 6 | Last tag analysis | `git log v0.X.Y..HEAD --oneline` — show commits since last release | Display for review |

### Phase 2 — Version Bump

| # | Step | Action |
|---|------|--------|
| 7 | Version suggestion | Analyze commits: "Fix"→Patch, "Add"/"Feature"→Minor. Present suggestion, user confirms or overrides |
| 8 | Update `ttcut-ng.pro` | Edit `VERSION = X.Y.Z` line |
| 9 | Scan for old version | Grep entire project for old version string, show matches. User decides which to update |

### Phase 3 — Documentation

| # | Step | Action |
|---|------|--------|
| 10 | CHANGELOG.md | Generate entry from commits since last tag. Format: version header, date, grouped by type (Features, Fixes, Changes). User can edit before saving |
| 11 | README.md | Show current README, ask user if updates needed. If yes, user describes changes |
| 12 | Wiki check | `cd /usr/local/src/TTCut-ng.wiki && git status` — if dirty or commits ahead, list changes. Ask user which pages need updates for this release |

### Phase 4 — Build & Verify

| # | Step | Action | Failure |
|---|------|--------|---------|
| 13 | Full rebuild | `make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)` | Abort — must fix before release |
| 14 | Smoke test | Run `./ttcut-ng --version` or similar quick check | Abort |
| 15 | Debian package | Run `build-package.sh` (pipe changelog message) | Abort |
| 16 | Cleanup | `git checkout -- debian/changelog` | Warn |

### Phase 5 — Publish

| # | Step | Action |
|---|------|--------|
| 17 | Commit | Stage all changed files, commit "Release vX.Y.Z" |
| 18 | Tag | `git tag vX.Y.Z` |
| 19 | Push repo | `git push origin master --tags` — with confirmation |
| 20 | GitHub Release | `gh release create vX.Y.Z --title "vX.Y.Z" --notes-file <changelog-excerpt>` |
| 21 | Wiki push | If wiki has changes: `cd /usr/local/src/TTCut-ng.wiki && git push` — with confirmation |

## Version Suggestion Algorithm

```
commits = git log <last-tag>..HEAD --oneline
if any commit starts with "Add" or contains "feature" or "support":
    suggest MINOR bump (0.X.0 → 0.X+1.0)
else:
    suggest PATCH bump (0.X.Y → 0.X.Y+1)
```

User always has final say on version number.

## CHANGELOG Entry Format

```markdown
## vX.Y.Z (YYYY-MM-DD)

### Features
- Description (from "Add ..." commits)

### Fixes
- Description (from "Fix ..." commits)

### Changes
- Description (other commits)
```

## File Locations

| File | Purpose |
|------|---------|
| `~/.claude/skills/release/SKILL.md` | Skill definition |
| `ttcut-ng.pro` | Version source of truth |
| `CHANGELOG.md` | Release history |
| `README.md` | Project overview |
| `TODO.md` | May reference versions |
| `/usr/local/src/TTCut-ng.wiki/` | GitHub Wiki local clone |
| `build-package.sh` | Debian package builder |

## Error Handling

- On any step failure: display error, ask user how to proceed (retry/skip/abort)
- Never auto-skip failed steps
- If build fails, abort release — code must compile
- If push fails (network), allow retry

## Constraints

- Skill does NOT run `sudo` commands
- Skill does NOT upload .deb to external repos
- Wiki path is hardcoded to `/usr/local/src/TTCut-ng.wiki`
- Requires `gh` CLI for GitHub Release
- Requires `bear` for compile_commands.json generation
