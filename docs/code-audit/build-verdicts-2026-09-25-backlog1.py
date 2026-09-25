"""Record the 2026-09-25 backlog step (Rückstands-Schritt 1) in
docs/code-audit/verdicts.tsv: every item that was open project-wide after
audit run 9 gets a final drawer, so none stays open.

    python3 docs/code-audit/build-verdicts-2026-09-25-backlog1.py [rescan-dir]

The 41 open items are the status-open rows of the run-9 rescan
(CLAUDE_TMP/TTCut-ng/code-audit-run9d). Drawers approved by the user on
2026-09-25: rebuild in batches R1-R6, own project in TODO.md ("Umbau-Projekte
aus den Code-Audits"), or deliberate. Three drawers changed on reading the
code, reported to the user: R2 rebuilt only the summary tail (#9), the
detection-phase clones (#5, #6, #10, #11) turned out to differ per phase;
R4 (#21) was already done in 97551783, the remaining length of detectBurst
is the decoder-opening ladder of project P9; the new projects are numbered
P8/P9 (proposed as P9/P10, no P8 existed).

rescan-dir defaults to code-audit-backlog1c, the project-wide --all rescan
after the batches (1b was before the R5 follow-up); only fingerprints that
are new against code-audit-run9d and touch a changed file are judged here,
the never-judged rest belongs to the map audits; it stays out of the repository on purpose."""
import csv, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

RESCAN = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-backlog1c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-25"
B = "backlog step 2026-09-25"
P = 'documented: TODO.md "Umbau-Projekte aus den Code-Audits" '

# the open items: fingerprint -> (kind, verdict, reason)
OPEN_RULINGS = {
 # --- R1 muxer (91c73457)
 'ec9e3f44ab17bfa104fe387cd088a4ecb9acc7ee': ('clone', 'consolidate',
  f'done {B} batch R1: TTMkvMergeProvider::freeMuxInputs shared by the cleanup of mux() and muxAudioOnly()'),
 '8734cbd01e95dee6662c95e4064fe514730c80a1': ('clone', 'consolidate',
  f'done {B} batch R1: openMatroskaOutputFile (file-static) opens the output for mux() and muxAudioOnly()'),
 '99e6535e20dee8edfbfebcc2fe58a911d6ca70a6': ('clone', 'deliberate',
  f'{B}: after batch R1 (openMatroskaOutputFile, freeMuxInputs) the clone is the trailer + cleanup + finish-log tail of mux()/muxAudioOnly(); each calls its own local cleanup lambda and logs its own text'),
 # --- R2 stream-point workers (a81057e6)
 '0389a5fd51eaea0b3f9d3d27dd4cf0e74cd5cd6c': ('clone', 'consolidate',
  f'done {B} batch R2: TTAnalysisLog::summary appends the suppressed-events count; used by both audio-worker summaries and the aspect summary'),
 '178860b8adf04902ea3c6eaca1731d5b6156c690': ('clone', 'deliberate',
  f'{B}, re-judged on reading: the three detection phases share only "step report, detect, append, debug count"; silence adds a threshold line, a summary with cancel wording and resetCap, format and aspect have neither - a runDetectionPhase helper would need one hook per difference'),
 '63fbfca2be69fa65e2097c10d33bb7cca942145c': ('clone', 'deliberate',
  f'{B}, re-judged: AudioChange (audio worker) and AspectChange (video worker) points carry different fields and log texts; the shared part is TTStreamPoint construction + append + one mLog.event'),
 '7c0e2ec05f6d637c048a08a8685954110d93c8ce': ('clone', 'deliberate',
  f'{B}, re-judged: same detection-phase shape as 178860b8, audio silence vs video aspect phase'),
 '80c05845e0b5377ef5eaba9c99453f4ee84fa3b1': ('clone', 'deliberate',
  f'{B}, re-judged: the debug line + Step report heading the silence and format phases; two phases in one operation(), texts differ'),
 # --- R3 encoder settings (9a41c40b)
 'c91c1e49': None, 'ff0d672d': None, '76313dea': None,
 # --- R5 search (7cdc16b2)
 '761dd714': None,
 # --- R6 (4afaba20)
 '84de7b31': None, 'f4e06a3e': None, '83c98b00': None,
 # --- projects
 '4570fe39': None, 'd8d036e6': None, '73f924cb': None,
 '4f8a8f66': None, '8eb7e09b': None, 'e01a30e0': None,
 '7c2df30f': None, '33c3f384': None, '0c22a8d9': None, '10fb15b8': None,
 # --- deliberate
 'a2704377': None, 'd00b343f': None, '2497d440': None, 'd193520d': None,
 '33677e78': None, '0cb085fc': None,
 'd6b881bf': None, '0f5dc237': None, '4668cac7': None, '98c694d4': None,
 '61826a15': None, 'a42d4acd': None, 'aece16c1': None, '1f4e3aea': None,
 '2509743e': None, '8f962b21': None,
}

