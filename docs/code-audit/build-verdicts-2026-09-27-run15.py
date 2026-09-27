"""Add the audit-run-15 verdicts (scope: the source files of
docs/code-map/logging.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-27-run15.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run15 (19 never judged,
none open in scope); rescan-dir to code-audit-run15c, the rescan after the
batches. Judged in the main session; drawers approved by the user on
2026-09-27: F1 log settings applied in one place before the first line (H2),
F2 libav errors always logged and the callback taken back from libmpv (H3),
F3 [fatal] tag and located exceptions as error (H4), F4 in-process gzip (H5),
F5 QT_MESSAGELOGCONTEXT (H6), F6 wrappers with their own cache (H1),
C mechanical, the rest deliberate."""
import csv, re, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run15")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run15c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-27"

C = ('done 2026-09-27 audit run 15 batch C (mechanical: exception subclasses inherit the '
     'TTException constructors, explicit, const QString& in TTMessageLogger, '
     'initialisation list, logFilePath() by reference)')

RULINGS = {
 'f440551c': ('consolidate', C),
 'b72386db': ('deliberate', 'TTMessageLogger: one public method per message type (QString form), each a one-line forward to logMsg; the API the whole code base calls'),
 'b1acb3b6': ('deliberate', 'TTMessageLogger printf forms: every variadic function needs its own va_start/va_end, the body is that plus the forward'),
 '075f49c0': ('deliberate', 'same shape only: TTCutMainWindow::navigationEnabled forwards to five child widgets, TTCutTreeView::controlEnabled enables its own buttons'),
 'fc0d1f4a': ('deliberate', 'same shape only: two constructors setting the icons of their own buttons and connecting them (TTCutOutFrame, TTCutSettingsPaths)'),
 '49dfb3ba': ('deliberate', 'same shape only: setTabData/saveTabData of two settings pages with different fields (logging, navigation)'),
}

def tool_ruling(name):
    m = re.match(r"(\w+):", name)
    check = m.group(1) if m else ''
    if check in ('noExplicitConstructor', 'passedByValue', 'returnByReference', 'useInitializationList'):
        return 'consolidate', C
    return None

RUN_SCOPE = [l.split()[0] for l in """
common/ttmessagelogger.h
common/ttmessagelogger.cpp
common/ttavlog.h
common/ttavlog.cpp
common/ttexception.h
common/ttexception.cpp
gui/ttcutmain.cpp
gui/ttcutmainwindow.cpp
gui/ttcutsettingslogging.cpp
gui/ttcutsettingspaths.cpp
common/ttsettings.cpp
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
 'e5a13509': ('deliberate', 'deliberate (rescan after run 15): per-type TTMessageLogger methods with const QString& parameters, as b72386db'),
 'fde7f586': ('deliberate', 'deliberate (rescan after run 15): printf forms of TTMessageLogger, each with its own va_start, as b1acb3b6'),
 'c5c6450a': ('deliberate', 'deliberate (rescan after run 15): one exception type per kind (callers catch them apart), body is a single inherited-constructor line'),
 'c4182a10': ('deliberate', 'deliberate (rescan after run 15): declarations of the per-type TTMessageLogger methods in the header'),
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
