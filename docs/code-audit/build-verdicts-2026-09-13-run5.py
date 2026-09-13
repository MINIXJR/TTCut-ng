"""Add the 2026-09-13 run-5 verdicts (audit run 5, scope: the 12 source files
of docs/code-map/project-lifecycle.md) and its rulings to
docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-13-run5.py [review-dir] [rescan-dir]

review-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run5-review and must
hold verdicts-matched.tsv (the three classifier reports - common, data, gui -
matched to the scan's candidates by fingerprint); rescan-dir defaults to
CLAUDE_TMP/TTCut-ng/code-audit-run5b, the rescan after the batches, whose
sixteen new in-scope rows are ruled in RESCAN below. Both stay out of the
repository on purpose.

A consolidate verdict is marked done when the lines it points at were
rewritten by batches A-D (old-side hunks of the diff from the branch point
d25c7056 to the batch-D commit)."""
import csv, re, subprocess, sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

REVIEW = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run5-review")
RESCAN_DIR = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run5b")
ROOT = Path(__file__).resolve().parents[2]
OUT  = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-13"
MERGE_BASE, HEAD = "d25c7056", "5e47997b"   # branch point .. batch D

# Layer-3 rulings of run 5 (report block 1/2), name prefix -> (verdict, reason).
RULINGS = [
    ("unusedFunction: The function 'audioOnlyFormat' is never used.", "consolidate",
     "done 2026-09-13 batch A (fc4dd533): no caller - every reader uses workingAudioOnlyFormat(); field, load() and save() stay"),
    ("clone x2 (gui/ttcutmainwindow.cpp:1027-1035)", "consolidate",
     "done 2026-09-13 batch B (ea7f763a): startDetectorTask<Task>() connects pointsDetected and calls startAnalysisTask, for all four detectors"),
]

# The rescan after the batches (RESCAN_DIR) reported sixteen in-scope rows as
# new: the TTSettings and list-class idiom classes whose fingerprint (token
# sequence + site count) moved when batch A removed a getter, plus three
# genuinely new shapes. Name prefix -> (verdict, reason).
SETTINGS_IDIOM = ("deliberate", "TTSettings getter/setter/early-out idiom, re-fingerprinted after batch A removed audioOnlyFormat(); settings-state.md documents the 76 plain App-Default fields, run-5 classifier ruled the class deliberate")
LIST_FAMILY = ("documented", "TTAudioList/TTCutList/TTMarkerList/TTSubtitleList family, re-fingerprinted after the run-5 batches; run 2 ruled TTItemListStorage<T> out because the four differ exactly in the signal semantics a template would hide (docs/completed-work.md)")
RESCAN = [
    ("clone x12 (common/ttsettings.cpp:96-132)", *SETTINGS_IDIOM),
    ("clone x9 (common/ttsettings.h:45-73)", *SETTINGS_IDIOM),
    ("clone x3 (common/ttsettings.cpp:642-653)", *SETTINGS_IDIOM),
    ("clone x7 (common/ttsettings.cpp:628-636)", *SETTINGS_IDIOM),
    ("clone x2 (common/ttsettings.cpp:269-320)", *SETTINGS_IDIOM),
    ("clone x2 (common/ttsettings.h:45-71)", *SETTINGS_IDIOM),
    ("clone x2 (common/ttsettings.cpp:699-712)", *SETTINGS_IDIOM),
    ("clone x2 (common/ttsettings.h:81-90)", *SETTINGS_IDIOM),
    ("clone x2 (common/ttsettings.h:273-282)", *SETTINGS_IDIOM),
    ("clone x2 (common/ttsettings.cpp:627-639)", *SETTINGS_IDIOM),
    ("clone x2 (common/ttsettings.h:342-345)", *SETTINGS_IDIOM),
    ("clone x4 (data/ttaudiolist.h:95-98)", *LIST_FAMILY),
    ("clone x3 (data/ttaudiolist.h:80-85)", *LIST_FAMILY),
    ("clone x2 (gui/ttcutmainwindow.cpp:2168-2187)", "deliberate",
     "onSearchBlackFrame and onSearchSceneChange construct two different task classes from the same source bundle; what they share is already directedSearchSource() + launchDirectedSearch() (run 3, 026aa9fb)"),
    # The two parse-guard classes are the same finding at two window offsets.
    ("clone x3 (data/ttcutprojectdata.cpp:217-223)", "consolidate",
     "OPEN: parseVideoSection/parseAudioSection/parseSubtitleSection repeat the same two guards (node count, then resolveProjectPath on <Name> with a warning); batch C only changed the video one's return type. Target: a helper returning order+resolved name, ~10 lines x 3 sites"),
    ("clone x3 (data/ttcutprojectdata.cpp:215-218)", "consolidate",
     "OPEN: same three parse guards as the class above, one window earlier"),
]

