"""Add the 2026-09-25 run-9 verdicts (audit run 9, scope: the 14 source files
of docs/code-map/cut-preview.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-25-run9.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run9 (candidates.tsv);
rescan-dir to code-audit-run9c, the rescan after the batches (run9b was the one before the last const fix). Small scope
(9 new, 4 open): judged in the main session, no subagents. Open items first,
per the rule "offen darf nicht offen bleiben" (docs/quality-roadmap.md);
drawers approved by the user on 2026-09-25 (the TTCutTreeView member names
were rebuilt on the user's choice instead of the proposed deliberate)."""
import csv, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run9")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run9c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-25"

RULINGS = {
 'b18a239bd51af8c3f0897e6611c83c68498e3328': ('deliberate',
  'the unprefixed names in data/ttpreviewclip.h are fields of the plain value struct TTPreviewSource; classes carry m/mp - same practice as TTMuxTaskParams, SearchControls, HintCell'),
 '9357756bbc0479a82e78aaa5f22752400b7e2f3c': ('deliberate',
  'TTCutPreviewTask::errorMessage() returns by value like the other task getters; TTAVData::onCutPreviewAborted copies it right before deleting the task'),
 'ad71b445709bca42b5e62d86b6213b8a730592e9': ('deliberate',
  'TTCutTreeView::onEntryUp/onEntryDown are mirror images whose loop direction differs on purpose (moving several selected rows needs ascending order upward, descending downward)'),
 '58fad24caeff9f13d7fad7ea3515390966b1f1c8': ('deliberate',
  'same onEntryUp/onEntryDown mirror pair, the guard at the list edge'),
 '76e57846e75e7a371ecdd637cdbc0e1c0f570f16': ('deliberate',
  'aspectHint/acmodHint pick "start+end/start/end" texts that are separate tr() strings per hint kind; a shared helper would only pass three strings through'),
 'b37197935dd2be1d5d4ed00cb0f7560b30f52cfd': ('deliberate',
  'same aspectHint/acmodHint text choice'),
 'caa594deb5436051087f470f05357b8f2f820233': ('deliberate',
  'include and forward-declaration block of two unrelated headers (ttcutframenavigation.h, ttcuttreeview.h)'),
 '33a80c73b816db735e8040b75e04f53ff282c263': ('deliberate',
  'aspectHint is one of three const hint producers (burstHint, aspectHint, acmodHint) called per row by updateHintColumn; making one of them static splits the family for no gain'),
 'cc8ac5459a2e7be4dce995c5520380bc6a915c32': ('consolidate',
  'done 2026-09-25 audit run 9 batch C6: TTCutPreview constructor is explicit'),
}

# the open in-scope items of earlier runs: fingerprint -> (kind, verdict, reason)
OPEN_RULINGS = {
 'a20d1bcbfcbda9ad35f82abce45a41e73b33f76c': ('clone', 'documented',
  'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P3 (Vorschau-Pipeline) - the audio-cut skeleton of the two rebuild functions belongs to the clip-production consolidation (open-item ruling 2026-09-25, audit run 9)'),
 '44f7a9c99c8dc35500e4b11afee5e6d4ce761a16': ('clone', 'consolidate',
  'done 2026-09-25 audit run 9 batch C1: ttPreviewClipCount/ttPreviewCutOutEntry in data/ttpreviewclip.h used by the task, ttBuildClipCutList and the dialog'),
 'b3d7df74e54cdeae8d830371082774ca0e2b03c0': ('clone', 'consolidate',
  'done 2026-09-25 audit run 9 batch C1: the dialog counts clips through ttPreviewClipCount'),
 '78f9b3f1bac5cb585f72a27b1109f602c0f9f89f': ('convention', 'consolidate',
  'done 2026-09-25 audit run 9: TTCutTreeView members renamed to m/mp (conventions.md: renamed when the class is reworked; the unused currentEditItem removed) - user decision'),
}

RUN_SCOPE = ['gui/ttcuttreeview.h', 'gui/ttcuttreeview.cpp', 'gui/ttcutmainwindow.h', 'gui/ttcutmainwindow.cpp', 'data/ttavdata.h', 'data/ttavdata.cpp', 'data/ttcutpreviewtask.h', 'data/ttcutpreviewtask.cpp', 'data/ttpreviewclip.h', 'data/ttpreviewclip.cpp', 'gui/ttcutpreview.h', 'gui/ttcutpreview.cpp', 'common/ttsettings.h', 'ui/ttcutsettingssearch.ui']
def in_scope(row, files=RUN_SCOPE):
    return any(loc.split(":")[0] in files for loc in (row.get("location") or "").split(";"))

store = vd.load(OUT)
stats = Counter()
rows = [r for r in csv.DictReader((SCAN / "candidates.tsv").open(), delimiter="\t")
        if r["status"] == "new" and in_scope(r)]
seen = set()
for r in rows:
    if r["fingerprint"] not in RULINGS:
        raise SystemExit(f"in-scope candidate without a ruling: {r['name']}")
    verdict, reason = RULINGS[r["fingerprint"]]
    store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
    seen.add(r["fingerprint"])
    stats["new " + verdict] += 1
if set(RULINGS) - seen:
    raise SystemExit("rulings match no candidate")
for fp, (kind, verdict, reason) in OPEN_RULINGS.items():
    if fp not in store:
        raise SystemExit(f"open ruling for a fingerprint not in the store: {fp}")
    store[fp] = vd.Verdict(fp, kind, verdict, D, reason[:400])
    stats["open -> " + verdict] += 1

RESCAN_RULINGS = {
 '246f862ef7f87d5346df0a8e87a3c20d8f384a18': ('deliberate',
  'deliberate (rescan after run 9): generic connect-block shape across TTAVData, the cut dialog and the preview dialog; unrelated signals, no shared logic'),
 '981d3b7dce1f80ad391db56377deb1ccaaffa923': ('deliberate',
  'deliberate (rescan after run 9): each cut path wires the pool exit to its own finish slot and aborted to onCutAborted; the pair is the documented pool contract (progress-reporting.md), not a duplicate'),
 'abaebffd7c3f8fbeb2e4d4215bd04dcf62906679': ('deliberate',
  'deliberate (rescan after run 9): loop head "take item i, take its stream" in createPreviewCutList and serializeAVDataItem - coincidental shape, unrelated work'),
 'ed270b8f4d0bafc37b22343210913aa651f3933d': ('deliberate',
  'deliberate (rescan after run 9): the burst and aspect hint rows are built side by side; they differ in size policy (burst keeps its space when hidden) and button caption handling, the shared part is new/setObjectName/hide'),
 '30e2ce64224bfb71fa1adffd50f93cf3eeba1ad6': ('deliberate',
  'deliberate (rescan after run 9): include/forward-declaration blocks of two unrelated headers'),
}
if RESCAN.exists():
    for r in csv.DictReader((RESCAN / "candidates.tsv").open(), delimiter="\t"):
        if r["status"] != "new" or not in_scope(r):
            continue
        if r["fingerprint"] not in RESCAN_RULINGS:
            raise SystemExit(f"rescan candidate without a ruling: {r['name']}")
        verdict, reason = RESCAN_RULINGS[r["fingerprint"]]
        store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
        stats["rescan " + verdict] += 1

vd.save(OUT, store)
print(dict(stats))