SHORT = {
 'c91c1e49': ('consolidate', f'done {B} batch R3: updateQualityUI picks label, range and texts per codec and sets the widgets once'),
 'ff0d672d': ('consolidate', f'done {B} batch R3: same updateQualityUI rework'),
 '76313dea': ('consolidate', f'done {B} batch R3: the empty setTitle is gone; avcutdialog.ui promoted TTCutSettingsEncoder as QGroupBox with a title, now QWidget'),
 '761dd714': ('consolidate', f'done {B} batch R5: TTSearchTask::mpeg2GrayAt shared by isFrameBlackAt and buildHistogramAt; test_directed_search identical before/after on two MPEG-2 fixtures'),
 '84de7b31': ('consolidate', f'done {B} batch R6: ttplaybackmuxtask.cpp comments use //! like its header'),
 'f4e06a3e': ('consolidate', f'done {B} batch R6: openFile has one decoder-failure path and one debug-log block'),
 '83c98b00': ('consolidate', f'done {B} batch R6: computeDisplayScale dispatches to mpeg2AspectScale / sampleAspectScale'),
 '4570fe39': ('documented', P + 'P2 (TTESInfo aufräumen), user-deferred batch B7 of run 7 - ttencodernames array-size idiom'),
 'd8d036e6': ('documented', P + 'P2 (TTESInfo aufräumen) - ttencodernames array-size idiom, second pair'),
 '73f924cb': ('documented', P + 'P2 (TTESInfo aufräumen) - the shared time formatter (muxer chapter time vs goto-frame dialog)'),
 '4f8a8f66': ('documented', P + 'P8 (Dekodierpfade in TTFFmpegWrapper vereinheitlichen) - decodeFrameInternal complexity'),
 '8eb7e09b': ('documented', P + 'P8 - decodeFrameYUV size'),
 'e01a30e0': ('documented', P + 'P8 - decodeFrameYUV complexity; shares seek/guard/skip with decodeFrameInternal'),
 '7c2df30f': ('documented', P + 'P9 (Audio-Dekoder öffnen + Stream-Point-Audio-Worker teilen) - the libav open ladder in the anomaly scan and the silence detection'),
 '33c3f384': ('documented', P + 'P9 - detectSilencePoints length, mostly the decoder-opening ladder'),
 '0c22a8d9': ('documented', P + 'P9 - detectSilencePoints complexity'),
 '10fb15b8': ('documented', P + 'P9 - the per-sample-format RMS loop of the old reason is sumSquares<T> since 97551783; the remaining length of detectBurst is the decoder-opening ladder'),
 'a2704377': ('deliberate', f'{B}: legacy MPEG-2 header parsers, each reads its own fixed byte count from the buffer'),
 'd00b343f': ('deliberate', f'{B}: scanPacketsIntoRawIndex is the one packet-scan handler of TTFrameIndexer, covered by its own frame-index gates'),
 '2497d440': ('deliberate', f'{B}: TTAspectScanTask::operation is one tightly coupled scan loop over the batches'),
 'd193520d': ('deliberate', f'{B}: TTAudioAnomalyScanTask::evaluate is the labelled heuristic steps of one scan'),
 '33677e78': ('deliberate', f'{B}: setupVideoInput is one point over the threshold (26/25), single caller'),
 '0cb085fc': ('deliberate', f'{B}: runScreenshotMode is a linear list of screenshot steps'),
 'd6b881bf': ('deliberate', f'{B}, re-judged: TTAudioCutter::cut was split in earlier runs (planAudioCut, repair table); the rest is the packet loop'),
 '0f5dc237': ('deliberate', f'{B}, re-judged: same TTAudioCutter::cut packet loop'),
 '4668cac7': ('deliberate', f'{B}, re-judged: TTMkvMergeProvider::mux was split in earlier runs (setupVideoInput, audio/subtitle inputs, now freeMuxInputs); the rest is sequencing'),
 '98c694d4': ('deliberate', f'{B}, re-judged: same mux() sequencing'),
 '61826a15': ('deliberate', f'{B}, re-judged: the screenshot helper is already shared; what remains is the shape of its call sites'),
 'a42d4acd': ('deliberate', f'{B}, re-judged: make_test_video.sh: the unquoted expansions (sampled) are local assignments and numeric lavfi parameters'),
 'aece16c1': ('deliberate', f'{B}, re-judged: walk_ac3_frames (ttcut-ac3fix) 31/25, control flow of one frame walk'),
 '1f4e3aea': ('deliberate', f'{B}, re-judged: ttcut-demux helper already shared; the clone is the call-site shape around it'),
 '2509743e': ('deliberate', f'{B}, re-judged: collect_access_units (ttcut-pts-analyze) 33/25, control flow of one parse'),
 '8f962b21': ('deliberate', f'{B}, re-judged: find_video_pid (ttcut-pts-analyze) 29/25, control flow of one PID search'),
}

