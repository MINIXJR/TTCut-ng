"""Add the audit-run-20 verdicts (scope: the source files of
docs/code-map/dev-tools.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-10-02-run20.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-2026-10-02-run20 (3 never
judged, none open in scope); rescan-dir to ...-run20c. The scanner does not
read Python. Judged in the main session; decided by the user on 2026-10-02:
nal-verify.py and the TS damage scripts removed, screenshot script stays on
xcb, the detector level question becomes a TODO entry."""
import csv, re, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-10-02-run20")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-10-02-run20c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-10-02"

RULINGS = {
 'ab3cf894': ('deliberate', 'make_test_video.sh: generate_mpeg2_576i_pal / generate_mpeg2_576i_pal_duplicate, two encode_variant calls that differ in file name, timeline and key-frame list; same idiom class as the generate_* wrappers judged on 2026-09-04'),
 'f785fec0': ('deliberate', 'make_test_video.sh: tail of one generate_* wrapper and head of the next (encode_variant call boundary), same idiom class as above'),
}

def tool_ruling(name):
    if name.startswith('noExplicitConstructor'):
        return ('consolidate', 'done 2026-10-02 audit run 20 batch C1: TTCutAboutDlg constructor explicit')
    return None

RUN_SCOPE = [l.split()[0] for l in """
build-package.sh
tools/ttcut-screenshots.sh
tools/test-videos/make_test_video.sh
tools/ttcut-burst-probe/main.cpp
gui/ttcutaboutdlg.cpp
gui/ttcutaboutdlg.h
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
