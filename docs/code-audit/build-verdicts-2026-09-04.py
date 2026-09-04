"""Add the 2026-09-04 run's Layer-2 verdicts and Layer-3 rulings to
docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-04.py [audit-dir]

audit-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-2026-09-04 and must hold
candidates.tsv and verdicts/*.md of that run (kept out of the repository on
purpose). Existing rows are kept; rows for this run's candidates are added or
replaced. Consolidated findings that were rebuilt in batches A-D carry a
"done" reason so a later reader knows why the fingerprint is gone."""
import csv, re, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

O = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-09-04")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-04"

cands = list(csv.DictReader((O / "candidates.tsv").open(), delimiter="\t"))
new = [c for c in cands if c["status"] == "new"]
by_exact = {}
for c in new:
    by_exact.setdefault(f'{c["name"]} ({c["location"]})', []).append(c)
    by_exact.setdefault(c["name"], []).append(c)

# Layer-3 rulings by (kind, location-prefix) — override the classifier verdict.
RULINGS = [
    ("convention", "cpp/logging=",        "deliberate", "file logs through log-> throughout; internally consistent (conventions.md, 2026-09-04)"),
    ("convention", "cpp/indent=4",        "deliberate", "four-space block named in conventions.md; file-internal consistency"),
    ("convention", "cpp/header_comment=", "deliberate", "comment style, no structure; older forms remain in place per conventions.md"),
    ("tool",       "missingOverride: The function 'getClassName'", "consolidate", "done 2026-09-04 batch C: getClassName() deleted from TTException and all subclasses (no caller)"),
    ("method-size","TTMkvMergeProvider::mux", "deliberate", "sequential pipeline already delegating to setupVideoInput/addMediaInputs; no further clean target"),
    ("tool",       "function 'muxAudioOnly'", "deliberate", "per-track drain loop with early returns, different algorithm from mux(); no clean target"),
]
DONE = {  # (kind, name-prefix or location) -> batch note for consolidate rows that were rebuilt
    "batch B": ["constVariablePointer", "constParameterReference", "missingOverride", "noExplicitConstructor", "shadowMember", "shadowFunction",
                "unreadVariable", "unusedVariable", "variableScope", "uninitMemberVar", "returnByReference", "useStlAlgorithm",
                "passedByValue", "cstyleCast", "parameter 'workDir'", "unusedStructMember", "duplInheritedMember", "cpp/logging=ttlog"],
    "batch C": ["unusedFunction"],
    "batch A": ["uninitMemberVar 'mpPreviewCutList'", "uninitMemberVar: TTAC3AudioHeader"],
}
DONE_LOCS_D = ["common/ttthreadtaskpool.cpp", "data/ttaudioonlycuttask.cpp:90", "data/ttaudioonlycuttask.cpp:88",
               "data/ttsearchtask_blackframe.cpp:32", "extern/ttmuxlistdata.cpp", "avstream/ttdisplayordermap.cpp:211",
               "avstream/ttmpeg2videoheader.cpp:63-103", "avstream/ttnaluparser.cpp:556", "extern/ttessmartcut.cpp:1851",
               "extern/ttessmartcut.cpp:3288", "extern/ttessmartcut.cpp:2194", "extern/ttessmartcut.cpp:3138",
               "extern/ttmkvmergeprovider.cpp:480", "extern/ttaudiocutter.cpp:820", "avstream/ttframeindexer.cpp:105",
               "tools/ttcut-audiofix/ttcut-audiofix.c:403", "tools/ttcut-audiofix/ttcut-audiofix.c:415",
               "extern/ttmplexprovider.cpp", "avstream/ttmpeg2videostream.cpp", "avstream/ttmpegaudiostream.cpp",
               "tools/diag/test_mpeg2_seek.cpp", "tools/diag/test_stale_abort.cpp", "tools/diag/test_mpeg2cut_abort.cpp:139",
               "tools/diag/test_framesearch_progress.cpp:46", "tools/diag/test_previewcut_abort.cpp:119",
               "mpeg2decoder/ttmpeg2decoder.cpp", "mpeg2window/ttmpeg2window2.cpp:317", "common/ttexception.h",
               "common/ttsettings.h:27", "common/ttprogressestimator.cpp", "data/ttavlist.cpp:71", "data/ttavdata.cpp:2901",
               "data/ttcutpreviewtask", "data/ttopensubtitletask.h", "data/ttframesearchtask.cpp:93", "data/ttstreampoint_audioworker.cpp:3",
               "avstream/ttavstream", "avstream/ttvideoheaderlist.h", "avstream/ttmpegaudioheader.h", "avstream/ttac3audioheader.cpp",
               "extern/ttaudiorepairitem.h", "extern/ttencodeparameter.h", "avstream/tth265videoheader.h", "avstream/ttnaluparser.h:218",
               "data/ttavlist.h:138", "tools/diag/test_previewcut_abort.cpp", "ttcut.sh"]
