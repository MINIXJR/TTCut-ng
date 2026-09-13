"""Add the 2026-09-13 run's verdicts (audit run 4, scope: the 26 source files
of docs/code-map/stream-points.md) and its Layer-3 rulings to
docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-13.py [review-dir]

review-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-2026-09-13-review and
must hold verdicts-matched.tsv (the two classifier reports, data and gui,
matched to the scan's candidates by fingerprint); rescan-dir (second
argument, default CLAUDE_TMP/TTCut-ng/code-audit-2026-09-13b) holds the
candidates.tsv of the rescan after the batches, whose twelve new in-scope
rows are ruled in RESCAN below; rescan2-dir (third argument, default
code-audit-2026-09-13c) the second rescan, ruled in RESCAN2. All three stay
out of the repository on purpose.

A consolidate verdict is marked done when the lines it points at were
rewritten by the run's batches A-E (old-side hunks of the diff from the map
commit ed6939fe to the batch-E commit); one whose lines the batches never
touched stays open."""
import csv, re, subprocess, sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

REVIEW = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-09-13-review")
RESCAN_DIR = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-09-13b")
RESCAN2_DIR = Path(sys.argv[3] if len(sys.argv) > 3 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-09-13c")
ROOT   = Path(__file__).resolve().parents[2]
OUT    = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-13"
MERGE_BASE, HEAD = "ed6939fe", "0400b9a0"   # map commit .. batch E

# Layer-3 rulings (rulings.md of the run): name prefix -> (verdict, reason).
RULINGS = [
    ("clone x2 (data/ttavdata.cpp:894-907)", "consolidate", "done 2026-09-13 batch C: applyPending() for the parked language/delay of a freshly opened audio or subtitle track (613a132b)"),
    ("clone x2 (data/ttavdata.cpp:587-590)", "deliberate", "the four defect-cluster passes share two lines (pos = qMax(0, first - offsetFrames); append Error point); desc and durSec differ per pass - run-3 rule '3-line prologue of two different paths'"),
    ("clone x2 (data/ttavdata.cpp:576-580)", "deliberate", "same cluster passes as ttavdata.cpp:587-590 (see there)"),
    ("clone x2 (data/ttavdata.cpp:579-582)", "deliberate", "same cluster passes as ttavdata.cpp:587-590 (see there)"),
    ("clone x2 (gui/ttaudiorepairdialog.cpp:169-172)", "consolidate", "done 2026-09-13 batch C: TTAVItem::findAudioRepairOverlapping for the marker context menu and the repair dialog (613a132b)"),
    ("function 'buildContextMenu' has cognitive complexity", "deliberate", "once the overlap lookup moved to TTAVItem::findAudioRepairOverlapping (batch C) the remaining branching is the menu logic itself"),
    ("noExplicitConstructor: Class 'TTCutFrameNavigation'", "consolidate", "done 2026-09-13 batch A: explicit added; the agent's 'module convention' does not hold (21 gui headers with explicit, 21 without), run 3 accepted the same finding as mechanical"),
    ("noExplicitConstructor: Class 'TTStreamPointWidget'", "consolidate", "done 2026-09-13 batch A: explicit added; same reasoning as TTCutFrameNavigation"),
    ("method 'onLogoThresholdChanged' can be made static", "consolidate", "done 2026-09-13 batch D: the slot is gone - the three threshold setters are lambdas passed to TTCutFrameNavigation::wireSearch (91dba022)"),
    ("clone x3 (gui/ttcutframenavigation.cpp:506-529)", "consolidate", "done 2026-09-13 batch D: wireSearch/startSearch/setSearchRunning for the black, scene and logo trio (91dba022)"),
    ("clone x2 (gui/ttcutmainwindow.cpp:2428-2442)", "consolidate", "done 2026-09-13 batch B: createAnalysisWrapper() for onLogoDataLoaded (via loadMarkadLogoProfile) and onLogoROISelected (f2198216)"),
    ("clone x2 (data/ttavdata.cpp:1568-1570)", "consolidate", "done 2026-09-13 batch C: TTCutList::isH26xCut() for onDoCut and TTCutPreviewTask (613a132b)"),
]

# The rescan after batches A-E (candidates.tsv of RESCAN_DIR) reported twelve
# candidates in scope as new: connect() clusters whose fingerprint (token
# sequence + site count) changed when batch B and D removed sites, the two
# applyPending() blocks batch C created, and one cppcheck finding batch B
# created. Name prefix -> (verdict, reason).
CONNECT_IDIOM = "connect() list of the navigation constructor, re-fingerprinted after batch D removed the search connects (was clone x17/x15 ttcutframenavigation.cpp:139-150, deliberate: one connect per button-slot pair, pointer-style wiring idiom)"
RESCAN = [
    ("clone x2 (common/ttthreadtaskpool.cpp:122-129)", "deliberate", "TTThreadTaskPool::wireTask and TTCutMainWindow::startAnalysisTask both connect finished/aborted of a task, to different receivers (pool bookkeeping vs analysis counter + deleteLater); wiring idiom"),
    ("clone x6 (data/ttavdata.cpp:1346-1352)", "deliberate", "runs of connect() statements in six unrelated setups; wiring idiom (run-4 precedent clone x5 ttcutmainwindow.cpp:317-319)"),
    ("clone x2 (data/ttavdata.cpp:849-868)", "deliberate", "onOpenAudioFinished/onOpenSubtitleFinished after batch C: two applyPending() calls each with its own map and TTAVItem setter - the consolidated form; a further merge would need a template over the audio/subtitle accessors"),
    ("clone x7 (gui/ttcutframenavigation.cpp:134-145)", "deliberate", CONNECT_IDIOM),
    ("clone x6 (gui/ttcutframenavigation.cpp:134-145)", "deliberate", CONNECT_IDIOM),
    ("clone x5 (gui/ttcutframenavigation.cpp:141-148)", "deliberate", CONNECT_IDIOM),
    ("clone x3 (gui/ttcutframenavigation.cpp:136-148)", "deliberate", CONNECT_IDIOM),
    ("clone x3 (gui/ttcutframenavigation.cpp:140-150)", "deliberate", CONNECT_IDIOM),
    ("clone x2 (gui/ttcutframenavigation.cpp:134-150)", "deliberate", CONNECT_IDIOM),
    ("clone x2 (gui/ttcutframenavigation.cpp:132-146)", "deliberate", CONNECT_IDIOM),
    ("clone x4 (gui/ttcurrentframe.cpp:749-751)", "deliberate", "was clone x5 (gui/ttcurrentframe.cpp:749-751) before batch B folded one site into startAnalysisTask; generic connect(sig1); connect(sig2); shape across unrelated tasks"),
    ("constVariablePointer: Variable 'vs' can be declared as pointer to const", "consolidate", "done 2026-09-13 after the rescan: onLogoDataLoaded only null-checked vs once batch B moved the load into loadMarkadLogoProfile - the local is gone"),
]

# The second rescan (RESCAN2_DIR) surfaced one previously deferred candidate
# in scope (the per-module cap frees a slot once a candidate is judged).
RESCAN2 = [
    ("clone x3 (gui/ttcutsettingsencoder.cpp:140-150)", "deliberate", "setTabData() of three settings pages: each reads its own TTSettings keys into its own widgets; only the shape 'const TTSettings* s = instance(); widget->set(s->key())' repeats (normalised member names) - page-load idiom, nothing to share"),
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
    if verdict == "consolidate" and not reason.startswith("done ") and rewritten(location, hunks):
        reason = f"done {D} run 4 batches A-E (lines rewritten, see git log {MERGE_BASE}..{HEAD}); was: " + reason
        stats["done-by-diff"] += 1
    if verdict == "unsure":
        raise SystemExit(f"unsure left without a ruling: {name}")
    store[fp] = vd.Verdict(fp, kind, verdict, D, reason[:220])
    stats[verdict] += 1

for r in rows:
    reason = re.sub(r"\s+", " ", r["evidence"].strip().strip('"'))
    reason = re.sub(r'^evidence:\s*', "", reason)
    put(r["fingerprint"], r["kind"], r["verdict"], reason, r["name"], r["location"])

rescan = list(csv.DictReader((RESCAN_DIR / "candidates.tsv").open(), delimiter="\t"))
for prefix, v, why in RESCAN:
    hits = [c for c in rescan if c["name"].startswith(prefix) and c["status"] == "new"
            and (not prefix.startswith("constVariablePointer") or c["location"].startswith("gui/ttcutmainwindow.cpp:2414"))]
    if len(hits) != 1:
        raise SystemExit(f"rescan ruling matches {len(hits)} rows: {prefix}")
    c = hits[0]
    store[c["fingerprint"]] = vd.Verdict(c["fingerprint"], c["kind"], v, D, why[:220])
    stats["rescan"] += 1

rescan2 = list(csv.DictReader((RESCAN2_DIR / "candidates.tsv").open(), delimiter="\t"))
for prefix, v, why in RESCAN2:
    hits = [c for c in rescan2 if c["name"].startswith(prefix) and c["status"] == "new"]
    if len(hits) != 1:
        raise SystemExit(f"rescan-2 ruling matches {len(hits)} rows: {prefix}")
    c = hits[0]
    store[c["fingerprint"]] = vd.Verdict(c["fingerprint"], c["kind"], v, D, why[:220])
    stats["rescan2"] += 1

vd.save(OUT, store)
done = sum(1 for r in rows if store[r["fingerprint"]].verdict == "consolidate" and store[r["fingerprint"]].reason.startswith("done "))
open_ = sum(1 for r in rows if store[r["fingerprint"]].verdict == "consolidate") - done
print(f"rows written: {len(rows)}  store size: {len(store)}  consolidate done: {done}  open: {open_}")
print(dict(stats))
