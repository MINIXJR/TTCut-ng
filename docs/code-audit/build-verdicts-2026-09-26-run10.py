"""Add the audit-run-10 verdicts (scope: the 16 source files of
docs/code-map/audio-repair.md plus its spec) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-26-run10.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-reconcile2, the scan the
run started from (no in-scope item was open, 51 never judged); rescan-dir to
code-audit-run10b, the rescan after the batches. Judged in the main session,
no subagents; drawers approved by the user on 2026-09-25: 17 rebuilt in
batches C1-C3, 4 to project P9 (TODO.md), 28 deliberate, the two four-space
files added to the cpp/indent exception list of docs/conventions.md."""
import csv, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-reconcile2")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run10b")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-26"

C1 = 'done 2026-09-25 audit run 10 batch C1: TTAudioCutter::ensureAc3Codecs and the write*Packet helpers are static members taking the CutSession'
C3 = 'done 2026-09-25 audit run 10 batch C3: buildRepairTable releases its libav resources through one qScopeGuard and leaves through a fail() helper'
P9 = 'documented: TODO.md "Umbau-Projekte aus den Code-Audits" P9 (Audio-Dekoder oeffnen + Stream-Point-Audio-Worker teilen) - '
CUTSESSION = ('deliberate',
  'TTAudioCutter::CutSession is the per-call state struct of cut() (extern/ttaudiocutter.cpp: "Lives on cut()\'s stack; the helpers below it are the named steps of its packet loop"); its fields are the shared state of those steps, not an encapsulated class')

RULINGS = {
 # C1 - static packet helpers
 '69cbe7f68562dff62b27cf87bf2b654ef112421e': ('consolidate', C1),
 '587a15455c0eb86c7c7bcc686a7fb57470a4d03d': ('consolidate', C1),
 'b7e2db2f6228a5670f10b6b63b3e9748d5f8ba47': ('consolidate', C1),
 '1dca5fbcaebe8a32faab230f682a6920a52ac356': ('consolidate', C1),
 'aeeb8fb2be85e6146ef497e2000227dffc7e4a60': ('consolidate', C1),
 'd12c74b8c4de0121df9e2640ba59215557880251': ('consolidate', C1),
 'bbcd0206d656e454be8cb13e4a83e3082bf7e529': ('consolidate', C1),
 '7c8486f3ebe843a82e02d402131ed000dc8e6f4c': ('consolidate', C1),
 '1abd84327117c3ad447809882e7c17b6c338364b': ('consolidate', C1 + '; the C-style cast replaced'),
 # C2 - shared write tail
 '11dfb25a1a82c1c8cf9199bef260d09f1e712b1a': ('consolidate',
  'done 2026-09-25 audit run 10 batch C2: writeRepairedPacket and writeStreamCopyPacket share writeOnOutputTimeline'),
 # C3 - buildRepairTable
 '01762d1ff48952bce33463dfa70ebd829451e6ad': ('consolidate', C3),
 '270676b495bad38a2d75f7d604ad0b4bb1a16f0c': ('consolidate', C3),
 'a69ff7b3e9e6ae4d26521a534a6c6987d6a7cdd5': ('consolidate', C3),
 '16cbf6978581976deed9153260d5e022497d7d6b': ('consolidate', C3),
 '31c159f4ffcf943de83c791c938a644571e75ead': ('consolidate', C3 + '; the C-style cast replaced'),
 # the two size/complexity findings survive C3 (313 -> 258 lines, cognitive 95 -> 83)
 '8085122e4f0be50ef97a23a55179d832f2e6c925': ('deliberate',
  'rebuilt in audit run 10 batch C3 (313 -> 258 lines, cognitive complexity 95 -> 83); what remains is one linear decode -> mix -> encode -> splice-check pipeline with one error exit per libav step, the procedure was deliberately left unchanged (byte-identical gates audiorepair*)'),
 '3fa7eabdae23965f4a5c50d1f2fadac59da19bde': ('deliberate',
  'same buildRepairTable pipeline after batch C3 (cognitive complexity 95 -> 83)'),
 # P9
 '6d68c37373f9425edbbe541ca21ff7051cabdbc1': ('documented', P9 +
  'the open/find_stream_info ladder of ttOpenInput (avstream/ttavutil.cpp) and openFirstAudioStream (extern/ttaudiorepair.cpp) is the shared audio opener P9 creates'),
 'a81ec51e82b58283a5ce29dfef9e6453cca6e89c': ('documented', P9 +
  'the length of TTAudioCutter::detectBurst is mostly that opener ladder; split by phases after it'),
 '891b29350b05facd6a31ed6eaba81e4e9a3de7b1': ('documented', P9 + 'same detectBurst'),
 'd392b4da6d2cde871d26a921034e1588f2665eb2': ('documented', P9 +
  'sumSq lives in detectBurst\'s RMS loop, which the P9 phase split restructures'),
 # deliberate
 '30879865b3884bfd911f8382a3545cecb553c6c7': ('deliberate',
  'backlog step 2026-09-25 already judged TTAudioCutter::cut: split in earlier runs (planAudioCut, repair table), the rest is the packet loop'),
 '0db874cbd643718814a61871531f855adc4ffb3e': ('deliberate',
  'the index loop in TTAudioAnomalyScanTask filters by two fields and appends indices; std::transform would not express the filter and a copy_if over indices reads worse'),
 '296e8e7f4a1d820cd4ba752e3886a2d5c33440de': ('deliberate',
  'detectBurst\'s peak loop runs over a two-element index window (checkStart..checkEnd), an accumulate over iterator arithmetic reads worse'),
 '3054aa0871f0c46300819f6d8204b11863e60897': ('deliberate',
  'two of the three sites are harnesses (test_mka_interleave, test_preview_clip) kept self-contained; the dialog site probeFrameDurationMs is an audio opener that P9 covers'),
 '4b0e56b0b2a34e4b97a820e355b45d662be29071': ('deliberate',
  'docs/conventions.md cpp/indent names extern/ttaudiocutter as a four-space exception file (added in audit run 10, user decision 2026-09-25); the file is four-space throughout'),
 '598a01b05837a61275bbcd618a9d466c2a5a6f53': ('deliberate',
  'docs/conventions.md cpp/indent names extern/ttaudiorepair as a four-space exception file (added in audit run 10, user decision 2026-09-25); the file is four-space throughout'),
}
for fp in ('7f7387d9faade941e387a071538c59412f1fb303', '48ac0a659da80e357bbca1aa1450ebfeaf27957a',
           'c13d2fd07b6be7880b67ed09adb25eddff6cae34', '4867957cb35b5423679e3c4cd6405b4a3b6cd3fd',
           '0611728c997f470e9133a6d0e2d6a210f4807394', 'a29191e0e9e69b96646252e64671ef07b3ac4640',
           '56da8d4a947ada51a3af87a996a0b3e1e7e66eb9', '1eb9b489364a3aa316fb0cb0d90405197665dcef',
           '2516928c5f0034d9e8e87900078cf5545afeb0c7', '5ca2c0342f7dc25f535624bc51ea74c5fcfc7a2b',
           '3ee70742736f7c8b98f655535ea6151cdfa7316a', 'cff8227f6c0d3c7a8f66b5e4ffbecb73b1905331',
           'f5ff489f60959655398b62c2a2d94721195a6062', '6fb446ecd95d9eb6eb8655aae64083da79b4f859',
           'fb6bf18a6d0567c2d396c2630ab84c8bb4a213cf', '28078fb63f996a031d8b917f71336bf21fa73a35',
           '1ec4dee788a84474c22539712a88437f8b93ea54', '16b4378632d06c9b3a965ccd2edac341ff823a6c',
           '8c94a193fc1dcc4f284b9450f18276cbd731ee1c', 'ef8f21d8254258b0dd8559ecebf1303d22aa1250',
           'f62ecc904cad16456179145073eb1f0374eb9c77', '6dd509b859677d7f4ea5a8cc353408657f96e25e',
           'd4bdd0fd71d2f9eee48ecae536cbc5a44c6d213a', 'd2d10a34f3b5d70771a8a5636a60179dd46b3818'):
    RULINGS[fp] = CUTSESSION

