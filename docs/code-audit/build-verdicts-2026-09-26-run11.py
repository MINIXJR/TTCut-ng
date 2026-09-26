"""Add the audit-run-11 verdicts (scope: the 27 source files of
docs/code-map/audio-es-input.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-26-run11.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run11 (118 never judged,
none open in scope); rescan-dir to code-audit-run11c, the rescan after
batches C1-C4. Judged in the main session (almost all are cppcheck style
findings in the original TTCut audio classes); drawers approved by the user
on 2026-09-26: C1 dead fields, C2 mechanical, C3 two clones into the base
classes, the rest deliberate. C4 and P10 came out of the rescan."""
import csv, re, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run11")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run11c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-26"

C1 = 'done 2026-09-26 audit run 11 batch C1: never-read field or function removed'
C2 = 'done 2026-09-26 audit run 11 batch C2 (mechanical: override/explicit/initializers/scope/casts/const; the sites inside the per-codec streamLengthTime went with C3)'
P10 = ('documented: TODO.md "Umbau-Projekte aus den Code-Audits" P10 (one header-list walk for MPEG audio '
       'and AC3) - ')
DEAD = {'TTAudioStream::audio_delay', 'TTAudioStream::frame_length', 'TTAudioStream::frame_time',
        'TTAudioStream::samples_count', 'TTAudioHeader::bit_rate', 'TTAudioHeader::sample_rate',
        'TTMpegAudioHeader::bit_rate', 'TTMpegAudioHeader::sample_rate', 'TTAC3AudioHeader::crc1'}

# clone and convention candidates, by fingerprint
RULINGS = {
 '7371f901': ('consolidate', 'done 2026-09-26 audit run 11 batch C3: TTAudioStream::streamLengthTime replaces the MPEG and AC3 copies; the SRT one reads another list type'),
 'cb998101': ('consolidate', 'done 2026-09-26 audit run 11 batch C3: same streamLengthTime'),
 '60564be8': ('consolidate', 'done 2026-09-26 audit run 11 batch C3: TTAudioHeader::bitRateString/sampleRateString replace the MPEG and AC3 copies'),
 '5d31fb56': ('consolidate', 'done 2026-09-26 audit run 11 (fix H1/H5): the MPEG-1 and MPEG-2 frame-length formulas are one samples-per-frame computation'),
 '34a6666e': ('documented', P10 + 'end of createHeaderList (progress, swallowed EOF, log)'),
 '0abbfcfd': ('documented', P10 + 'loop head of createHeaderList (abort, sync search, header read)'),
 'c650a7b1': ('deliberate', 'the abort check + status-report head of a header-list loop, shared with TTMpeg2VideoStream::createHeaderList; different streams and exceptions'),
 'd02fba3b': ('deliberate', 'the two audio header classes declare the same virtual interface (desc/mode/bit rate/sample rate) - an interface, not duplicated logic'),
 '1494f413': ('deliberate', 'the AC3 acmod channel table also lives in a harness (test_acmod_majority) and in the C tool ttcut-audiofix, separate programs kept self-contained'),
 '36b48131': ('deliberate', 'AC3 table copy in the harness test_audiocutter_paths, kept self-contained'),
 'bf59dd45': ('deliberate', 'TTVideoStream::moveToNextPIFrame/moveToPrevPIFrame are mirror images (min vs max of the two hits), commented as symmetric on purpose'),
 '43ae81b2': ('deliberate', 'TTVideoIndex and TTCutParameter getter/setter idiom'),
 'c052d1b5': ('deliberate', 'same getter/setter idiom'),
 'b6219e82': ('deliberate', 'TTHeaderList::deleteAll and TTVideoIndexList::deleteAll free different element types; TTVideoIndexList is not a TTHeaderList'),
 '3a32d2d8': ('deliberate', 'checkIndexRange of TTHeaderList and TTVideoIndexList, same reason'),
 '6b4fb511': ('deliberate', 'TTMpeg2VideoHeader re-declares the pure virtual readHeader/parseBasicData of TTVideoHeader for its subclasses (MPEG-2 video, outside this map)'),
 '1958706d': ('deliberate', 'typed accessor per header-list kind (audioHeaderAt / subtitleHeaderAt)'),
 '8c23d2f0': ('deliberate', 'sort comparator per header-list kind, each on its own time field'),
 'e34e3ec4': ('deliberate', 'descString and modeString switch over different header fields (version/layer vs mode)'),
 'cd99f92e': ('deliberate', 'constructor/destructor shape of the three TTAVTypes siblings (audio, video, subtitle)'),
 '6e948cbf': ('deliberate', 'same TTAVTypes siblings, type detection per kind (sync bytes, libav probe, suffix)'),
 'b3425dbd': ('deliberate', 'declaration shape of TTOpenAudioTask and TTOpenSubtitleTask (two open tasks, different stream types)'),
 'ced8c9e3': ('deliberate', 'legacy TTCut member names in ttavstream.h; docs/conventions.md: renamed when the class is reworked - C1/C2 were no rework'),
 'f7b9d703': ('deliberate', 'legacy TTCut member names in ttavtypes.h, same rule'),
 'ba77d346': ('deliberate', 'MPEG audio header fields named after the ISO 11172-3 syntax elements'),
}

