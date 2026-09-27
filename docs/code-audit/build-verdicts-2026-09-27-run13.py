"""Add the audit-run-13 verdicts (scope: the source files of
docs/code-map/mpeg2-decoder.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-27-run13.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run13 (12 never judged,
none open in scope); rescan-dir to code-audit-run13b, the rescan after the
batches. Judged in the main session; drawers approved by the user on
2026-09-27: F1 decoder fixes (H1/H2/H4/H5), C1 transcode tidy, C2
mechanical, the rest deliberate; F2 (sequential re-encode, H6) left as a
TODO by user decision."""
import csv, re, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run13")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run13b")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-27"

C1 = 'done 2026-09-27 audit run 13 batch C1: TTTranscodeProvider uses ttAvErrorToString and one drainPackets lambda'
C2 = 'done 2026-09-27 audit run 13 batch C2 (mechanical: static_cast, const)'

RULINGS = {
 '65e0df15': ('deliberate', 'TTVideoIndexList::moveToNextIndexPos / moveToPrevIndexPos are mirror images (step +1 / -1, bound count / 0)'),
 '7556cbbd': ('deliberate', 'the centre-band scans of TTSearchTask (black-frame test with early exit vs histogram) accumulate different things'),
 '4665c471': ('consolidate', C1),
 'a0e4cc69': ('consolidate', C1),
 'fddb2eaa': ('consolidate', C1 + ' (the in-loop and flush receive-and-write loops)'),
}

def tool_ruling(name):
    m = re.match(r"(\w+):", name)
    check = m.group(1) if m else ''
    if check in ('cstyleCast', 'constParameterCallback', 'constVariablePointer'):
        return 'consolidate', C2
    if "encodeFrames" in name:
        return 'deliberate', ('cognitive complexity 36 -> 29 after C1; the rest is the per-frame decode loop, '
                              'left as it is with the H6 TODO (sequential decode differs on field pictures)')
    if "'operation'" in name:
        return 'deliberate', 'TTQuickJumpWorker::operation serves both decoder kinds per thumbnail; audit run 13 fixed H4 only'
    return None

RUN_SCOPE = [l.split()[0] for l in """
mpeg2decoder/ttmpeg2decoder.h
mpeg2decoder/ttmpeg2decoder.cpp
avstream/ttframeinfo.h
avstream/ttvideoindexlist.cpp
avstream/ttvideoheaderlist.cpp
mpeg2window/ttmpeg2window2.cpp
data/ttsearchtask.cpp
data/ttframesearchtask.cpp
gui/ttquickjumpworker.cpp
extern/tttranscode.cpp
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
 '86ddb8ed': ('deliberate', 'deliberate (rescan after run 13): sort comparator per header-list kind (audio by abs_frame_time, video by header offset), as 8c23d2f0'),
 'cb3a6788': ('deliberate', 'deliberate (rescan after run 13): the TTMpeg2Decoder constructor assigns every member in its body (TTCut style); mFrameInfo follows it'),
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