RUN_SCOPE = [l.split()[0] for l in """
data/ttaudioanomalyscantask.h
data/ttaudioanomalyscantask.cpp
extern/ttaudiorepairitem.h
extern/ttaudiorepair.h
extern/ttaudiorepair.cpp
gui/ttaudiorepairdialog.h
gui/ttaudiorepairdialog.cpp
gui/ttstreampointwidget.cpp
gui/ttcutmainwindow.cpp
data/ttavlist.h
data/ttavlist.cpp
data/ttavdata.h
data/ttavdata.cpp
data/ttcutprojectdata.cpp
data/ttstreampoint.h
extern/ttaudiocutter.cpp
docs/superpowers/specs/2026-08-19-audio-anomaly-repair-design.md
""".strip().splitlines()]
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

# rescan after the batches: only fingerprints the run-start scan did not have
RESCAN_RULINGS = {
 'aeb0c52b5000ec83cf6c8cf30a5a8a4d17b9de76': ('deliberate',
  'deliberate (rescan after run 10): the qScopeGuard release lists of buildRepairTable (batch C3) and TTStreamPointAudioWorker::detectSilencePoints share the shape "free each handle", but each frees its own resource set'),
 '0158f44b68669a385cf45bc27f9acc44601fb0ba': ('deliberate',
  'deliberate (rescan after run 10): decode step (send_packet/receive_frame) and encode step (send_frame/receive_packet) of buildRepairTable; different libav calls and messages, only the fail() exit shape of batch C3 is shared'),
 'e3154d27f09b8da3376550ccf22ca700ced52b4a': ('deliberate',
  'deliberate (rescan after run 10): libav read-loop head "skip foreign streams, count the audio packet" in buildRepairTable and the dialog\'s listen-window writer; the packet ordinal is the frame number rule of audio-repair.md, the loop bodies decode resp. copy source bytes'),
}
if RESCAN.exists():
    for r in csv.DictReader((RESCAN / "candidates.tsv").open(), delimiter="\t"):
        if r["status"] != "new" or not in_scope(r) or r["fingerprint"] in seen:
            continue
        if r["fingerprint"] not in RESCAN_RULINGS:
            raise SystemExit(f"rescan candidate without a ruling: {r['name']}")
        verdict, reason = RESCAN_RULINGS[r["fingerprint"]]
        store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
        stats["rescan " + verdict] += 1

vd.save(OUT, store)
print(dict(stats))