def tool_ruling(name):
    m = re.match(r"(\w+):", name)
    check = m.group(1) if m else ''
    if check == 'uninitMemberVar':
        member = re.search(r"'([^']+)'", name).group(1)
        if member in DEAD:
            return 'consolidate', C1 + f' ({member})'
        if member == 'TTVideoStream::video_index':
            return 'consolidate', 'done 2026-09-26 audit run 11 batch C2: the never-read TTVideoStream::video_index removed'
        return 'consolidate', C2 + f': {member} initialized'
    if check in ('duplInheritedMember', 'unusedFunction'):
        return 'consolidate', C1
    if check in ('missingOverride', 'noExplicitConstructor', 'variableScope', 'constVariablePointer',
                 'dangerousTypeCast', 'cstyleCast', 'passedByValue', 'constParameterPointer',
                 'noCopyConstructor', 'noOperatorEq', 'returnByReference', 'shadowFunction',
                 'useInitializationList', 'unsignedLessThanZero'):
        return 'consolidate', C2
    return None

RUN_SCOPE = [l.split()[0] for l in """
avstream/ttavtypes.h
avstream/ttavtypes.cpp
avstream/ttavheader.h
avstream/ttavheader.cpp
avstream/ttheaderlist.h
avstream/ttheaderlist.cpp
avstream/ttaudioheaderlist.h
avstream/ttaudioheaderlist.cpp
avstream/ttmpegaudiostream.h
avstream/ttmpegaudiostream.cpp
avstream/ttac3audiostream.h
avstream/ttac3audiostream.cpp
avstream/ttmpegaudioheader.h
avstream/ttmpegaudioheader.cpp
avstream/ttac3audioheader.h
avstream/ttac3audioheader.cpp
avstream/ttavstream.h
avstream/ttavstream.cpp
data/ttopenaudiotask.h
data/ttopenaudiotask.cpp
data/ttaudiolist.cpp
data/ttavlist.cpp
data/ttavdata.cpp
avstream/ttac3acmod.cpp
data/ttstreampoint_audioworker.cpp
data/ttcutprojectdata.cpp
gui/ttcutmainwindow.cpp
""".strip().splitlines()]
def in_scope(row, files=RUN_SCOPE):
    return any(loc.split(":")[0] in files for loc in (row.get("location") or "").split(";"))

store = vd.load(OUT)
stats = Counter()
used = set()
for r in csv.DictReader((SCAN / "candidates.tsv").open(), delimiter="\t"):
    if r["status"] != "new" or not in_scope(r):
        continue
    fp = r["fingerprint"]
    ruling = RULINGS.get(fp[:8]) if r["kind"] != "tool" else tool_ruling(r["name"])
    if ruling is None:
        raise SystemExit(f"in-scope candidate without a ruling: {r['kind']} {r['name']}")
    if r["kind"] != "tool":
        used.add(fp[:8])
    verdict, reason = ruling
    store[fp] = vd.Verdict(fp, r["kind"], verdict, D, reason[:400])
    stats["new " + verdict] += 1
if set(RULINGS) - used:
    raise SystemExit(f"rulings match no candidate: {set(RULINGS) - used}")

RESCAN_RULINGS = {
 'a86bc731': ('documented', P10 + 'constructor, stream type and sync search of the two parsers (rescan after run 11)'),
 '6cf8906f': ('documented', P10 + 'abs_frame_time chaining of the two parsers (rescan after run 11)'),
 'c3cb7f72': ('deliberate', 'deliberate (rescan after run 11): the two audio header class declarations after C2 (override/initializers) - one interface'),
 '77149298': ('deliberate', 'deliberate (rescan after run 11): the two audio stream class declarations after C2'),
 '3709c486': ('deliberate', 'deliberate (rescan after run 11): TTVideoStream and TTSubtitleStream class heads (cut(), headerList())'),
 'f4b7aa1d': ('deliberate', 'deliberate (rescan after run 11): TTVideoType and TTSubtitleType constructor/create shape, as cd99f92e'),
 '46dbdf5b': ('deliberate', 'deliberate (rescan after run 11): TTAudioType and TTVideoType declarations after C2 (explicit, override)'),
}
rescan_used = set()
base_fps = {r["fingerprint"] for r in csv.DictReader((SCAN / "candidates.tsv").open(), delimiter="\t")}
if RESCAN.exists():
    for r in csv.DictReader((RESCAN / "candidates.tsv").open(), delimiter="\t"):
        if r["status"] != "new" or not in_scope(r) or r["fingerprint"] in base_fps:
            continue
        key = r["fingerprint"][:8]
        if key not in RESCAN_RULINGS:
            raise SystemExit(f"rescan candidate without a ruling: {r['name']}")
        verdict, reason = RESCAN_RULINGS[key]
        store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
        rescan_used.add(key)
        stats["rescan " + verdict] += 1
    if set(RESCAN_RULINGS) - rescan_used:
        raise SystemExit(f"rescan rulings match no candidate: {set(RESCAN_RULINGS) - rescan_used}")

vd.save(OUT, store)
print(dict(stats))
