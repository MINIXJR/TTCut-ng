"""Add the audit-run-14 verdicts (scope: the source files of
docs/code-map/subtitle-core.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-27-run14.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run14 (24 never judged,
none open in scope); rescan-dir to code-audit-run14c, the rescan after the
batches. Judged in the main session; drawers approved by the user on
2026-09-27: F1 cut fix (H1), F2 parser/lookup robustness (H2/H3/H4/H6),
C1 TTAVData::warnLater, C2 mechanical, the rest deliberate."""
import csv, re, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run14")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run14c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-27"

C2 = 'done 2026-09-27 audit run 14 batch C2 (mechanical: override, explicit, static/reinterpret_cast, const, scope, TTFileBuffer not copyable)'
F2 = 'done 2026-09-27 audit run 14 F2: TTSubtitleHeaderList::sort rewritten (stable, lambda, static_cast)'

RULINGS = {
 '8ce28178': ('consolidate', 'done 2026-09-27 audit run 14 batch C1: TTAVData::warnLater replaces the deferred warning copies (four sites)'),
}

def tool_ruling(name):
    m = re.match(r"(\w+):", name)
    check = m.group(1) if m else ''
    if check == 'dangerousTypeCast':
        return 'consolidate', F2
    if check == 'variableScope' and "'i'" in name:
        return 'deliberate', 'TTFileBuffer::nextStartCodeTS: legacy TS start-code search (FIXME inside), i is reset at the top of every pass'
    if check == 'knownConditionTrueFalse':
        return 'deliberate', 'TTFileBuffer::nextStartCodeTS: do { } while(-1) is the intended endless loop, left through return or the EOF exception'
    if check in ('variableScope', 'noCopyConstructor', 'noOperatorEq', 'passedByValue', 'cstyleCast',
                 'noExplicitConstructor', 'missingOverride', 'constVariablePointer'):
        return 'consolidate', C2
    return None

RUN_SCOPE = [l.split()[0] for l in """
avstream/ttsrtsubtitlestream.h
avstream/ttsrtsubtitlestream.cpp
avstream/ttsubtitleheaderlist.h
avstream/ttsubtitleheaderlist.cpp
avstream/ttavheader.h
avstream/ttavheader.cpp
avstream/ttfilebuffer.cpp
avstream/ttavtypes.cpp
data/ttopensubtitletask.cpp
data/ttsubtitlelist.cpp
data/ttavdata.cpp
mpeg2window/ttmpeg2window2.cpp
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
 '1b3f9d83': ('deliberate', 'deliberate (rescan after run 14): typed accessor per header-list kind (audioHeaderAt / subtitleHeaderAt), as 1958706d'),
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
