"""Add the 2026-09-12 run's verdicts (audit run 3, the first "map before audit"
run: the 68 source files of the settings, stream-open, playback and
detection maps) and its Layer-3 rulings to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-12.py [review-dir] [scan-dir]

review-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-2026-09-12-review and
must hold verdicts-matched.tsv (the six classifier reports matched to the
scan's candidates by fingerprint); scan-dir to CLAUDE_TMP/TTCut-ng/
code-audit-2026-09-12 (candidates.tsv, for the two rows the reports named
by an older line number). Both stay out of the repository on purpose.

A consolidate verdict is marked done when the lines it points at were
rewritten by the run's batches A-H (old-side hunks of the diff from the
merge base 5924340b to the batch commits); a consolidate whose lines the
batches never touched stays open, as do the method-size findings the report
listed as "not a batch"."""
import csv, re, subprocess, sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

REVIEW = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-09-12-review")
SCAN   = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-09-12")
ROOT   = Path(__file__).resolve().parents[2]
OUT    = Path(__file__).resolve().parent / "verdicts.tsv"
D, D_DONE = "2026-09-12", "2026-09-13"
MERGE_BASE, HEAD = "5924340b", "8788b25f"   # batch H is the last batch commit

# Layer-3 rulings (rulings.md of the run): name prefix -> (verdict, reason).
RULINGS = [
    ("clone x2 (avstream/tth26xvideostream.cpp:106-110)", "consolidate", "fourth site of the .info frame-rate fallback; done 2026-09-13 batch B (TTESInfo::timingForVideo, 9191a46a)"),
    ("function 'scanPacketsIntoRawIndex' has cognitive complexity", "consolidate", "low: one ~70-line per-packet handler, gate exists (test_frameindex_dump); not a batch of run 3"),
    ("useStlAlgorithm: Consider using std::accumulate", "deliberate", "both loops are max-reductions; std::accumulate is the wrong algorithm, std::max_element on a derived quantity reads no better (run-2 precedent)"),
    ("clone x2 (gui/ttcurrentframe.cpp:522-535)", "consolidate", "done 2026-09-13 batch E: ttFramePositionText() for TTCurrentFrame and TTCutOutFrame (3504eb3e)"),
    ("clone x2 (gui/ttcurrentframe.cpp:1108-1113)", "consolidate", "done 2026-09-13 batch B: further .info site (rate + avOffset), TTESInfo::timingForVideo (9191a46a)"),
    ("clone x2 (gui/ttcutpreview.cpp:740-750)", "deliberate", "3-line prologue of two different regenerate paths"),
    ("clone x2 (gui/ttcurrentframe.h:30-41)", "deliberate", "twin widgets, header declarations; a common base class is a design decision (report block 2, not taken)"),
    ("clone x2 (gui/ttcutpreview.cpp:481-488)", "consolidate", "numPreview = count/2+1 guard, falls into previewSegmentIndices (data-A #60); open"),
    ("clone x2 (gui/ttcutpreview.cpp:328-332)", "consolidate", "play-button state text+icon, small showPlayState(bool) helper; open"),
    ("returnByReference", "deliberate", "QString/value bundle getters return by value like the project majority; implicitly shared (run-2 precedent lastError())"),
    ("useStlAlgorithm: Consider using std::count_if", "deliberate", "the loop reads no worse (precedent)"),
    ("method 'onPlayerError' can be made static", "deliberate", "Qt slot stays non-static next to its siblings (run-2 precedent)"),
    ("functionStatic: Technically the member function 'TTCutSettingsEncoder::setTitle'", "deliberate", "ui_avcutdialog.h (uic-generated) calls setTitle - a source grep does not see generated code; found by the compiler in batch A"),
]
# Two report rows the classifier named by an older line number (rulings.md, data-A).
BY_HAND = {
    "clone x3 (data/ttcutpreviewtask.cpp:561-566)": ("deliberate", "agent named it clone x3 (data/ttcutpreviewtask.cpp:232-236); mapped by hand (rulings.md data-A)"),
    "clone x2 (data/ttcutprojectdata.cpp:584-587)": ("deliberate", "agent named it clone x2 (data/ttcutprojectdata.cpp:515-522); mapped by hand (rulings.md data-A)"),
}

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
cands = {c["name"]: c for c in csv.DictReader((SCAN / "candidates.tsv").open(), delimiter="\t")}

stats = Counter()
def put(fp, kind, verdict, reason, name, location):
    for prefix, v, why in RULINGS:
        if name.startswith(prefix):
            verdict, reason = v, why
            stats["ruled"] += 1
            break
    if verdict == "consolidate" and not reason.startswith("done ") and rewritten(location, hunks):
        reason = f"done {D_DONE} run 3 batches A-H (lines rewritten, see git log {MERGE_BASE}..{HEAD}); was: " + reason
        stats["done"] += 1
    store[fp] = vd.Verdict(fp, kind, verdict, D, reason[:220])
    stats[verdict] += 1

for r in rows:
    reason = re.sub(r"\s+", " ", r["evidence"].strip().strip('"'))
    reason = re.sub(r'^evidence:\s*', "", reason)
    put(r["fingerprint"], r["kind"], r["verdict"], reason, r["name"], r["location"])

for name, (v, why) in BY_HAND.items():
    c = cands[name]
    put(c["fingerprint"], c["kind"], v, why, name, c["location"])

vd.save(OUT, store)
print(f"rows written: {len(rows) + len(BY_HAND)}  store size: {len(store)}")
print(dict(stats))
