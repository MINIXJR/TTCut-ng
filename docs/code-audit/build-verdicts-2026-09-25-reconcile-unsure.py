"""Close the eight stale `unsure` rows of docs/code-audit/verdicts.tsv
(2026-09-25, after the reconciliation of the 168 consolidate rows).

    python3 docs/code-audit/build-verdicts-2026-09-25-reconcile-unsure.py

All eight come from audit run 1 (2026-09-03); none of their fingerprints
occurs in the code any more. Checked in the main session against today's
code: three findings were fixed, two are covered by a documented row or
project, three were misreads of the scanner's class attribution that the
scanner no longer produces."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-25"
A = "reconciliation of the unsure rows 2026-09-25"
RULINGS = {
 '0909d827': ('consolidate', f'done ({A}): TTAC3AudioHeader and TTMpegAudioHeader declare const QString& bitRateString() override, matching the base (bb348d51)'),
 '202cb376': ('consolidate', f'done ({A}): the unused struct TStreamInfo was removed from mpeg2decoder/ttmpeg2decoder.h in ab3fae4d'),
 '298aeee0': ('consolidate', f'done ({A}): same TStreamInfo removal (ab3fae4d)'),
 'fc6a4ba2': ('documented', f'documented ({A}): TTCurrentFrame class size - decided in the current class-size row 97e7e0e0 (playback/temp-MKV group)'),
 '3ef75e75': ('documented', f'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P9 - the range lay in the burst detector, today TTAudioCutter::detectBurst ({A})'),
 '081a8b70': ('deliberate', f'{A}: scanner misread - class size attributed to QFile from static QFile:: calls; no longer reported'),
 '4402ea29': ('deliberate', f'{A}: scanner misread - class size attributed to TTMessageLogger from getInstance() calls; no longer reported'),
 'b5842807': ('deliberate', f'{A}: scanner misread - TTCentredTitleStyle has three out-of-line methods; no longer reported'),
}

store = vd.load(OUT)
for key, (verdict, reason) in RULINGS.items():
    fps = [fp for fp in store if fp.startswith(key)]
    if len(fps) != 1:
        raise SystemExit(f"{key} matches {len(fps)} rows")
    v = store[fps[0]]
    if v.verdict != "unsure" and A not in v.reason:
        raise SystemExit(f"{key} is {v.verdict}, not an unsure row")
    store[fps[0]] = vd.Verdict(fps[0], v.kind, verdict, D, reason[:400])
vd.save(OUT, store)
print(len(RULINGS), "rows closed")
