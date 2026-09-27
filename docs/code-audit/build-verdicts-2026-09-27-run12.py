"""Add the audit-run-12 verdicts (scope: the source files of
docs/code-map/h26x-stream.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-27-run12.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run12 (95 never judged,
none open in scope); rescan-dir to code-audit-run12b, the rescan after
batches C1-C2. Judged in the main session; drawers approved by the user on
2026-09-27: F1 findIDRBefore on true IDRs, C1 typed header classes removed,
C2 mechanical, the rest deliberate."""
import csv, re, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run12")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run12b")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-27"

C1 = ('done 2026-09-27 audit run 12 batch C1: typed SPS/VPS/access-unit classes (tth264videoheader, '
      'tth265videoheader) removed, TTH26xVideoStream answers from the index bundle')
C2 = 'done 2026-09-27 audit run 12 batch C2 (mechanical: override, const, const-reference return, renamed argument, one H.26x cast)'
INDENT = ('deliberate: four-space H.26x block, docs/conventions.md cpp/indent exception list '
          '(ttavutil and ttdisplayordermap added in run 12)')

RULINGS = {
 '0a1cbbc0': ('deliberate', 'TTDisplayOrderMap::buildFromFile pairs the drop flags and the key flags with the entries; two vectors, two different warnings'),
 'dae01690': ('consolidate', C1 + ' (the TTH265SPS getter site)'),
 '08b568a2': ('consolidate', C1 + ' (profileString/levelString per codec)'),
 'e8b51e0e': ('consolidate', C1),
 '419b0dde': ('consolidate', C1),
 '1788acf5': ('consolidate', C1),
 '72410c50': ('consolidate', C1 + ' (the H.264 NAL type enum copy; ttnaluparser.h keeps its own)'),
 'fc760b84': ('consolidate', C1 + ' (buildAccessUnits frame-type branches)'),
 '5e3401e8': ('consolidate', C1 + ' (accessUnitToCodingType per codec -> TTFrameInfo::frameType)'),
 'fd72ffb9': ('consolidate', C1 + ' (buildSPSFromStreamInfo per codec)'),
 'aa2d760e': ('consolidate', C1 + ' (setSPSFrameRate/spsDescription per codec)'),
 '3eacf138': ('consolidate', C1 + ' (the subclass declarations shrank to the codec identity)'),
 '001947c2': ('consolidate', C1),
 '8bb4a5fb': ('consolidate', C1),
 'fd8e000a': ('consolidate', C1 + ' (the two TTH265SPS getter sites; the TTESInfo one is a plain getter)'),
 'c0109eb1': ('consolidate', 'done 2026-09-27 audit run 12 batch C2: TTQuickJumpDialog::highlightCurrentKeyframe'),
 '4e714629': ('deliberate', 'isCutInPoint and isCutOutPoint share the encoder-mode and bounds head; the tests differ (RAP at pos vs last frame or RAP at pos+1)'),
 'e1a385c8': ('deliberate', 'key switch of TTCutFrameNavigation::keyPressEvent: one case per key, each emitting its own signal'),
 'd6bc483c': ('deliberate', 'avformat_open_input failure idiom; the two other sites are self-contained harnesses'),
 'b3950fee': ('deliberate', 'cut-point tests of H.26x (random access from the bundle) and MPEG-2 (I/P picture types); same encoder-mode head, different streams'),
}

def tool_ruling(name):
    m = re.match(r"(\w+):", name)
    check = m.group(1) if m else ''
    if check in ('unusedFunction',):
        return 'consolidate', C1
    if check == 'missingOverride':
        return 'consolidate', (C2 if '~TTH26xVideoStream' in name else C1)
    if check == 'shadowFunction':
        arg = re.search(r"'([^']+)'", name).group(1)
        return 'consolidate', (C2 if arg in ('decodeToDisplay', 'subtitleStream') else C1)
    if check == 'constVariablePointer':
        var = re.search(r"'([^']+)'", name).group(1)
        return 'consolidate', (C1 if var == 'au' else C2)
    if check in ('constParameterPointer', 'returnByReference'):
        return 'consolidate', C2
    if check == 'useStlAlgorithm':
        return 'deliberate', 'cppcheck style: the raw loops in TTDisplayOrderMap count/find with side conditions and read as they are'
    return None

RUN_SCOPE = [l.split()[0] for l in """
avstream/tth26xvideostream.h
avstream/tth26xvideostream.cpp
avstream/tth264videostream.h
avstream/tth264videostream.cpp
avstream/tth265videostream.h
avstream/tth265videostream.cpp
avstream/tth264videoheader.h
avstream/tth264videoheader.cpp
avstream/tth265videoheader.h
avstream/tth265videoheader.cpp
avstream/ttframeindex.h
avstream/ttavutil.cpp
avstream/ttavstream.cpp
avstream/ttdisplayordermap.cpp
data/ttopenvideotask.cpp
data/ttpreviewclip.cpp
data/ttavdata.cpp
gui/ttcutframenavigation.cpp
gui/ttvideotreeview.cpp
gui/ttquickjumpdialog.cpp
mpeg2window/ttmpeg2window2.cpp
data/ttframesearchtask.cpp
""".strip().splitlines()]
def in_scope(row, files=RUN_SCOPE):
    return any(loc.split(":")[0] in files for loc in (row.get("location") or "").split(";"))

store = vd.load(OUT)
stats = Counter()
used = set()
for r in csv.DictReader((SCAN / "candidates.tsv").open(), delimiter="\t"):
    if r["status"] != "new" or not in_scope(r):
        continue
    fp = r["fingerprint"]
    if r["kind"] == "convention":
        ruling = ('deliberate', INDENT) if r["name"] == "cpp/indent=4" else None
    elif r["kind"] == "tool":
        ruling = tool_ruling(r["name"])
    else:
        ruling = RULINGS.get(fp[:8])
        used.add(fp[:8])
    if ruling is None:
        raise SystemExit(f"in-scope candidate without a ruling: {r['kind']} {r['name']}")
    verdict, reason = ruling
    store[fp] = vd.Verdict(fp, r["kind"], verdict, D, reason[:400])
    stats["new " + verdict] += 1
if set(RULINGS) - used:
    raise SystemExit(f"rulings match no candidate: {set(RULINGS) - used}")

RESCAN_RULINGS = {
 '393b722b': ('deliberate', 'deliberate (rescan after run 12): the two codec subclasses after C1 - identity only (stream type, label, expected codec)'),
 '9538ebe5': ('consolidate', 'done 2026-09-27 audit run 12 (rescan): the local probe info in createHeaderList no longer shadows streamInfo()'),
}
rescan_used = set()
base_fps = {r["fingerprint"] for r in csv.DictReader((SCAN / "candidates.tsv").open(), delimiter="\t")}
if RESCAN.exists():
    for r in csv.DictReader((RESCAN / "candidates.tsv").open(), delimiter="\t"):
        if r["status"] != "new" or not in_scope(r) or r["fingerprint"] in base_fps:
            continue
        key = r["fingerprint"][:8]
        if key not in RESCAN_RULINGS:
            raise SystemExit(f"rescan candidate without a ruling: {r['name']}")
        verdict, reason = RESCAN_RULINGS[key]
        store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
        rescan_used.add(key)
        stats["rescan " + verdict] += 1
    if set(RESCAN_RULINGS) - rescan_used:
        raise SystemExit(f"rescan rulings match no candidate: {set(RESCAN_RULINGS) - rescan_used}")

vd.save(OUT, store)
print(dict(stats))