def old_side_hunks():
    """file -> list of (start, end) old-side line ranges the batches rewrote."""
    out = defaultdict(list)
    diff = subprocess.run(["git", "diff", "-U0", f"{MERGE_BASE}..{HEAD}", "--", ".", ":(exclude)docs"],
                          cwd=ROOT, capture_output=True, text=True, check=True).stdout
    cur = None
    for line in diff.splitlines():
        if line.startswith("--- "):
            cur = line[6:] if line.startswith("--- a/") else None
        elif line.startswith("@@") and cur:
            m = re.match(r"@@ -(\d+)(?:,(\d+))? ", line)
            start, count = int(m.group(1)), int(m.group(2) or 1)
            out[cur].append((start, start + max(count, 1) - 1))
    return out

def rewritten(location, hunks):
    for site in location.split(";"):
        m = re.match(r"([^:]+):(\d+)-(\d+)$", site.strip())
        if not m:
            continue
        f, a, b = m.group(1), int(m.group(2)), int(m.group(3))
        if any(not (hb < a or ha > b) for ha, hb in hunks.get(f, [])):
            return True
    return False

hunks = old_side_hunks()
store = vd.load(OUT)
rows = list(csv.DictReader((REVIEW / "verdicts-matched.tsv").open(), delimiter="\t"))

stats = Counter()
def put(fp, kind, verdict, reason, name, location):
    for prefix, v, why in RULINGS:
        if name.startswith(prefix):
            verdict, reason = v, why
            stats["ruled"] += 1
            break
    if verdict == "consolidate" and not reason.startswith(("done ", "OPEN")) and rewritten(location, hunks):
        reason = f"done {D} run 5 batches A-D (lines rewritten, see git log {MERGE_BASE}..{HEAD}); was: " + reason
        stats["done-by-diff"] += 1
    if verdict == "unsure":
        raise SystemExit(f"unsure left without a ruling: {name}")
    store[fp] = vd.Verdict(fp, kind, verdict, D, reason[:220])
    stats[verdict] += 1

for r in rows:
    reason = re.sub(r"\s+", " ", r["evidence"].strip().strip('"'))
    reason = re.sub(r"^evidence:\s*", "", reason)
    put(r["fingerprint"], r["kind"], r["verdict"], reason, r["name"], r["location"])

rescan = list(csv.DictReader((RESCAN_DIR / "candidates.tsv").open(), delimiter="\t"))
for prefix, v, why in RESCAN:
    hits = [c for c in rescan if c["name"].startswith(prefix) and c["status"] == "new"]
    if len(hits) != 1:
        raise SystemExit(f"rescan ruling matches {len(hits)} rows: {prefix}")
    c = hits[0]
    store[c["fingerprint"]] = vd.Verdict(c["fingerprint"], c["kind"], v, D, why[:220])
    stats["rescan"] += 1

vd.save(OUT, store)
print(f"rows written: {len(rows)} + {len(RESCAN)} rescan   store size: {len(store)}")
print(dict(stats))
