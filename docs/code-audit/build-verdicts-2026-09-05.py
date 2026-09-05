"""Mark the 2026-09-04 run's consolidate verdicts that batches E and F
rebuilt on 2026-09-05 as done in docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-05.py [audit-dir]

audit-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-2026-09-04 (its
candidates.tsv supplies module and location per fingerprint). Only rows of
that run with verdict `consolidate` are touched; rows already marked done
keep their note. Batch F swept every consolidate verdict of the four tool
modules (all were rebuilt), batch E is matched by exact location."""
import csv, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

O = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-09-04")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-05"

F_MODULES = {"tools/ttcut-demux": "batch F (ttcut-demux helpers, 927f0968)",
             "tools/ttcut-ac3fix": "batch F (process_one_frame, switch table, b5141328)",
             "tools/ttcut-pts-analyze": "batch F (section/AU/grid helpers, dac1cf58)",
             "tools/test-videos": "batch F (skip_if_present, 5e84d157)"}
E_LOCATIONS = {  # location prefix -> note
    "data/ttaudioanomalyscantask.cpp:266-419": "batch E1 (qScopeGuard, e3e585e7)",
    "extern/ttaudiocutter.cpp:145-626":        "batch E2 (CutSession + five steps, 5d38ae80)",
    "extern/ttaudiocutter.cpp:145-145":        "batch E2 (CutSession + five steps, 5d38ae80)",
    "data/ttaudioonlycuttask.cpp:68-142":      "batch E3 (TTAbortableTask, de4df8dd)",
    "data/ttaudioonlycuttask.cpp:78-117":      "batch E3 (TTAbortableTask, de4df8dd)",
    "gui/ttsubtitletreeview.h":                "batch E4 (the unprefixed action members moved into TTTrackTreeView as mp*)",
    "gui/ttaudiotreeview.cpp:239-249":         "batch E4 (TTTrackTreeView)",
    "gui/ttaudiotreeview.cpp:48-130":          "batch E4 (TTTrackTreeView)",
    "gui/ttaudiotreeview.cpp:239-250":         "batch E4 (TTTrackTreeView)",
    "gui/ttaudiotreeview.cpp:131-219":         "batch E4 (TTTrackTreeView)",
    "gui/ttaudiotreeview.cpp:216-263":         "batch E4 (TTTrackTreeView)",
    "gui/ttaudiotreeview.cpp:229-249":         "batch E4 (TTTrackTreeView)",
    "gui/ttaudiotreeview.cpp:229-250":         "batch E4 (TTTrackTreeView)",
    "gui/ttsubtitletreeview.cpp:207-207":      "batch E4 (dead contextMenu member gone with the base class)",
}

# Layer-3 ruling 2026-09-05: the two open-stream tasks stay separate (user decision; a
# template helper for their ~15 shared lines would add more machinery than it removes).
RULED_DELIBERATE = {"data/ttopenaudiotask.cpp:28-78": "TTOpenAudioTask/TTOpenSubtitleTask kept separate (decision 2026-09-05): a template helper for the shared open/validate/connect sequence costs more than the 15 lines it saves"}

cands = {c["fingerprint"]: c for c in csv.DictReader((O / "candidates.tsv").open(), delimiter="\t")}
store = vd.load(OUT)
n = 0
for fp, v in store.items():
    if v.verdict != "consolidate" or v.date != "2026-09-04" or v.reason.startswith("done "):
        continue
    c = cands.get(fp)
    if not c:
        continue
    for loc, why in RULED_DELIBERATE.items():
        if c["location"].startswith(loc):
            store[fp] = vd.Verdict(fp, v.kind, "deliberate", D, why); n += 1
    if store[fp].verdict != "consolidate":
        continue
    note = F_MODULES.get(c["module"])
    for loc, e_note in E_LOCATIONS.items():
        if c["location"].startswith(loc):
            note = e_note
    if not note:
        continue
    store[fp] = vd.Verdict(fp, v.kind, "consolidate", D, f"done {D} {note}: {v.reason}")
    n += 1
vd.save(OUT, store)
left = [c for fp, c in cands.items() if fp in store and store[fp].verdict == "consolidate" and not store[fp].reason.startswith("done ")]
print("rows marked done", n, "| store", len(store), Counter(x.verdict for x in store.values()))
print("consolidate still open from this run:", len(left))
for c in left: print("  ", c["module"], c["location"][:70], "|", c["name"][:50])
