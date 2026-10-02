"""Add the audit-run-19 verdicts (scope: the source files of
docs/code-map/ttcut-ac3fix.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-10-02-run19.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-2026-10-02-run19 (4 never
judged, none open in scope); rescan-dir to ...-run19c, the rescan after the
batches. Judged in the main session; batches approved by the user on
2026-10-02: F1 repair decision in ttcut-demux (A3, A4), F2 output keeps every
byte (A5), F3 frame confirmation and messages (A6, A7), F4 shell error paths
(A1, A2), F5 arguments (A8), C1 dead fields and shellcheck."""
import csv, re, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-10-02-run19")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-10-02-run19c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-10-02"

C1 = 'done 2026-10-02 audit run 19 batch C1 (2464de4b)'

RULINGS = {
 '805a7a94': ('deliberate', 'mixed.ac3 recipe of gate_ac3fix.sh repeated in run-gates.sh make_mixed_ac3: both comments say it is kept identical so that the gates see the same acmod switches; the two scripts run standalone'),
 '8bccd245': ('deliberate', 'tail of the same mixed.ac3 recipe (gate_ac3fix.sh / run-gates.sh), see 805a7a94'),
 '655f3a4b': ('deliberate', 'two consecutive case lines of the gate table in gate_ac3fix.sh (run <name> <args>), one line per case by design'),
}

def tool_ruling(name):
    if name.startswith('Use find instead of ls'):
        return ('consolidate', C1 + ': gate_ac3fix.sh counts its captured files with find')
    return None

RUN_SCOPE = [l.split()[0] for l in """
tools/ttcut-ac3fix/ttcut-ac3fix.c
tools/diag/gate_ac3fix.sh
tools/diag/gate_ac3fix_contract.sh
tools/ttcut-demux/ttcut-demux
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
 '457c29f9': ('deliberate', 'extract_fn (awk, three lines) in four gate scripts that take functions out of ttcut-demux: each gate runs standalone, no shared gate library'),
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
