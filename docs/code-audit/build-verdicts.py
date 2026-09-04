"""Rebuild docs/code-audit/verdicts.tsv from the Layer-2 verdict files, the
Layer-3 rulings and the post-rebuild review of the rescan (run b) of the
code audit of 2026-09-03.

    python3 docs/code-audit/build-verdicts.py [audit-dir] [rescan-dir]

audit-dir  defaults to CLAUDE_TMP/TTCut-ng/code-audit-2026-09-03 and must hold
           candidates.tsv and verdicts/*.md from that run;
rescan-dir defaults to audit-dir + "b" and must hold the rescan's candidates.tsv.

The audit outputs are deliberately not part of the repository (see the
code-audit skill: never `--out` into the repo).  Without them this script is
the provenance record of verdicts.tsv, not a runnable tool.  Moved here from
the audit directory on 2026-09-04 so the derivation survives a temp purge.

Reproduces verdicts.tsv as committed in ff552802 byte for byte.  Later rows
were changed by hand (aa7ca910: module-edge rows set to `documented`), so a
rerun must not overwrite the committed file blindly — diff first."""
import re, csv, sys
from pathlib import Path
from collections import Counter
sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd
O = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-09-03")
O2 = Path(sys.argv[2] if len(sys.argv) > 2 else str(O) + "b")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-03"
cands = list(csv.DictReader((O/"candidates.tsv").open(), delimiter="\t"))
new = list(csv.DictReader((O2/"candidates.tsv").open(), delimiter="\t"))
by_name = {}
for c in cands: by_name.setdefault(c["name"], []).append(c)
store = {}
def put(c, v, reason, force=False):
    if force or c["fingerprint"] not in store:
        store[c["fingerprint"]] = vd.Verdict(c["fingerprint"], c["kind"], v, D, re.sub(r"\s+", " ", reason)[:160])
blk = re.compile(r"^(?P<name>[^\n(]+?) \((?P<loc>[^)]+)\) — (?P<v>consolidate|deliberate|documented|unsure)\s*$", re.M)
unmatched = 0
for f in sorted((O/"verdicts").glob("*.md")):
    text = f.read_text(); ms = list(blk.finditer(text))
    for i, m in enumerate(ms):
        name, loc, v = m["name"].strip(), m["loc"].strip(), m["v"]
        body = text[m.end(): ms[i+1].start() if i+1 < len(ms) else len(text)]
        ev = re.search(r"evidence:\s*(.+)", body); reason = ev.group(1).strip() if ev else ""
        rows = by_name.get(f"{name} ({loc})") or [c for c in by_name.get(name, []) if c["location"].startswith(loc)]
        if not rows:  # tool/structure blocks with rephrased names
            key = re.sub(r"^(tool|class-size|file-size|method-size|convention|clone):?\s+", "", name).split(":")[0].strip()
            rows = [c for c in cands if c["location"] == loc or c["name"] == loc]
            if not rows: rows = [c for c in cands if c["location"] == loc + "-" + loc.split(":")[-1]]
            if not rows:
                fl = loc.split(":")[0]
                rows = [c for c in cands if c["location"].startswith(fl) and key and key in c["name"]]
            if not rows and key: rows = [c for c in cands if c["name"] == key or c["name"].startswith(key + " ")]
            if len(rows) > 3: rows = []
        if not rows: unmatched += 1
        for c in rows: put(c, v, reason)
# Layer 3: class-size rows the subagents named without location
for c in cands:
    if c["kind"] == "class-size" and c["name"] == "TTFFmpegWrapper":
        put(c, "consolidate", "split into decode/index/cut/mux units as a separate project (report block 2)", True)
    if c["kind"] == "class-size" and c["name"] == "TTESSmartCut":
        put(c, "documented", "smart-cut.md documents the seam variants; size is intrinsic", True)
# Batch D ruling: the four list classes stay separate
for fp8 in ["3ba84073","6388a99f","fe9f9417","6951bf02","6ac9b9e1","11706a01"]:
    for c in cands:
        if c["fingerprint"].startswith(fp8):
            put(c, "deliberate", "list classes differ in signal semantics (clear/remove/update/swap); shared part ~50 lines per class, rulings.md batch D", True)
