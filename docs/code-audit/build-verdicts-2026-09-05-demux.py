"""Verdicts of the 2026-09-05 single-module pass over tools/ttcut-demux
(scan with --all, audit-dir CLAUDE_TMP/TTCut-ng/code-audit-2026-09-05-demux).

    python3 docs/code-audit/build-verdicts-2026-09-05-demux.py [audit-dir]

Six candidates were new after batch F (five clones inside the helpers that
batch introduced, one shellcheck SC2153 false positive caused by the nameref
in _parse_fraction) plus one re-surfaced quoting site; all but the false
positive were rebuilt in the same pass. The 26 known rows keep their verdicts."""
import csv, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

O = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-2026-09-05-demux")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-05"
DONE = "done 2026-09-05 ttcut-demux rest pass: "
RULES = [  # (location prefix, verdict, reason)
    ("tools/ttcut-demux/ttcut-demux:87-96",   "consolidate", DONE + "_ffprobe front end shared by _probe_first_packet_pts, _probe_field, _probe_stream_field, _probe_pts_extreme"),
    ("tools/ttcut-demux/ttcut-demux:97-98",   "consolidate", DONE + "same _ffprobe front end"),
    ("tools/ttcut-demux/ttcut-demux:98-99",   "consolidate", DONE + "same _ffprobe front end (also _probe_pts_extreme)"),
    ("tools/ttcut-demux/ttcut-demux:116-118", "consolidate", DONE + "sixth $FRAME_RATE re-parse (audiofix block) reads FRAME_RATE_NUM/DEN"),
    ("tools/ttcut-demux/ttcut-demux:411-420", "consolidate", DONE + "_probe_segment_timing for the three start_time/duration pairs of the multi-file scanners"),
    ("tools/ttcut-demux/ttcut-demux:1969-1969","deliberate",  "shellcheck cannot see the nameref assignment in _parse_fraction; SC2153 disabled at the call with a note"),
    ("tools/ttcut-demux/ttcut-demux:2546-2546","consolidate", DONE + "ENCODER is an array now, expanded quoted"),
]

cands = [c for c in csv.DictReader((O / "candidates.tsv").open(), delimiter="\t")
         if c["module"] == "tools/ttcut-demux" and c["status"] in ("new", "open")]
store = vd.load(OUT)
n = 0
for c in cands:
    for loc, verdict, why in RULES:
        if c["location"].startswith(loc):
            store[c["fingerprint"]] = vd.Verdict(c["fingerprint"], c["kind"], verdict, D, why)
            n += 1
vd.save(OUT, store)
print("rows written", n, "of", len(cands), "| store", len(store), Counter(x.verdict for x in store.values()))
