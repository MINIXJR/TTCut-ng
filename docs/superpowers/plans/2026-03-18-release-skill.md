# Release Skill Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a Claude Code skill (`/release`) that interactively guides TTCut-ng releases through 5 phases: pre-flight, version bump, build, documentation, and publish.

**Architecture:** Single SKILL.md file at `~/.claude/skills/release/SKILL.md`. The skill is a structured prompt — Claude follows it step-by-step, asking user confirmation at each step. Wiki page reference list in a separate file to keep SKILL.md under 500 lines.

**Tech Stack:** Claude Code skill (Markdown), git, gh CLI, qmake/make, build-package.sh, lupdate

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `~/.claude/skills/release/SKILL.md` | Create | Skill definition with all 5 phases |
| `~/.claude/skills/release/wiki-pages.md` | Create | Reference: Wiki pages and what to check per page |

---

## Chunk 1: Create Release Skill

### Task 1: Create SKILL.md

**Files:**
- Create: `~/.claude/skills/release/SKILL.md`
- Create: `~/.claude/skills/release/wiki-pages.md`

- [ ] **Step 1: Create skill directory**

```bash
mkdir -p ~/.claude/skills/release
```

- [ ] **Step 2: Write SKILL.md**

Frontmatter:
```yaml
---
name: release
description: Use when preparing a TTCut-ng release, bumping version, building packages, updating docs, or publishing to GitHub. Triggers on release requests, version bumps, or packaging tasks.
---
```

Body structure (keep under 500 lines, low freedom — exact commands):

