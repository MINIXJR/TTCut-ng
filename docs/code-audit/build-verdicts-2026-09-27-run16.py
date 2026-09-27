"""Add the audit-run-16 verdicts (scope: the source files of
docs/code-map/navigator-window.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-27-run16.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run16 (10 never judged,
none open in scope); rescan-dir to code-audit-run16c, the rescan after the
batches. Judged in the main session; drawers approved by the user on
2026-09-27: F1 overview bar repainted on append and remove (N1), N4 as TODO,
C mechanical (navigator display, window-geometry size check, centred option,
harness proxy copy), the rest deliberate."""
import csv, re, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run16")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run16c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-27"

C = 'done 2026-09-27 audit run 16 batch C (c81e99bc)'

RULINGS = {
 '522f96fc': ('consolidate', C + ': proxy-mode copy of the style removed from test_pulse_stylesheet'),
 'd92afb6e': ('consolidate', C + ': proxy-mode copy of the style removed from test_pulse_stylesheet'),
 'be9cdafb': ('consolidate', C + ': centredGroupBox() builds the option once for drawing and layout'),
 'f2467ce9': ('consolidate', C + ': readSize() and writeSize() in gui/ttwindowgeometry.cpp'),
 'ac178011': ('consolidate', C + ': TTNavigatorDisplay members m-prefixed'),
 'fd98d3d8': ('deliberate', 'TTWindowGeometry is a plain struct of public fields (rect, maximized, valid), no class members to prefix'),
}

def tool_ruling(name):
    m = re.match(r"(\w+):", name)
    check = m.group(1) if m else ''
    if check == 'functionStatic':
        return 'consolidate', C + ': empty TTStreamNavigator::setTitle and the .ui property calling it removed'
    if check in ('noExplicitConstructor', 'variableScope'):
        return 'consolidate', C
    return None

RUN_SCOPE = [l.split()[0] for l in """
gui/ttstreamnavigator.h
gui/ttstreamnavigator.cpp
gui/ttnavigatordisplay.h
gui/ttnavigatordisplay.cpp
gui/ttwindowgeometry.h
gui/ttwindowgeometry.cpp
gui/ttcentredtitlestyle.h
gui/ttcentredtitlestyle.cpp
gui/ttcutmainwindow.cpp
gui/ttcuttreeview.cpp
gui/ttquickjumpdialog.cpp
gui/ttcutmain.cpp
ui/streamnavigationwidget.ui
ui/navigatordisplaywidget.ui
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
    if r["kind"] == "tool":
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
 '60240d7f': ('deliberate', 'deliberate (rescan after run 16): header preamble of two widgets (ui include, TTAVItem forward declaration, Q_MOC_INCLUDE)'),
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
