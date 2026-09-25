"""Close the 168 stale `consolidate` rows of docs/code-audit/verdicts.tsv
(reconciliation of 2026-09-25, after backlog step 1).

    python3 docs/code-audit/build-verdicts-2026-09-25-reconcile.py [work-dir] [rescan-dir]

The 168 rows carried `consolidate` without a "done" note although none of
their fingerprints occurs in the code any more - 127 of them from audit run 1
(2026-09-03), whose verdict script never marked its rebuilt rows. A vanished
fingerprint only says the code changed, so three read-only subagents checked
each reason text against today's code (work-dir/result{1,2,3}.tsv: fingerprint,
verdict done|alive|unclear, evidence, ...); the main session re-checked every
"alive" and eight "done" samples. Corrections of the main session: 9a1cdd49
is done (drawSubtitleOnImage is static), 1c9d71b9 is done (9e5511f0
reindented the file; the subagent counted nested 4-space lines as a second
style). The remaining alive rows were sorted by user decision of 2026-09-25:
three rebuilt on this branch, three to TODO projects, four deliberate.

work-dir defaults to CLAUDE_TMP/TTCut-ng/abgleich168, rescan-dir to
CLAUDE_TMP/TTCut-ng/code-audit-reconcile1 (the --all rescan after the
rebuilds, judged against code-audit-backlog1d). Both stay out of the
repository on purpose."""
import csv, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

WORK   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/abgleich168")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-reconcile1")
BASE   = Path("/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-backlog1d")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-25"
A = "reconciliation 2026-09-25"
P = 'documented: TODO.md "Umbau-Projekte aus den Code-Audits" '

# decisions for the rows that were alive (or corrected) - prefix of the fingerprint
DECIDED = {
 '9a1cdd49': ('consolidate', f'done ({A}): drawSubtitleOnImage is static (mpeg2window/ttmpeg2window2.h); subagent said alive, corrected'),
 '1c9d71b9': ('consolidate', f'done ({A}): 9e5511f0 reindented gui/ttcutmainwindow_headless.cpp to 2 spaces; the one flat loop body left (waitForProjectLoad) indented on this branch'),
 '5ef5da4c': ('consolidate', f'done ({A}): the gates write single-video projects through ttcut_project_xml (tools/diag/ttcut-project.sh), gate_audiofix.sh included'),
 'f8d04cb0': ('consolidate', f'done ({A}): same ttcut_project_xml helper, eight gate sites'),
 '89b1be8c': ('consolidate', f'done ({A}): TTFrameSearchTask::openMpeg2DecoderFor builds the libmpeg2 decoder for reference and search; FFmpeg side was openFFmpegWrapperFor already'),
 'c13e6646': ('deliberate', f'{A}: findIncompatibleVideos runs two loops over the same list, each needs its own skip of items without video'),
 '0c6f30e6': ('deliberate', f'{A}: original site not reconstructible (scan of run 1 purged); the only matching struct, TFrameInfo, is filled by the decoder before any read'),
 '610454c8': ('deliberate', f'{A}: the if (logX()) qDebug() shape is a project-wide idiom (scanner: idiom class), not a consolidation target'),
 '6be37f6f': ('deliberate', f'{A}: mux()/muxAudioOnly() trailer + cleanup + finish-log tail, decided deliberate in backlog step 1 (99e6535e)'),
 '7f201feb': ('documented', P + f'P1 (TTCutMainWindow-Rest) - onAnalyzeStreamPoints builds its four workers inline ({A})'),
 '3c7f80f0': ('documented', P + f'P8 (TTFFmpegWrapper zerlegen) - split the class into decode/index/analysis units ({A})'),
 'ccfaad27': ('documented', P + f'P2 (TTESInfo aufräumen) - ttencodernames array-size idiom, user-deferred batch B7 ({A})'),
}
# current rows whose reason was useless
CURRENT_FIX = {
 '7106656b': ('documented', P + f'P1 (TTCutMainWindow-Rest) - onAnalyzeStreamPoints cognitive complexity; reason was "same as above" ({A})'),
}

store = vd.load(OUT)
stats = Counter()
results = {}
for i in (1, 2, 3):
    for r in csv.DictReader((WORK / f"result{i}.tsv").open(), delimiter="\t"):
        results[r["fingerprint"]] = r