store = vd.load(OUT)
stats = Counter()
for key, val in OPEN_RULINGS.items():
    matches = [fp for fp in store if fp.startswith(key)]
    if len(matches) != 1:
        raise SystemExit(f"open ruling {key} matches {len(matches)} store rows")
    fp = matches[0]
    if val is None:
        verdict, reason = SHORT[key]
        kind = store[fp].kind
    else:
        kind, verdict, reason = val
    store[fp] = vd.Verdict(fp, kind, verdict, D, reason[:400])
    stats["open -> " + verdict] += 1
if sum(stats.values()) != 41:
    raise SystemExit(f"expected 41 open rulings, got {sum(stats.values())}")

# rescan after the batches: new candidates in the touched files
RESCAN_RULINGS = {
 'd2c51c6ab2f92f25deae55c366da41f0c1628952': ('deliberate',
  f'deliberate (rescan after {B}): head of isFrameBlackAt/buildHistogramAt - wrapper dispatch, mpeg2FrameAt, TTCentreBand loop; the per-sample work (black count with early exit vs histogram) differs, the band rule itself is already shared in TTCentreBand'),
 '0799f4d35086574636ee0ec49642a18bcc4d6f88': ('deliberate',
  f'deliberate (rescan after {B}): coincidental setter sequence in TTCurrentFrame::controlEnabled and TTCutSettingsEncoder::updateQualityUI'),
 '9bd387c973c40321ed10a5ce7fd27a603ceae614': ('deliberate',
  f'deliberate (rescan after {B}): end of a test function + main() of two unit harnesses; harness isolation'),
}
TOUCHED = ['extern/ttmkvmergeprovider.h', 'extern/ttmkvmergeprovider.cpp',
           'data/ttanalysislog.h', 'data/ttanalysislog.cpp',
           'data/ttstreampoint_audioworker.cpp', 'data/ttstreampoint_videoworker.cpp',
           'gui/ttcutsettingsencoder.h', 'gui/ttcutsettingsencoder.cpp', 'ui/avcutdialog.ui',
           'data/ttsearchtask.h', 'data/ttsearchtask.cpp',
           'data/ttplaybackmuxtask.cpp', 'extern/ttffmpegwrapper.cpp',
           'mpeg2window/ttmpeg2window2.h', 'mpeg2window/ttmpeg2window2.cpp',
           'tools/diag/test_analysislog.cpp']
def touched(row):
    return any(loc.split(":")[0] in TOUCHED for loc in (row.get("location") or "").split(";"))
BASE = Path("/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run9d")
if RESCAN.exists():
    before = {r["fingerprint"] for r in csv.DictReader((BASE / "candidates.tsv").open(), delimiter="\t")}
    for r in csv.DictReader((RESCAN / "candidates.tsv").open(), delimiter="\t"):
        if r["status"] != "new" or not touched(r) or r["fingerprint"] in before:
            continue
        if r["fingerprint"] not in RESCAN_RULINGS:
            raise SystemExit(f"rescan candidate without a ruling: {r['name']} {r['fingerprint']} {r['location']}")
        verdict, reason = RESCAN_RULINGS[r["fingerprint"]]
        store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
        stats["rescan " + verdict] += 1

vd.save(OUT, store)
print(dict(stats))
