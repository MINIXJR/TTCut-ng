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
- README.md and CLAUDE.md review
- Wiki update check, commit, and push (`/usr/local/src/TTCut-ng.wiki`)
- Full rebuild and smoke test
- Debian package build (`build-package.sh`) with .deb attached to GitHub Release
- Git commit, tag, push
- GitHub Release creation (`gh release create`)

### Out of Scope
- .deb upload to external package repositories (apt repos etc.)
- Automated testing beyond smoke test
- Branch creation or merge (assumed done before `/release`)
- Wiki content writing (skill prompts user to update, doesn't auto-generate)

## Phases and Steps

### Phase 1 — Pre-Flight Checks

| # | Step | Action | Failure |
|---|------|--------|---------|
| 1 | Branch & merge state | Verify on `master`. If not, list current branch and unmerged feature branches with suggested merge commands | Abort with guidance |
| 2 | Working tree | `git status` — must be clean | Show dirty files, ask to proceed |
| 3 | GitHub CLI auth | `gh auth status` — verify authenticated | Abort — needed for GitHub Release |
| 4 | Security issues | Grep TODO.md for CRITICAL/HIGH security items | Warn with count, ask to proceed |
| 5 | Translations | Run `lupdate ttcut-ng.pro` to update .ts from source, then check for `type="unfinished"` entries in .ts XML files | Warn with details |
| 6 | Last tag analysis | `git log v0.X.Y..HEAD --oneline` — show commits since last release | Display for review |

### Phase 2 — Version Bump

| # | Step | Action |
|---|------|--------|
| 7 | Version suggestion | Analyze commits since last tag (see algorithm below). Present suggestion, user confirms or overrides |
| 8 | Update `ttcut-ng.pro` | Edit `VERSION = X.Y.Z` line |
| 9 | Scan for old version | Grep project for old version string including `CLAUDE.md`, `TODO.md`, `README.md`. Show matches, user decides which to update |

### Phase 3 — Build & Verify

| # | Step | Action | Failure |
|---|------|--------|---------|
| 10 | Full rebuild | `make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)` | Abort — must fix before release |
| 11 | Smoke test | `file ./ttcut-ng` (verify ELF binary) + `ldd ./ttcut-ng \| grep "not found"` (verify shared libs) | Abort |
| 12 | Debian package | `echo "Release vX.Y.Z" \| bash build-package.sh` — note .deb output path | Abort |
| 13 | Cleanup | `git checkout -- debian/changelog` | Warn |

### Phase 4 — Documentation

| # | Step | Action |
|---|------|--------|
| 14 | CHANGELOG.md | Generate entry from commits since last tag. Format: version header, date, grouped by type (Features, Fixes, Changes). User can edit before saving |
| 15 | TODO.md | Check for items completed in this release — prompt user to move to Completed section |
| 16 | README.md | Ask user if updates needed for this release |
| 17 | CLAUDE.md | Check if "Recent Fixes and Features" section needs new entries |
| 18 | Wiki check | `cd /usr/local/src/TTCut-ng.wiki && git status` — show state. Ask user which pages need updates (Changelog, Features, etc.) |

### Phase 5 — Publish

| # | Step | Action |
|---|------|--------|
| 19 | Commit | Stage specific files (`git add ttcut-ng.pro CHANGELOG.md ...`), show `git diff --cached` for review, commit "Release vX.Y.Z" |
| 20 | Tag | `git tag vX.Y.Z` |
| 21 | Push repo | `git push origin master --tags` — with confirmation |
| 22 | GitHub Release | `gh release create vX.Y.Z --title "vX.Y.Z" --notes-file <changelog-excerpt> /path/to/ttcut-ng_*.deb` |
| 23 | Wiki commit & push | If wiki has changes: `cd /usr/local/src/TTCut-ng.wiki && git add -A && git commit -m "Update wiki for vX.Y.Z" && git push` — with confirmation |

## Version Suggestion Algorithm

```
commits = git log <last-tag>..HEAD --oneline

if any commit starts with "Add" or contains "feature" or "support":
    suggest MINOR bump (0.X.Y → 0.X+1.0)
elif any commit starts with "Fix" and touches CRITICAL/HIGH security items:
    suggest MINOR bump
elif any commit starts with "Fix", "Update", "Improve", "Enhance":
    suggest PATCH bump (0.X.Y → 0.X.Y+1)
else:
    suggest PATCH bump
```

MAJOR bumps (pre-1.0 → 1.0) are only suggested manually by the user. User always has final say on version number.

## CHANGELOG Entry Format

```markdown
## vX.Y.Z (YYYY-MM-DD)

### Features
- Description (from "Add ..." commits)

### Fixes
- Description (from "Fix ..." commits)

### Changes
- Description (other commits: Update, Improve, Refactor, etc.)
```

## File Locations

| File | Purpose |
|------|---------|
| `~/.claude/skills/release/SKILL.md` | Skill definition |
| `ttcut-ng.pro` | Version source of truth |
| `CHANGELOG.md` | Release history |
| `README.md` | Project overview |
| `CLAUDE.md` | Developer guide — version refs in "Recent Fixes" |
| `TODO.md` | Feature roadmap — completed items reference versions |
| `/usr/local/src/TTCut-ng.wiki/` | GitHub Wiki local clone |
| `build-package.sh` | Debian package builder |

## Error Handling

- On any step failure: display error, ask user how to proceed (retry/skip/abort)
- Never auto-skip failed steps
- If build fails, abort release — code must compile
- If push fails (network), allow retry

### Partial Publish Recovery

If Steps 19-20 succeed (commit + tag) but Step 21-22 fail:
- To retry: just re-run the failed step
- To rollback: `git tag -d vX.Y.Z && git reset --soft HEAD~1` — removes tag and uncommits (changes preserved in staging)

## Constraints

- Skill does NOT run `sudo` commands
- Skill does NOT upload .deb to external apt repositories
- Wiki path is hardcoded to `/usr/local/src/TTCut-ng.wiki`
- Requires `gh` CLI (authenticated) for GitHub Release
- Requires `bear` for compile_commands.json generation