# each result row must still be a stale consolidate - or already carry this
# script's verdict (rerun after the rescan)
for fp in results:
    v = store.get(fp)
    if v is None:
        raise SystemExit(f"result for a fingerprint not in the store: {fp}")
    if not ((v.verdict == "consolidate" and "done" not in v.reason) or A in v.reason):
        raise SystemExit(f"{fp} is neither a stale consolidate nor reconciled: {v.verdict}")
for fp, r in results.items():
    if fp not in store:
        raise SystemExit(f"result for a fingerprint not in the store: {fp}")
    key = next((k for k in DECIDED if fp.startswith(k)), None)
    if key:
        verdict, reason = DECIDED[key]
    elif r["verdict"] == "done":
        verdict, reason = "consolidate", f"done ({A}): {r['evidence']}"
    else:
        raise SystemExit(f"{r['verdict']} row without a decision: {fp}")
    store[fp] = vd.Verdict(fp, store[fp].kind, verdict, D, reason[:400])
    stats["168 -> " + (verdict if not reason.startswith("done") else "done")] += 1
if len(results) != 168 or set(DECIDED) - {k for k in DECIDED for fp in results if fp.startswith(k)}:
    raise SystemExit("results incomplete or a decision matches no row")
for key, (verdict, reason) in CURRENT_FIX.items():
    fps = [fp for fp in store if fp.startswith(key)]
    if len(fps) != 1:
        raise SystemExit(f"current fix {key} matches {len(fps)} rows")
    store[fps[0]] = vd.Verdict(fps[0], store[fps[0]].kind, verdict, D, reason[:400])
    stats["current fix"] += 1

# rescan after the rebuilds: new candidates in the touched files
RESCAN_RULINGS = {
 '5e092c3e24f87fe75120c945d086d542774b02fe': ('deliberate',
  f'deliberate (rescan after {A}): project header of the two-track and the two-video gate projects and of the make_test_video.sh generator (writes <basename>.ttcut below OUTDIR) - shapes ttcut_project_xml does not cover by design'),
 'a39fd4738a745da447ffd9c01c642778d524d352': ('deliberate',
  f'deliberate (rescan after {A}): same header, the two-track and two-video gate projects only'),
 '527dabdbac6e005c04cca7aaa31d7cd6109c64ee': ('deliberate',
  f'deliberate (rescan after {A}): one-line gate wrappers around make_aspect_m2v, each calling a different harness'),
 '3bc055c7afd24640251e8184ad51ddc968b93174': ('deliberate',
  f'deliberate (rescan after {A}): write the project, run --auto-cut, check the exit code - each gate checks a different outcome afterwards'),
 '9b3e1ef69d5c363c829ad29474492d2e37ebafba': ('deliberate',
  f'deliberate (rescan after {A}): same project + --auto-cut call shape in mpeg2_framerate_cut and chapter_file, different checks after it'),
 'f23e6bbb4ca216587a5be47ca82d23b401d21470': ('deliberate',
  f'deliberate (rescan after {A}): prologue of two standalone gate scripts (set -u, ROOT, source the project helper)'),
 'd03215c52e5c99c7c85dc8b000ae0d2325303fac': ('deliberate',
  f'deliberate (rescan after {A}): shellcheck SC2329 on a gate function that run-gates.sh calls indirectly as gate_$name from its table'),
}
TOUCHED = ['tools/diag/ttcut-project.sh', 'tools/diag/run-gates.sh', 'tools/diag/gate_audiofix.sh',
           'tools/diag/gate_cut_identity.sh', 'tools/diag/qc-autocut.sh',
           'data/ttframesearchtask.h', 'data/ttframesearchtask.cpp', 'gui/ttcutmainwindow_headless.cpp']
if RESCAN.exists():
    before = {r["fingerprint"] for r in csv.DictReader((BASE / "candidates.tsv").open(), delimiter="\t")}
    for r in csv.DictReader((RESCAN / "candidates.tsv").open(), delimiter="\t"):
        if r["status"] != "new" or r["fingerprint"] in before:
            continue
        if not any(loc.split(":")[0] in TOUCHED for loc in (r.get("location") or "").split(";")):
            continue
        if r["fingerprint"] not in RESCAN_RULINGS:
            raise SystemExit(f"rescan candidate without a ruling: {r['name']} {r['fingerprint']} {r['location']}")
        verdict, reason = RESCAN_RULINGS[r["fingerprint"]]
        store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
        stats["rescan " + verdict] += 1

vd.save(OUT, store)
print(dict(stats))