```markdown
# TTCut-ng Release

Interactive release checklist. Every step requires confirmation before execution.
On failure: show error, ask retry/skip/abort. Never auto-skip.

## Phase 1 — Pre-Flight Checks

Present each check, show result, ask to proceed.

### 1.1 Branch & merge state
- Run: `git branch --show-current` → must be `master`
- If not: show current branch, list `git branch --no-merged master`, suggest merge commands
- ABORT if not on master

### 1.2 Working tree
- Run: `git status --short`
- Must be empty. If dirty: show files, ask proceed/abort

### 1.3 GitHub CLI
- Run: `gh auth status`
- Must be authenticated. ABORT if not.

### 1.4 Security issues
- Run: `grep -cE '(CRITICAL|HIGH)' TODO.md`
- If > 0: warn with count, ask proceed/abort

### 1.5 Translations
- Run: `lupdate ttcut-ng.pro 2>&1`
- Then: `grep -c 'type="unfinished"' trans/*.ts`
- If > 0: warn with count, filenames, and context (`grep -B1 'type="unfinished"' trans/*.ts`)

### 1.6 Commits since last release
- Run: `git describe --tags --abbrev=0` to get LAST_TAG
- If no tags exist (first release): use `git log --oneline` for all commits, note this is the first release
- Otherwise: `git log ${LAST_TAG}..HEAD --oneline`
- Display commit list for review

## Phase 2 — Version Bump

### 2.1 Suggest version
Analyze commits from Phase 1.6:
- Any starting with "Add" or containing "feature"/"support" → MINOR (0.X.Y → 0.X+1.0)
- Any "Fix" matching CRITICAL/HIGH security → MINOR
- "Fix"/"Update"/"Improve"/"Enhance" → PATCH (0.X.Y → 0.X.Y+1)
- Default → PATCH

Present: "Vorschlag: vOLD → vNEW. Einverstanden oder andere Version eingeben?"

### 2.2 Update ttcut-ng.pro
- Edit the `VERSION = X.Y.Z` line in `ttcut-ng.pro`

### 2.3 Scan all version references
- Run: `grep -rn "OLD_VERSION" --include="*.md" --include="*.pro" --include="*.h" .`
- Include CLAUDE.md, TODO.md, README.md explicitly
- Skip historical references (CHANGELOG.md entries, "v0.61.x" section headers in CLAUDE.md)
- Show each match, ask user which to update

## Phase 3 — Build & Verify

### 3.1 Full rebuild
```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```
ABORT on failure.

### 3.2 Smoke test
```bash
file ./ttcut-ng                    # expect "ELF 64-bit LSB"
ldd ./ttcut-ng | grep "not found"  # expect empty
```
ABORT on failure.

### 3.3 Debian package
```bash
echo "Release vX.Y.Z" | bash build-package.sh
```
Note the .deb output path from build output. ABORT on failure.

### 3.4 Cleanup
```bash
git checkout -- debian/changelog
```

## Phase 4 — Documentation

### 4.1 CHANGELOG.md
Generate entry from commits (Phase 1.6). Group:
- "Add ..." → Features
- "Fix ..." → Fixes
- Others → Changes

Format:
```markdown
## vX.Y.Z (YYYY-MM-DD)

### Features
- ...

### Fixes
- ...

### Changes
- ...
```
Show draft. User reviews/edits before saving. Prepend after header.

### 4.2 TODO.md
Check for items completed in this release. Ask user which to move to Completed.

### 4.3 README.md
Ask: "Gibt es Änderungen am README für dieses Release?"

### 4.4 CLAUDE.md
Ask: "Sollen neue Einträge in 'Recent Fixes and Features' in CLAUDE.md?"

### 4.5 Screenshots
Check commits for UI changes (grep for gui/, widget, button, dialog, icon).
If found:
- List affected UI areas
- List current screenshots in README.md and Wiki images/
- Remind: "Screenshots mit Tux-Testvideo erstellen (keine Filmausschnitte wegen Urheberrecht)"

### 4.6 Wiki update
Read Wiki pages from `/usr/local/src/TTCut-ng.wiki/`.
Compare commits since last tag against each page's content.
See [wiki-pages.md](wiki-pages.md) for page-by-page checklist.

Propose concrete changes per page. If non-trivial: suggest brainstorming.
Show `git status` of wiki repo for pending changes.
After user approves: edit pages.

## Phase 5 — Publish

### 5.0 Idempotency check
- Run: `git tag -l vX.Y.Z` — if tag exists, warn and ask proceed/abort
- Run: `gh release view vX.Y.Z 2>&1` — if release exists, warn and ask proceed/abort

### 5.1 Commit
Stage specific files (list explicitly what changed):
```bash
git add ttcut-ng.pro CHANGELOG.md  # + other modified files
```
Show `git diff --cached` for review. Commit:
```bash
git commit -m "Release vX.Y.Z"
```

### 5.2 Tag
```bash
git tag vX.Y.Z
```

### 5.3 Push — CONFIRM before executing
```bash
git push origin master --tags
```

### 5.4 GitHub Release — CONFIRM before executing
Extract CHANGELOG entry to temp file, then:
```bash
gh release create vX.Y.Z \
  --title "vX.Y.Z" \
  --notes-file /usr/local/src/CLAUDE_TMP/release-notes.md \
  /path/to/ttcut-ng_*.deb
```

### 5.5 Wiki — CONFIRM before executing
If wiki has changes:
```bash
cd /usr/local/src/TTCut-ng.wiki
git add -A
git commit -m "Update wiki for vX.Y.Z"
git push
```

## Rollback (if publish partially fails)
```bash
# Undo commit + tag:
git tag -d vX.Y.Z
git reset --soft HEAD~1
# Undo GitHub Release (if created):
gh release delete vX.Y.Z --yes
# Wiki push cannot be easily undone — note in output
```
```

- [ ] **Step 3: Write wiki-pages.md**

Reference file listing each Wiki page and what to check:

```markdown
# Wiki Pages Reference

Pages in `/usr/local/src/TTCut-ng.wiki/`:

| Page | Check for |
|------|-----------|
| Home.md | New major features in overview |
| Quickstart.md | Changed workflows or UI steps |
| Installation.md | New/changed dependencies |
| Keyboard-Shortcuts.md | New or changed shortcuts |
| Smart-Cut.md | Smart Cut algorithm changes |
| Supported-Formats.md | New codec/format support |
| ttcut-demux.md | Demux tool changes |
| VDR-Demux-Workflow.md | VDR workflow changes |
| Troubleshooting.md | New known issues or resolved issues |
| Changelog.md | Link to repo CHANGELOG.md or summary update |
| Info-File-Format.md | .info file format changes |
| _Sidebar.md | New pages to add to navigation |
```

- [ ] **Step 4: Verify skill is discovered**

Invoke `/release` in Claude Code. Verify the skill loads and Phase 1.1 executes (should abort since we're on feature/stream-points, not master).

- [ ] **Step 5: Commit plan**

```bash
git add docs/superpowers/plans/2026-03-18-release-skill.md
git commit -m "Add release skill implementation plan"
```
