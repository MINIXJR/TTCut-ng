"""Add the audit-run-17 verdicts (scope: the source files of
docs/code-map/demux-helpers.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-27-run17.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run17 (6 never judged,
none open in scope); rescan-dir to code-audit-run17c, the rescan after the
batches. The scanner covers the Bash sources only (ttcut-ocr-glyphs is
Python). Judged in the main session; drawers approved by the user on
2026-09-27: F1 unique output names (D2), F2 glyph band/line pairing (D5),
F3 Pillow warning and selftest gate (D3, D4), F4 program lookup in the
example (D1), C1 subtitle extraction and OCR call once each, the rest
deliberate."""
import csv, re, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run17")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run17c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-27"

C1 = 'done 2026-09-27 audit run 17 batch C1 (d1ecf5ba)'

RULINGS = {
 'a2da5a05': ('consolidate', C1 + ': _extract_subs_ts holds the four time flags for the stream copy and the re-encode fallback'),
 '053ec3e5': ('consolidate', C1 + ': _subs_delay_ms + _run_ocr for the direct and the re-encode OCR'),
 '6cfe4dd6': ('consolidate', C1 + ': _subs_delay_ms + _run_ocr for the direct and the re-encode OCR'),
 '02367fa4': ('deliberate', 'ffprobe packet/frame count idiom: gate_mkv_framerate.sh stays self-contained, ttcut-demux counts its own output'),
 'e7a1ed64': ('deliberate', 'Bash idiom for optional arguments (local -a x=(); [ -n ... ] && x=(...)), three different options'),
 '12da9d0b': ('deliberate', 'Bash idiom for optional arguments (local -a x=(); [ -n ... ] && x=(...)), two different options'),
}

def tool_ruling(name):
    return None

RUN_SCOPE = [l.split()[0] for l in """
tools/ttcut-demux/ttcut-ocr-glyphs
tools/ttcut-demux/ocr-glyphs/note_thin__♪.txt
tools/vdr-demux-example.sh
tools/ttcut-demux/ttcut-demux
debian/rules
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