# Post-rebuild review of what the rescan still reports (exact location + kind)
review = [
 ("clone", "avstream/ttesinfo.cpp:422-434", "deliberate", "bounds-checked accessor idiom (audioTrack/nalUnitAt/...), five sites; no shared helper worth it"),
 ("include-cycle", "avstream<->common<->data<->extern<->mpeg2decoder", "consolidate", "remaining cycle edges after batch C: common/ttcut.h->avstream, avstream stream classes->extern (ttffmpegwrapper/tttranscode), extern->data (ttaudiorepairitem, ttmuxlistdata), tttranscode.cpp->mpeg2decoder; architecture work, separate project"),
 ("method-size", "avstream/ttesinfo.cpp:184-369", "consolidate", "parseSection still ~185 lines after parseEsRangeList (batch B); split by section kind is the next step"),
 ("tool", "avstream/ttnaluparser.cpp:1223-1223", "deliberate", "plain keyframe scan loop; std::find_if would not read better"),
 ("convention", "common/ttthreadtaskpool.h", "deliberate", "tabs expanded in batch C; whole file is consistent four-space"),
 ("clone", "extern/ttffmpegwrapper.cpp:1193-1254", "consolidate", "isFrameBlack/buildHistogram share the decode+sample skeleton; part of the wrapper split (report block 2)"),
 ("convention", "extern/ttffmpegwrapper.h", "documented", "docs/conventions.md: older comment forms remain in place; convention findings are not rebuilt"),
 ("class-size", "gui/ttcutmainwindow.cpp", "consolidate", "headless modes split off in batch C (2681 lines left, 81 methods); further split by responsibility is a separate project"),
 ("file-size", "gui/ttcutmainwindow.cpp:1-2681", "consolidate", "same as the TTCutMainWindow class-size row"),
 ("clone", "gui/ttwindowgeometry.cpp:28-35", "deliberate", "two readers with different key sets share four lines of width/height reading"),
 ("clone", "tools/vdr-demux-example.sh:375-386", "deliberate", "mirrors the user's private VDR_Demux.sh; both change together or neither (memory feedback_sync_vdr_demux_example)"),
 ("clone", "tools/test-videos/make_test_video.sh:339-342", "deliberate", "per-variant ffmpeg parameter sets; the shared part is the encoder flag tail"),
 ("tool", "tools/ttcut-ac3fix/ttcut-ac3fix.c:206-206", "consolidate", "process_ac3_file 242 lines: open/buffer setup, frame walk and report in one function; batch E extracted only args and banner"),
 ("clone", "tools/ttcut-demux/ttcut-demux:352-360", "deliberate", "detect_video_gaps_multifile / detect_audio_gaps_multifile: nine-line segment loop, callees differ in arity; a shared loop would need an indirection for one call"),
 ("tool", "tools/ttcut-demux/ttcut-demux:1314-1314", "deliberate", "_map is a nameref onto declare -A maps; explicit $idx subscript kept"),
 ("tool", "tools/ttcut-demux/ttcut-demux:1318-1318", "deliberate", "same as line 1314"),
 ("tool", "tools/ttcut-demux/ttcut-demux:2503-2503", "deliberate", "$ENCODER is an intentionally word-split argument list"),
 ("tool", "tools/test-videos/make_test_video.sh:301-301", "deliberate", "$(build_tux_timeline_args ...) is an intentionally word-split argument list"),
]
n = 0
for kind, loc, v, reason in review:
    hit = [c for c in new if c["kind"] == kind and (c["location"] == loc or c["location"].startswith(loc + ";"))]
    if not hit: print("review: no match", kind, loc)
    for c in hit:
        if c["fingerprint"] in store or kind in ("tool", "include-cycle", "method-size", "class-size", "file-size", "convention", "clone"):
            put(c, v, reason, True); n += 1
vd.save(OUT, store)
print("unmatched blocks", unmatched, "| review rows", n, "| store", len(store), Counter(x.verdict for x in store.values()))
