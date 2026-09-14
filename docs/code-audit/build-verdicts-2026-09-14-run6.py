"""Add the 2026-09-14 run-6 verdicts (audit run 6, scope: the 21 source files
of docs/code-map/cut-edit-and-start.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-14-run6.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run6 and must hold
candidates.tsv; rescan-dir defaults to code-audit-run6b, the rescan after the
batches. Both stay out of the repository on purpose.

Two subagents (sonnet, one per module) classified the 27 never-judged
in-scope candidates against the map; the main session verified every
consolidate and spot-checked the deliberates. The verdicts are spelled out
below rather than parsed from the agents' reports: their candidate names
carry line numbers that had already moved."""
import csv, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run6")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run6b")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-14"

# name prefix -> (verdict, reason). One entry per in-scope candidate of run 6.
RULINGS = [
 ("clone x2 (data/ttcutpreviewtask.cpp:534-546)", "consolidate",
  "OPEN: encoder setup (setPresetOverride + initialize + the TTH26xVideoStream display-map cast) stands in TTCutPreviewTask::operation and TTCutPreview::regenerateSmartCutPreviewClip; part of the second preview pipeline, deferred to the separate batch G"),
 ("clone x2 (data/ttavlist.cpp:132-155)", "deliberate",
  "onAudioDelayChanged guards its index, onAudioLanguageChanged does not (the scanner normalises that away); copy-mutate-update over two independent value types without a common base"),
 ("clone x3 (data/ttaudiolist.cpp:73-88)", "deliberate",
  "TTAudioItem::operator== compares pointer identity, TTCutItem::operator== a QUuid - different identity models, no shared base class"),
 ("clone x3 (data/ttaudiolist.cpp:45-58)", "deliberate",
  "copy constructors of four item families, each copying its own field set (with/without mID); no common base to hold them"),
 ("clone x2 (data/ttcutlist.cpp:72-95)", "deliberate",
  "TTCutItem and TTMarkerItem match field for field, but none of the four item families shares a base class; the map records that gap as accepted, not as an open step"),
 ("clone x2 (data/ttavlist.cpp:41-46)", "deliberate",
  "one site wires data-model sublists, the other mpv player signals - only the connect() syntax matches"),
 ("cpp/member_prefix=none", "deliberate",
  "data/tth26xcuttask.h: the flagged type is TTH26xCutParams, documented as a value bundle; the class itself uses the m prefix throughout, and ttaudioonlycuttask.h mirrors the same shape"),
 ("cpp/member_prefix=none", "deliberate",
  "gui/ttcutoutframe.h: TriTime-era naming, locally consistent and shared with the other original-TTCut widget classes; renaming members is a mechanical batch of its own, never a side effect"),
 ("missingOverride: The function 'onUserAbort'", "consolidate",
  "done 2026-09-14 batch E (91e088ff, batch E): override added on both TTCutVideoTask::onUserAbort and TTCutTask::onUserAbort; the younger sibling tasks already had it"),
 ("missingOverride: The function 'onUserAbort'", "consolidate",
  "done 2026-09-14 batch E (91e088ff, batch E): the second of the two slots in ttcutvideotask.h, same fix"),
 ("returnByReference: Function 'exitMessage()'", "deliberate",
  "QString is copy-on-write; the identically shaped, unflagged getters of the sibling TTH26xCutTask show return-by-value is the consistent local idiom"),
 ("returnByReference: Function 'lastError()'", "deliberate",
  "same as exitMessage(): copy-on-write QString, consistent with the sibling task class"),
 ("returnByReference: Function 'outputSummary()'", "deliberate",
  "same as exitMessage(): copy-on-write QString, consistent with the sibling task class"),
 ("unusedFunction: The function 'removeMarker'", "consolidate",
  "done 2026-09-14 batch D (91e088ff, batch D): removed - declaration and definition only, no caller; appendMarker is used, TODO.md records no marker-removal path"),
 ("useInitializationList: Variable 'mID'", "deliberate",
  "the whole constructor assigns in the body, not just mID; that is the module-wide legacy convention and a single fix would break local consistency"),
 ("clone x4 (gui/ttcurrentframe.cpp:111-121)", "deliberate",
  "four unrelated widgets (playback panel, cut dialog, cut-out still, preview dialog) wiring their own buttons; the local precedent TTTrackTreeView::bindListWidgets was extracted only because its widgets already shared a base class"),
 ("clone x2 (gui/ttcuttreeview.cpp:765-775)", "deliberate",
  "TTCutTreeView::createActions: eight ttMakeAction+connect pairs with distinct label/icon/slot, already using the shared helper; the declarative action list is the local convention"),
 ("clone x2 (gui/ttcurrentframe.cpp:111-123)", "deliberate",
  "subset of the four-widget icon/connect class above, same reasoning"),
 ("clone x2 (gui/ttcutavcutdlg.cpp:49-61)", "deliberate",
  "subset of the four-widget icon/connect class above, same reasoning"),
 ("clone x3 (gui/ttcuttreeview.cpp:365-372)", "consolidate",
  "done 2026-09-14 batch E (91e088ff, batch E): currentCutIndex() holds the guard and the row->entry lookup for onEntrySelected, onGotoCutIn, onGotoCutOut and the fourth site onItemSelectionChanged that the scanner missed"),
 ("clone x3 (gui/ttaudiotreeview.cpp:40-46)", "deliberate",
  "per-widget column setup in the track views; TTTrackTreeView deliberately left the header/column data out of the shared base"),
 ("clone x2 (gui/ttaudiotreeview.cpp:93-106)", "deliberate",
  "row builders for different item types and column sets, one embedding widgets the other does not"),
 ("method 'acmodHint' can be made static", "deliberate",
  "kept non-static for signature symmetry with its sibling burstHint, which genuinely needs this"),
 ("noExplicitConstructor: Class 'TTCutAVCutDlg'", "consolidate",
  "done 2026-09-14 batch E (91e088ff, batch E): explicit added; measured majority among gui widget constructors is 22 explicit against 8"),
 ("noExplicitConstructor: Class 'TTCutOutFrame'", "consolidate",
  "done 2026-09-14 batch E (91e088ff, batch E): explicit added, same measurement"),
 ("noExplicitConstructor: Class 'TTCutTreeView'", "consolidate",
  "done 2026-09-14 batch C (91e088ff, batch C): explicit added together with the destructor the job-list ownership needed"),
 ("unusedFunction: The function 'onEditCutOut'", "consolidate",
  "done 2026-09-14 batch D (91e088ff, batch D): removed with its cutOutUpdated signal - no caller, no receiver, unchanged since the initial commit; the live gesture is onEntryEdit -> TTCutFrameNavigation::onEditCut"),
]