NOT_DONE_D = ["avstream/ttmpeg2videoheader.cpp:63-82"]   # readFixedHeader judged not worth it (Layer 3)

blk = re.compile(r"^(?P<name>[^\n]+?) \((?P<loc>[^)]+)\) — (?P<v>consolidate|deliberate|documented|unsure)", re.M)
store = vd.load(OUT)
unmatched, n_rows = [], 0
for f in sorted((O / "verdicts").glob("*.md")):
    text = f.read_text()
    ms = list(blk.finditer(text))
    for i, m in enumerate(ms):
        name, loc, v = m["name"].strip(), m["loc"].strip(), m["v"]
        body = text[m.end(): ms[i + 1].start() if i + 1 < len(ms) else len(text)]
        ev = re.search(r"evidence[^:]*:\s*(.+)", body)
        reason = re.sub(r"\s+", " ", ev.group(1).strip())[:160] if ev else ""
        rows = by_exact.get(f"{name} ({loc})") or by_exact.get(name)
        if not rows:  # convention rows: name is the feature, loc the file; tool rows: match by location
            single = loc + "-" + loc.split(":")[-1]          # "file:318" -> "file:318-318"
            rows = [c for c in new if c["location"] in (loc, single)
                    or c["location"].startswith(loc + ";") or c["location"].startswith(loc + ":")
                    or c["location"].startswith(loc + "-")]
            if len(rows) > 1:
                rows = [c for c in rows if c["name"].startswith(name.split(":")[0][:20])] or rows[:1]
        if not rows:
            unmatched.append(f"{f.name}: {name} ({loc})"); continue
        for c in rows:
            verdict, why = v, reason
            for kind, prefix, rv, rr in RULINGS:
                if c["kind"] == kind and (c["name"].startswith(prefix) or c["location"].startswith(prefix)):
                    verdict, why = rv, rr
            if verdict == "consolidate":
                done = None
                for batch, prefixes in DONE.items():
                    if any(c["name"].startswith(p) for p in prefixes): done = batch
                if c["kind"] in ("clone", "method-size", "tool", "convention") and c["location"] and \
                   any(c["location"].startswith(p) for p in DONE_LOCS_D) and \
                   not any(c["location"].startswith(p) for p in NOT_DONE_D) and \
                   c["module"] not in ("tools/ttcut-demux", "tools/ttcut-ac3fix", "tools/ttcut-pts-analyze", "tools/test-videos") and \
                   c["kind"] != "method-size":
                    done = done or "batch D"
                if c["kind"] == "convention" and c["name"] == "bash/set_flags=none" and c["location"] == "ttcut.sh":
                    done = "decision 4: ttcut.sh removed"
                if done:
                    why = f"done {D} {done}: {why}"
            store[c["fingerprint"]] = vd.Verdict(c["fingerprint"], c["kind"], verdict, D, why)
            n_rows += 1
vd.save(OUT, store)
print("rows written", n_rows, "| store", len(store), Counter(x.verdict for x in store.values()))
print("unmatched blocks", len(unmatched))
for u in unmatched: print("  ", u)