# The rescan after the batches reports the same classes as new because a clone
# fingerprint carries the site count and the lines moved. Each of these points
# at the ruling above that already covers it.
RESCAN_NOTE = "rescan after the run-6 batches: same class as the run-6 ruling above, re-fingerprinted because the lines moved"

def scope_files():
    lines = (Path(__file__).resolve().parents[2] / "docs/code-map/cut-edit-and-start.md").read_text().split("\n")
    out, inside = [], False
    for ln in lines:
        if ln.startswith("sources:"): inside = True; continue
        if inside:
            if ln.startswith("  - "): out.append(ln[4:].strip())
            elif ln.strip() == "---": break
    return out

SCOPE = scope_files()
def in_scope(row):
    blob = (row.get("location") or "") + " " + (row.get("sources") or "")
    return any(f in blob for f in SCOPE)

store = vd.load(OUT)
stats = Counter()

rows = [r for r in csv.DictReader((SCAN / "candidates.tsv").open(), delimiter="\t")
        if r["status"] == "new" and in_scope(r)]
used = set()
for prefix, verdict, reason in RULINGS:
    hits = [r for r in rows if r["name"].startswith(prefix) and r["fingerprint"] not in used]
    if not hits:
        raise SystemExit(f"ruling matches no candidate: {prefix}")
    r = hits[0]
    used.add(r["fingerprint"])
    store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
    stats[verdict] += 1
missing = [r["name"] for r in rows if r["fingerprint"] not in used]
if missing:
    raise SystemExit(f"{len(missing)} in-scope candidates without a ruling: {missing}")

rescan = [r for r in csv.DictReader((RESCAN / "candidates.tsv").open(), delimiter="\t")
          if r["status"] == "new" and in_scope(r)]
for r in rescan:
    if r["fingerprint"] in store:
        continue
    store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], "deliberate", D, RESCAN_NOTE)
    stats["rescan"] += 1

vd.save(OUT, store)
print(f"rulings: {len(RULINGS)}   rescan rows: {stats['rescan']}   store size: {len(store)}")
print(dict(stats))
