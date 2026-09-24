"""Add the 2026-09-24 run-7 verdicts (audit run 7, scope: the 21 source files
of docs/code-map/output-mux.md) to docs/code-audit/verdicts.tsv.

    python3 docs/code-audit/build-verdicts-2026-09-24-run7.py [scan-dir] [rescan-dir]

scan-dir defaults to CLAUDE_TMP/TTCut-ng/code-audit-run7 and must hold
candidates.tsv; rescan-dir defaults to code-audit-run7c, the rescan after the
batches. Both stay out of the repository on purpose.

Two subagents (sonnet: extern/, and data+gui+avstream+common) classified the
130 never-judged in-scope candidates against the map; the main session
verified the consolidates, spot-checked one deliberate per module and
corrected three (the TTAbortableTask funnel, the begin-stage skeleton, the
cutList parameter shadow). Rulings are keyed by fingerprint: the agents
partly paraphrased the candidate names. "done ... batch X" names the batch
of cleanup/code-audit-run7; "OPEN" is a consolidate not built in run 7."""
import csv, sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path.home() / ".claude/skills/code-audit/scripts"))
from code_audit import verdicts as vd

SCAN   = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run7")
RESCAN = Path(sys.argv[2] if len(sys.argv) > 2 else "/usr/local/src/CLAUDE_TMP/TTCut-ng/code-audit-run7c")
OUT = Path(__file__).resolve().parent / "verdicts.tsv"
D = "2026-09-24"

# fingerprint -> (verdict, reason); one entry per in-scope candidate of run 7.
# The trailing comment is the scanner name at scan time, for orientation only.
RULINGS = {
 '4140316265513daed01cf20073b80fd7c8bf54f0': ('consolidate',
  'done 2026-09-24 audit run 7 batch B2: TTNaluParser::findH264SlicePayload (frame indexer + PAFF branch) and the local firstVclPayload/containsVclNal replace the four VCL scans'),  # clone x3 (extern/ttmkvmergeprovider.cpp:591-594)
 '2127116d098fd1f8134ac6ed1ded43a0b0009fd1': ('consolidate',
  'done 2026-09-24 audit run 7 batch B2: allocMatroskaOutput() holds context + title for mux() and muxAudioOnly()'),  # clone x2 (extern/ttmkvmergeprovider.cpp:758-772)
 'c44d63adb8ef81fda83db63dd511f233134d4c23': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: one copyTracks helper in TTMuxListDataItem::copyFrom for the audio and subtitle lists'),  # clone x2 (extern/ttmuxlistdata.cpp:40-46)
 '88fa2cc21df8a40b0abbb5a343725d6056084c8a': ('consolidate',
  'done 2026-09-24 audit run 7 batch B2: TTNaluParser::findH264SlicePayload (frame indexer + PAFF branch) and the local firstVclPayload/containsVclNal replace the four VCL scans'),  # clone x2 (extern/ttmkvmergeprovider.cpp:591-596)
 'ec9e3f44ab17bfa104fe387cd088a4ecb9acc7ee': ('consolidate',
  'done 2026-09-24 audit run 7 batch B2: freeMatroskaOutput() closes and frees for both functions'),  # clone x2 (extern/ttmkvmergeprovider.cpp:772-777)
 '8734cbd01e95dee6662c95e4064fe514730c80a1': ('consolidate',
  'OPEN: avio_open of the output file still spelled out in mux() and muxAudioOnly(); small, left for the next work on the provider'),  # clone x2 (extern/ttmkvmergeprovider.cpp:805-813)
 'f170098f424d7dede0a3e0c68b2a1448cb140082': ('deliberate',
  '`decodeVdrName`\'s `QString result; result.reserve(...); for(...) {...}` loop shell coincidentally token-matches `legacyRemoveEp`\'s `QByteArray rbsp; rbsp.reserve(...); for(...) {...}` in the harness — same generic "reserve+append loop" shape, unrelated logic (VDR filename decoding vs H.264 RBSP escape removal). test_bitstream.cpp states its purpose explicitly: "Frozen copies of the implementations'),  # clone x2 (extern/ttmkvmergeprovider.cpp:93-101)
 '6be37f6f485350333c6ea33aab18fd890d37d6c9': ('consolidate',
  'OPEN: trailer + finish log in mux() and muxAudioOnly(); mux() adds the display-PTS self-check, left for the next work on the provider'),  # clone x2 (extern/ttmkvmergeprovider.cpp:1040-1048)
 'f90008ea3be10085cc4d105546fd35cbf1fb787b': ('deliberate',
  'Both are one-line-shaped `if (logMkvMux()) qDebug() << "..." << totalPacketsWritten << "pts=" << in.pkt->pts << "fc=" << in.frameCount << "sz=" << in.pkt->size ...` calls, but they log two different events (PAFF field-pair merge vs. single frame packet) with different trailing fields (`l2mfn=` vs `field=`+`l2mfn=`). Debug-only log lines with per-branch payload differences; extracting a shared logg'),  # clone x2 (extern/ttmkvmergeprovider.cpp:618-622)
 '0c0aa87e2fa131f8af5950cd42f6732a577a8650': ('deliberate',
  '`win1252ToUnicode`\'s `static const ushort map[32] = {...}` (Windows-1252 codepoints) coincidentally token-matches `acmodRuns`\'s `static const int kWords48k[38] = {...}` (AC3 frame-size table) — both are just "static const array literal" declarations with unrelated data. test_audiocutter_paths.cpp is a standalone diagnostic gate (documented at its file header as an "Output gate for TTAudioCutter::c'),  # clone x2 (extern/ttmkvmergeprovider.cpp:86-90)
 '51ee69d1785e37449284af81fd9cd2943f5d21ac': ('consolidate',
  'done 2026-09-24 audit run 7 batch B2: TTNaluParser::findH264SlicePayload (frame indexer + PAFF branch) and the local firstVclPayload/containsVclNal replace the four VCL scans'),  # clone x2 (extern/ttmkvmergeprovider.cpp:906-908)
 '73f924cbccfc733642d56208f8c649146be5a84b': ('consolidate',
  'OPEN, deferred by the user (run 7 batch B7): shared h:m:s.ms formatter for the chapter file and the goto-frame dialog'),  # clone x2 (extern/ttmkvmergeprovider.cpp:1244-1246)
 '026cc1afaa31d6fc19f486c0c1ce397c894aa8bc': ('deliberate',
  'docs/conventions.md, cpp/member_prefix rule: "Legacy headers without a prefix are renamed when the class is reworked ... not in bulk." ttmuxlistdata.h/.cpp carry the "Originally TTCut (c) 2003-2010 B. Altendorf / TriTime" heritage header and have not been reworked since; the un-prefixed members (`videoFileName`, `audioFileNames`, ...) are the documented legacy-until-reworked case, not a drift need'),  # cpp/member_prefix=none
 '899d153b33914199313df55d3d5394bf3fa81be9': ('documented',
  'docs/conventions.md, cpp/logging rule: "Files that still log through `log->` throughout stay internally consistent ... the scanner lists them as outliers and the verdict store carries them as deliberate." ttmplexprovider.cpp logs exclusively through `log->debugMsg/errorMsg/warningMsg/infoMsg` (TTMessageLogger), consistent file-wide.'),  # cpp/logging=ttlog
 'e032b0a053806340d3fb2dc9542885c8ef1978f9': ('documented',
  "same conventions.md rule as above; ttmuxlistdata.cpp's only logging (`TTMuxListData::print()`) goes through `log->infoMsg(...)` throughout, file-internally consistent."),  # cpp/logging=ttlog
 '6941d17fd9e52b7272776cfb846b194902939fae': ('documented',
  'docs/conventions.md, cpp/indent rule explicitly names this file: "The H.26x/Smart-Cut block (..., `ttmkvmergeprovider`, ...) uses four spaces consistently and stays that way ... The scanner lists these files as outliers against `2`; the verdict store carries them as `deliberate`."'),  # cpp/indent=4
 'cfe29747f9ced595772572cef9e66e701952ba79': ('documented',
  'same conventions.md rule/quote as candidate 16 — `ttmkvmergeprovider` (both .h and .cpp) is explicitly named as a four-space-indent exception file.'),  # cpp/indent=4
 'b8d3d62a10c492e44a44cf788d21bb7fc39c1c66': ('documented',
  'docs/conventions.md, cpp/class_prefix rule: "Exception: pure interfaces carry an `I` prefix (`IStatusReporter`, `IMuxProvider`, `ITTMpvBackend`)." IMuxProvider is named verbatim as an intended exception.'),  # cpp/class_prefix=none
 '7b82428dae4c29dbf3ce4efc208b4e685aaf5379': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # constParameterReference: Parameter 'activeLog2MaxFrameNum' c
 '346b07e52e84df1e03a09fc5e95d9edbdd10a3d1': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # cstyleCast: C-style pointer casting
 '8ca3f0ebbab898adbedc0b8b898aa836d595b08d': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # cstyleCast: C-style pointer casting
 '3bf293c03f3f972dd7feebb424e35053c4856cef': ('deliberate',
  'docs/code-map/output-mux.md pitfall: "A missing or unopenable input track does not fail the mux. addMediaInputs skips it with a qWarning and mux() still returns true." (also hypothesis H3). The function\'s branching is one early-continue per possible per-track failure (file missing, open failure, no matching stream, new-stream failure, params-copy failure) plus the explicit-language/`_xxx`-suffix f'),  # function 'addMediaInputs' has cognitive complexity of 39 (th
 '9170f9585a08f95c57b7daf5eb89104b09e204ce': ('deliberate',
  'docs/code-map/output-mux.md: "buildMpeg2DisplayOrder → mux() | Only when no display order was set and the codec is MPEG-2: display rank = GOP base + temporal_reference, per-GOP permutation check ... Tested by tools/diag/test_mpeg2order." The function is a single self-contained bitstream state machine (GOP flush, permutation self-check, chunk-overlap carry) with tightly-coupled local state (`gopBas'),  # function 'buildMpeg2DisplayOrder' has cognitive complexity o
 '4668cac70ac986ae055dd0810f1a128b3146de09': ('consolidate',
  'done 2026-09-24 audit run 7 batches B2+A8: per-packet video step moved to prepareEsVideoPacket(), the interleave loop to writeInterleaved(); size/complexity re-measured by the rescan'),  # function 'mux' exceeds recommended size/complexity threshold
 '98c694d4410016f9a81fc3e274f8ea9391ba962e': ('consolidate',
  'done 2026-09-24 audit run 7 batches B2+A8: per-packet video step moved to prepareEsVideoPacket(), the interleave loop to writeInterleaved(); size/complexity re-measured by the rescan'),  # function 'mux' has cognitive complexity of 161 (threshold 25
 '33677e78553effd516c66371fd29f58d969781fe': ('consolidate',
  'OPEN: setupVideoInput complexity (display-order block) - low priority, one call site'),  # function 'setupVideoInput' has cognitive complexity of 26 (t
 'dd27e28dc933e44c3c2782db70c577fccb85fbbc': ('deliberate',
  "cppcheck false positive — `isFieldPacket` (declared false at 904) is set to true by reference at line 935 via `TTNaluParser::parseH264SliceFieldInfo(d + nalStart, sz - nalStart, activeLog2MaxFrameNum, frameNum, isFieldPacket, isBottom)`, a function defined in a different translation unit (avstream/ttnaluparser.cpp) that cppcheck's single-TU analysis cannot see into. Not a real defect."),  # knownConditionTrueFalse: Condition 'isFieldPacket' is always
 '8a7183b8b19ec025860cf5a909749c2f056443e3': ('consolidate',
  'done 2026-09-24 audit run 7 batch B2: the tautological sz>=1 clauses went with the VCL-scan helpers'),  # knownConditionTrueFalse: Condition 'sz>=1' is always true
 '62057f548f1ec5d4344b72a810dec2d5d74ef17d': ('consolidate',
  'done 2026-09-24 audit run 7 batch B2: the tautological sz>=1 clauses went with the VCL-scan helpers'),  # knownConditionTrueFalse: Condition 'sz>=1' is always true
 '4c4f6f327ded25129a2e365340b56d31acb839a7': ('consolidate',
  'done 2026-09-24 audit run 7 batch B2: made static (addAudioInputs became a member again in A3, it now records dropped inputs)'),  # method 'addAudioInputs' can be made static
 'a2a1ad69990290278131316c21d8f4594e344a3d': ('consolidate',
  'done 2026-09-24 audit run 7 batch B2: made static (addAudioInputs became a member again in A3, it now records dropped inputs)'),  # method 'assignEsTimestamps' can be made static
 'df88bbb67282725ea6a46944a2dbcd1b7d7e2eba': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # method 'createOutputFilePath' can be made static
 '2fd1ec643f57dbb39540bfbaf9513125fbd54796': ('deliberate',
  'ttmkvmergeprovider.h:198-199, directly above this and readNextPacket\'s declarations: "Per-input read helper + normalized PTS calc (used by interleaved write loop). Not static so they can take MuxInput& without exposing the struct." Explicit, documented design choice, even though the reasoning (static private members can equally take the private nested `MuxInput` type) is debatable — the decision i'),  # method 'getNormalizedPts' can be made static
 '53331403d70cc8e1c15a482abba75b85408fce0c': ('deliberate',
  'same documented comment as candidate 33 (ttmkvmergeprovider.h:198-199), which covers both `readNextPacket` and `getNormalizedPts` together.'),  # method 'readNextPacket' can be made static
 'b4d41508c24952e508a3ef659c937c543a2cafcf': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # missingOverride: The destructor '~TTMplexProvider' overrides
 'a77b21e7eb9d218ac62acdfdbb7594f73c48485e': ('consolidate',
  'done 2026-09-24 audit run 7 batch B6: moot - the IMuxProvider interface was removed (one implementer, never used through it)'),  # missingOverride: The function 'mplexPart' overrides a functi
 '3cc4ae0c1e1890ca38fc2e0583f4f32bd8adfd80': ('consolidate',
  'done 2026-09-24 audit run 7 batch B6: moot - the IMuxProvider interface was removed (one implementer, never used through it)'),  # missingOverride: The function 'writeMuxScript' overrides a f
 '51b91f7ff1ed3c19ae88dbdc11fbe0f498cc9571': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # noExplicitConstructor: Class 'TTMplexProvider' has a constru
 'ddfc30bbab818e895b90578c33e4814b47ce08b7': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # passedByValue: Function parameter 'audio' should be passed b
 '1968f9fe2e6600c863ca0be068f145e747482cb4': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # passedByValue: Function parameter 'audio' should be passed b
 '39bd83907ec0cd2e07c17eb26c3349563d55d4be': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # passedByValue: Function parameter 'audioFileName' should be 
 '3c80d314e93592570a0ebcb6dbb20ea77e20996d': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # passedByValue: Function parameter 'subtitle' should be passe
 '372f51e3af663d29fcea93dd38afbf13806ecbeb': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # passedByValue: Function parameter 'subtitleFileName' should 
 '1e33060204aa7d46cda482fc5975d0ff32dd1a69': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # passedByValue: Function parameter 'video' should be passed b
 'd0d33ff4f3c9a4aa5db3fe9fc1897549b3887e0b': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # passedByValue: Function parameter 'video' should be passed b
 '4a82be21bb29c4740f4eaa1819821293a18907fe': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # passedByValue: Function parameter 'videoFileName' should be 
 'e116dd91b03b80b4276403bbbb076f888271bd02': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # returnByReference: Function 'audioFilePathsAt()' should retu
 'a677e88764c469c0f962087ba5590874bc8f7047': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # returnByReference: Function 'getAudioLanguages()' should ret
 'd82c7efb906a013a40146491a6be834339dbaacb': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # returnByReference: Function 'getAudioNames()' should return 
 '8c5aadd52d7ff3af67eef05107b62f15c7384460': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # returnByReference: Function 'getSubtitleLanguages()' should 
 '8f8c79ec86466c8b327c96ed1e11e2f64511479f': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # returnByReference: Function 'getSubtitleNames()' should retu
 'a6019c86c05bf02a6f86139fa1213d74107b71c0': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # returnByReference: Function 'getVideoName()' should return m
 '3aa53521d2d1ab5752c5fe597dc596567b4d291a': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # returnByReference: Function 'lastError()' should return memb
 'd0c724553cbca1a03a56d2130437dafbb1abd219': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # returnByReference: Function 'videoFilePathAt()' should retur
 '9482c6d7d3d1d3eb9715497267fc7341e60c2dec': ('deliberate',
  "`setVideoName(QString videoFileName)`'s parameter shadows the private member `videoFileName`; the body already disambiguates correctly with `this->videoFileName = videoFileName;` (ttmuxlistdata.cpp:84), the same explicit-`this->` pattern used throughout `copyFrom()`. Fixing the shadow at its root (renaming the member to `mVideoFileName`) is exactly the member_prefix rework covered by candidate 13 "),  # shadowMember: Argument 'videoFileName' shadows outer member
 '395cb0054452335b5d9474f7952239843aca6f4c': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # uninitMemberVar: Member variable 'TTMplexProvider::mCurrentM
 '4470826f4262db9e689dc936a57de7f45754b5c4': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # useInitializationList: Variable 'audioFileNames' is assigned
 'e2ab76b8a622ba9a055fab0b14297393888d78fa': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # useInitializationList: Variable 'audioFileNames' is assigned
 '43d7531525d29efa7087c1580cc3db7085deaced': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # useInitializationList: Variable 'subtitleFileNames' is assig
 'd9648cdb3604bf557245d85e33b910baf94f4613': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # useInitializationList: Variable 'videoFileName' is assigned 
 'cf582edad28c5474a48533e9012011daed1beb6d': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: const references, const-ref getters and initializer lists in TTMuxListDataItem/TTMuxListData/TTMplexProvider'),  # useInitializationList: Variable 'videoFileName' is assigned 
 '76de215fbc48de06942bcb820c2fa2afcbe77c54': ('deliberate',
  "`for (int r : gopRefs) order.append(gopBase + r);` inside `buildMpeg2DisplayOrder`'s `flushGop` lambda. The codebase (TTCut heritage plus the H.26x/Smart-Cut additions) consistently uses range-for/index loops rather than `<algorithm>` transforms throughout avstream/ and extern/ — a std::transform here (needing a lambda plus a QVector output iterator) would not read more clearly in this idiom and n"),  # useStlAlgorithm: Consider using std::transform algorithm ins
 '6c5d7bcebfd245e79ca4d7e711620fdde0ba9ac5': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # variableScope: The scope of the variable 'line' can be reduc
 '713d20867920452ff2d1ac2e143b80897e093199': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # variableScope: The scope of the variable 'mplexCmd' can be r
 '303e95f2886a12a34fc39db29d904b954281308c': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # variableScope: The scope of the variable 'update' can be red
 '47cb4a2676c76002216078b9a5b80b41572e5b62': ('consolidate',
  'OPEN, deferred by the user (run 7 batch B7): pipe-record parsing in TTESInfo'),  # clone x2 (avstream/ttesinfo.cpp:257-269)
 '09f0008618870b2f12b9023db0d0a2fb1194c908': ('consolidate',
  'OPEN, deferred by the user (run 7 batch B7): logIfLoaded() for the four TTESInfo warning blocks'),  # clone x3 (avstream/ttesinfo.cpp:311-319)
 'c98519701db4f2a2a6ddce62d892bd8df3c7cecb': ('consolidate',
  'OPEN, deferred by the user (run 7 batch B7): same TTESInfo cluster'),  # clone x2 (avstream/ttesinfo.cpp:320-336)
 '449212baa28f596fcdc22d001624e5ea2661a1f3': ('deliberate',
  '`mVideoWidth = values.value("width","0").toInt(); mVideoHeight = ...; mStartPts = ...toDouble(); mFillerStripped = (...=="true");` (parseVideoSection) vs `mFirstVideoPts = ...; mFirstAudioPts = ...; mAvOffsetMs = ...; mHasTimingInfo = true;` (parseTimingSection) — plain sequential scalar-field assignment from a keyed QMap, each line naming a different field/type/default. This is the established id'),  # clone x2 (avstream/ttesinfo.cpp:214-217)
 'bcf52cf941f0f5b1c428075a86da6fbab0beb13f': ('deliberate',
  '`TTESInfo::audioTrack(int index)` (avstream/ttesinfo.cpp:440-445, `if (index>=0 && index<mAudioTracks.size()) return mAudioTracks[index]; return TTAudioTrackInfo();`) vs `TTNaluParser::gopAt(int index)` (avstream/ttnaluparser.cpp:1158-1164, identical shape on `mGops`/`TTGopInfo`). Coincidental bounds-checked-getter idiom match between two unrelated classes/types with no shared base — same class of'),  # clone x2 (avstream/ttesinfo.cpp:434-451)
 '53851f24b28975e0683f2c2025b1eef8eaddd458': ('consolidate',
  'OPEN, deferred by the user (run 7 batch B7): same TTESInfo cluster'),  # clone x2 (avstream/ttesinfo.cpp:306-310)
 '7cd7dff4169070cb5a91828a8837be91b0a4f1bb': ('consolidate',
  'done 2026-09-24 audit run 7 batch B2: TTNaluParser::findH264SlicePayload (frame indexer + PAFF branch) and the local firstVclPayload/containsVclNal replace the four VCL scans'),  # clone x2 (avstream/ttframeindexer.cpp:186-188)
 '0e0d2d94397db9ec04a8dc0533895b5d707641d6': ('deliberate',
  'function bodies are 4-space indented throughout (checked avstream/ttesinfo.cpp:255-280 and elsewhere), no mixing with 2-space. Not on docs/conventions.md\'s named H.26x/Smart-Cut exception list, but matches the same "internally consistent whole-file legacy indent" pattern verdicts.tsv already judges deliberate independently of that list (e.g. the common/ttmessagelogger.cpp row: "uses 4-space indent'),  # cpp/indent=4
 'fc4efbef53561ff46a6657818d0f152ebb5b2cad': ('consolidate',
  'OPEN, deferred by the user (run 7 batch B7): parseWarningsSection complexity, same TTESInfo cluster'),  # function 'parseWarningsSection' has cognitive complexity of 
 '4570fe397de0467297cd754fccfb5b5666be2b78': ('consolidate',
  'OPEN, deferred by the user (run 7 batch B7): array-size idiom in ttencodernames.h'),  # clone x2 (common/ttencodernames.h:22-29)
 'ccfaad27a80a7ae2cfdc99e06152053d42202d4f': ('consolidate',
  'OPEN, deferred by the user (run 7 batch B7): same ttencodernames.h idiom'),  # clone x2 (common/ttencodernames.h:21-25)
 'd41bc03f6905a913222db8116b5145e8c1536910': ('deliberate',
  'the file\'s one logging call (line 29, `ttRemoveElementaryStreams`) goes through `log->debugMsg(...)`, no qDebug present — internally consistent, matches docs/conventions.md `cpp/logging` rule verbatim: "Files that still log through `log->` throughout stay internally consistent ... the scanner lists them as outliers and the verdict store carries them as deliberate."'),  # cpp/logging=ttlog
 '861d5a0c21c4ca31a26b86a6716a9a13bc6b255f': ('deliberate',
  'the whole 51-line header (array bodies included) is consistently 4-space indented, no 2-space mixing. Same "internally consistent whole-file legacy/local indent" pattern as the ttesinfo.cpp indent verdict above, not the "mixed 4/8/12" pattern verdicts.tsv marks consolidate.'),  # cpp/indent=4
 '70759b3539aa6330aebf6c587d2a119171c61cc7': ('deliberate',
  'ttavdata.cpp sites are `disconnect(mpThreadTaskPool, &TTThreadTaskPool::exit, this, &TTAVData::onXFinished); disconnect(mpThreadTaskPool, &TTThreadTaskPool::aborted, this, &TTAVData::onXAborted);` pairs (endAbortedProjectLoad/onCutAborted); ttcutmainwindow.cpp sites are blocks of unrelated `connect(sender, &Class::signal, receiver, &Class::slot);` wiring lines. All four match only on the generic `'),  # clone x4 (data/ttavdata.cpp:1331-1341)
 '4c9d545c5ca4b0cf44701801ea68e46647a38917': ('consolidate',
  'done 2026-09-24 audit run 7 batch B4: TTAbortableTask::abortIfEngineAborted() at all five engine-failure sites (log line and failure text stay at the call site)'),  # clone x4 (data/ttaudioonlycuttask.cpp:191-196)
 '91ead0eef02b90066805d697912bd44a6653ab1d': ('consolidate',
  'done 2026-09-24 audit run 7 batch B4: TTAbortableTask::abortIfEngineAborted() at all five engine-failure sites (log line and failure text stay at the call site)'),  # clone x2 (data/tth26xcuttask.cpp:144-158)
 'fc5da6bf4273233eab3ab962a779634152bc8a61': ('consolidate',
  'done 2026-09-24 audit run 7 batch B4: TTAbortableTask::abortIfEngineAborted() at all five engine-failure sites (log line and failure text stay at the call site)'),  # clone x3 (data/ttaudioonlycuttask.cpp:191-196)
 '01f5393fd4f3376086d93cafdc02528f0cb750a1': ('consolidate',
  "done 2026-09-24 audit run 7 batch B4: TTAbortableTask::audioTrackProgress() - the 'Audio progress lambda pair' of progress-reporting.md; the MPEG-2 branch in TTAVData emits differently and stays"),  # clone x2 (data/ttaudioonlycuttask.cpp:121-132)
 'd8804b090e0e286e3a5c8f2417db2f67a11183d1': ('consolidate',
  'OPEN: preview audio-cut skeleton shared by TTCutPreviewTask and both ttRebuild*PreviewClip - preview pipeline, outside the mux batches of run 7'),  # clone x2 (data/ttcutpreviewtask.cpp:569-575)
 'a20d1bcbfcbda9ad35f82abce45a41e73b33f76c': ('consolidate',
  'OPEN: same preview audio-cut skeleton as the entry above'),  # clone x2 (data/ttpreviewclip.cpp:205-215)
 '126f786a4e29af022132bfc3c2e71d89646c4545': ('deliberate',
  'both headers repeat the standard TTAbortableTask-subclass class skeleton (ctor, `init(Params)`, result accessors, `protected: cleanUp()/operation() override`, `public slots: onUserAbort() override`, private `Params mParams; QString mError; <engine members>`). TTAbortableTask (per progress-reporting.md, "Done 2026-09-05") already holds the actually-shared state (cancel flag, created-files list, abo'),  # clone x2 (data/ttaudioonlycuttask.h:40-79)
 '3190bc41768161fe2a91cb40a2b6721b5e0d08e9': ('deliberate',
  'constructor/`init()`/`onUserAbort()` bodies mirror each other, but `onUserAbort()` genuinely differs in content (TTAudioOnlyCutTask aborts one engine, `mMkvProvider`; TTH26xCutTask aborts two, `mSmartCut` and `mMkvProvider`) — the duplication is the surrounding 2-3 line pass-through shape only, inherent to wrapping different engine sets on top of the already-shared TTAbortableTask base.'),  # clone x2 (data/ttaudioonlycuttask.cpp:45-70)
 '5be2aae118720acdb28aa1c1b8500e2f9caa43b1': ('deliberate',
  'both are "log at the right level, then set the matching outcome-string member" pairs (partial-failure error block vs AOF_OriginalES success block) — the established two-audience reporting convention across this whole task family (log line for diagnostics, member string for the GUI Exit bracket), not shared logic; message text, log level and target member differ at each site.'),  # clone x2 (data/ttaudioonlycuttask.cpp:157-161)
 '4c51d2b12e2c152ec612f7bbeb2143514d58e555': ('deliberate',
  '`if (avData == nullptr) return false; const TTPreviewSource src = ttResolvePreviewSource(clipCutList); if (!src.isValid()) return false;` is the shared 3-line entry guard of ttRebuildMpeg2PreviewClip and ttRebuildSmartCutPreviewClip. Trivial null/validity guard idiom, not real logic; an out-parameter helper would not reduce complexity meaningfully for 2 call sites.'),  # clone x2 (data/ttpreviewclip.cpp:174-185)
 '6b6721ed7ed02613fbecede094bae0a0beb509e8': ('deliberate',
  "the two operation() funnels are 6 lines each and cross-reference each other on purpose; the classifier's claim that TTMuxTask lacks the safety net is wrong - TTMuxTask cleans up in its cleanUp() override guarded by mOperationDone, a different, documented mechanism"),  # clone x2 (data/ttaudioonlycuttask.cpp:70-93)
 'a09dcdab668032e876d1f3d8ebfa84a728dbe746': ('deliberate',
  "both are QMessageBox construction with `(Warning, title, msg, NoButton, TTCut::mainWindow)` followed by `addButton(...)` calls — standard Qt custom-button dialog boilerplate. The two dialogs genuinely differ (simple 2-button confirm/cancel at 1570-1574 vs a 3-button dialog with a disconnect/reconnect for the copy button at 1622-1636); Qt's QMessageBox API offers no shorter construction, and a shar"),  # clone x2 (data/ttavdata.cpp:1569-1573)
 '44f7a9c99c8dc35500e4b11afee5e6d4ce761a16': ('consolidate',
  'OPEN: preview clip count computed in data/ttpreviewclip.cpp and gui/ttcutpreview.cpp'),  # clone x2 (data/ttpreviewclip.cpp:101-107)
 'c70517133efee81aaf94dc33b9b6e6a8c3e4dc04': ('deliberate',
  'reportStage + reportStep + mCreatedFiles.append with a different stage, text and file at each site; a helper would take three arguments to save three lines'),  # clone x2 (data/ttaudioonlycuttask.cpp:183-191)
 'c13e6646ae8923e95c0de9cd4aad49aff5575d64': ('consolidate',
  "OPEN: findIncompatibleVideos filters video items twice - project-lifecycle code, outside run 7's batches"),  # clone x2 (data/ttavdata.cpp:1355-1357)
 '5ea1cbb0afd499534fff60d2be37cc8de56591d4': ('deliberate',
  '`CutBurstInfo bout = detectCutOutBurst(item); if (bout.present) warnings << tr("...end...");` vs `CutBurstInfo bin = detectCutInBurst(item); if (bin.present) warnings << tr("...start...");` — trivial 2-line "call detector, conditionally append warning" idiom repeated once for cut-out and once for cut-in, with different detector functions and different message text each time; not worth a helper for'),  # clone x2 (data/ttavdata.cpp:1527-1529)
 'ec093e81190243ad94f85d6d8cdbcaa7b29aa298': ('consolidate',
  "OPEN: preview Smart Cut failure logging repeated in TTCutPreviewTask and ttRebuildSmartCutPreviewClip - preview pipeline, outside run 7's batches"),  # clone x2 (data/ttcutpreviewtask.cpp:476-479)
 '795fec1e3cfd67ad7d9fda5b57d024c58b9813be': ('consolidate',
  'done 2026-09-24 audit run 7 batch B4: TTAbortableTask::forwardProgressOf() for both engines of TTH26xCutTask and the MKA mux'),  # clone x2 (data/tth26xcuttask.cpp:131-144)
 '3accd49c837f26ac7e864ecb4edaf544ed26c097': ('consolidate',
  'done 2026-09-24 audit run 7 batch B4: local cutFilePath() in TTH26xCutTask::runCut for the audio and subtitle names'),  # clone x2 (data/tth26xcuttask.cpp:255-258)
 'dff6cf0b72a412df89b053e69050502597f10e8e': ('documented',
  'docs/conventions.md `cpp/header_comment` rule: "`//!` Doxygen line comments before declarations and definitions. The older `/*!`, `/**` and `/* //// */` banner forms remain in place." data/ttpreviewclip.cpp\'s `/**` blocks (e.g. lines 94-95, 176-178, 231-233) are exactly this documented older banner form kept in place.'),  # cpp/header_comment=/**
 '2b1719922012f96f2adc362f66cd8a4cd8262d47': ('deliberate',
  'the "none"-prefix finding is on `TTMuxTaskParams` (data/ttmuxtask.h:31-51: `mkvOutput`, `videoFile`, `audioFiles`, ... unprefixed), a plain value-bundle DTO copied on the GUI thread before the task starts (per its own doxygen comment, "same arrangement as TTH26xCutParams"); the class itself, `TTMuxTask`, correctly uses `m`-prefixed members throughout (`mParams`, `mError`, `mOperationDone`, `mMkvPr'),  # cpp/member_prefix=none
 'f439966f71d9e47441596091bb9e038d4f577705': ('deliberate',
  "every logging call in this file goes through `log->infoMsg/errorMsg/warningMsg(...)` (lines 116,142,157,175,195,202,212) — no qDebug present, internally consistent throughout. Matches docs/conventions.md's documented `cpp/logging` exception verbatim."),  # cpp/logging=ttlog
 'bca08ba2dca707fb9e21b07156f6725cf2e97e12': ('deliberate',
  'nearly every call goes through `log->...Msg(...)`; one stray `qDebug() << "doH264Cut: Injected display-order map (" ...` at line 166 (guarded by `TTSettings::instance()->logCutPipeline()`). docs/conventions.md\'s `cpp/logging` rule explicitly covers this case: "a stray qDebug in such a file goes through log->, not the other way round" — i.e. the file\'s own convention (log->) stays the standard, and'),  # cpp/logging=ttlog
 '39ab78376afeeced76799bb8c3fa3ddd1cb3cd3a': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # constParameterPointer: Either there is a missing override/fi
 '06096b331ba4cf038f35a4c679f8ae1b75b637a9': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # constParameterPointer: Either there is a missing override/fi
 'ae31a7b87dd6e2f5047a4162ad19c503db5effde': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # constParameterPointer: Either there is a missing override/fi
 '63400b5125889bb4733bdea233452c32c69183bc': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # constVariablePointer: Variable 'discardButton' can be declar
 '90d1a262bd5430160516028783a3b76ba752f1c4': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # constVariablePointer: Variable 'itemB' can be declared as po
 '27cae4218ea3dfafb9421e77cf3b2637e0b8738b': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # constVariablePointer: Variable 'vStream' can be declared as 
 '2417b9ad1d556dfe02b73bcb3f7a2496b47f1381': ('consolidate',
  'OPEN (partly done): run 7 batch B4 moved the engine-failure checks, progress forwarding and file naming out of TTH26xCutTask::runCut; the rescan re-measures what is left'),  # function 'runCut' exceeds recommended size/complexity thresh
 '99e69ec610a0bc80b51b136fa9dd0d95dccc5910': ('consolidate',
  'OPEN (partly done): same as the size finding of runCut'),  # function 'runCut' has cognitive complexity of 65 (threshold 
 '8aa3251e7616455b7dd468899ee62ecd50adbdd4': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # noExplicitConstructor: Class 'TTMuxTask' has a constructor w
 '91dd9f3d3802d589176bbdd64abfe51daa4f6155': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # returnByReference: Function 'createdFiles()' should return m
 'a3268b880991185700550dccd745d7f8d8070afd': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # returnByReference: Function 'exitMessage()' should return me
 '84c088a6b4a8b83104627670527fccd42b1a59f8': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # returnByReference: Function 'finalOutput()' should return me
 'bb0e550883ef5314cca66210918c68874ab88c8e': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # returnByReference: Function 'lastError()' should return memb
 'c982e53bf580e18e3524fb342810892a5bab7fd3': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # returnByReference: Function 'lastError()' should return memb
 '518056e3dec3a5255475ece8317975e1bb611f91': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # returnByReference: Function 'mkvOutput()' should return memb
 'fb62f088a7215e6484f9af15d01a632b865195f2': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # returnByReference: Function 'seamNotes()' should return memb
 'de3e8b8d310ecc313517bcfb680c4e39150065a4': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # returnByReference: Function 'unrewrittenSourceFrames()' shou
 'bd91e1eac32f62decbd73f92f2806d669f9e4c92': ('deliberate',
  "TTCutList* cutList is the parameter name of every cut entry point in TTAVData (onDoCut, doH264Cut, doAudioOnlyCut, buildVideoKeepList); renaming one of them breaks the file's consistency for a shadow that has no reader"),  # shadowFunction: Argument 'cutList' shadows outer function
 '4afae631fe39a9563d6a480a838f3ff783b9b257': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # shadowFunction: Argument 'exitMessage' shadows outer functio
 '77fd83ff57cdb1063b77fa26a3628301efa57d2b': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # shadowFunction: Argument 'params' shadows outer function
 '7ace18a1441c652fce23e7e9943712b44d81393f': ('deliberate',
  '`for (const auto& region : regions) { errorPoints.append(TTStreamPoint(region.frame, StreamPointType::Error, QString("Decode Error (%1 errors)").arg(region.errorCount))); }` — a range-for building a QList via `append()`, the pervasive idiom throughout this codebase (Qt containers, not STL algorithms with back_inserter); no doc mandates STL algorithms, and this codebase-wide convention isn\'t in sco'),  # useStlAlgorithm: Consider using std::transform algorithm ins
 'f5921ad1081562ce75266c7ad52c0d9f0d429566': ('deliberate',
  '`for (const TTIndexCluster& c : ttClusterIndices(mAudioGapIndices, gapFrames)) ranges.append({c.first, c.last});` — same range-for + `append()` idiom as above, same reasoning.'),  # useStlAlgorithm: Consider using std::transform algorithm ins
 '7cf698f4e25afcc9f23a0fb5af540d7a17cd2d6d': ('consolidate',
  "OPEN: shared tail of two TTCurrentFrame cut-in slots - playback widget, outside run 7's batches"),  # clone x2 (gui/ttcurrentframe.cpp:370-386)
 '98f5b4919a4c05fe20ec1452265580e3ea3aef53': ('deliberate',
  "both are `setTabData()` bodies mirroring `TTSettings::instance()` getters into this page's own widgets (`cbX->setChecked(s->getX()); sbY->setValue(s->getY()); ...`) — the standard per-settings-page widget-binding shape shared by every `TTCutSettingsXxx` class; each line names a different widget/getter pair specific to that page's fields. Consolidating would need a generic field-binding framework ("),  # clone x2 (gui/ttcutsettingsmuxer.cpp:76-85)
 '4835a2fc65ff153b06c125a879896255eacb29b7': ('deliberate',
  'same finding as "clone x2 (gui/ttcutsettingsmuxer.cpp:76-85)" above — `saveTabData()`/`setTabData()` widget-to-settings mirroring, this time against gui/ttcutsettingssearch.cpp; same per-page boilerplate shape, different fields.'),  # clone x2 (gui/ttcutsettingsmuxer.cpp:97-107)
 'c35ad2e18c6537a61ff275cb4df3f90ce7f8a460': ('deliberate',
  'both are "Use theme icons with Qt standard icon fallback" blocks calling `ttThemedIcon()`/`ttThemeIcon()` per button — the icon-lookup helper itself is already the shared consolidation point; the remaining lines just differ in which button gets which icon name, inherent per-widget content, not duplicated logic.'),  # clone x2 (gui/ttcurrentframe.cpp:110-119)
 '97c392870ee7e4d2bed6804ff9eb8bf06cc1a645': ('consolidate',
  'done 2026-09-24 audit run 7 batch B5: cppcheck fix applied as suggested'),  # constVariablePointer: Variable 'audioStream' can be declared
 '3b0e74c91490bb4ffeb03400d8cc4796bd41cd81': ('deliberate',
  '`beginPlayerLoad()` guards `mAVItem` at line 701 (`if (mPlayer && mAVItem && mAVItem->audioCount() > 0)`) but then dereferences `mAVItem->subtitleCount()` unconditionally at line 713. Traced both call sites: `onPlayVideo()` (line 670) already dereferences `mAVItem->audioCount()` unconditionally at line 665, before calling `beginPlayerLoad()`, so `mAVItem` is already established non-null there; `st'),  # nullPointerRedundantCheck: Either the condition 'mAVItem' is
}

# Earlier consolidates (run 3) that run 7 closed.
OLD_DONE = {
 '39d4751288fb89892f9dbdb29d85f236e68fa790': ('clone', 'done 2026-09-24 audit run 7 batch B1: TTMkvVideoOptions + setVideoOptions(); TTMuxTask and TTPlaybackMuxTask no longer set the provider field by field'),
 '648a303d0c4f48946d9f664035e5863f984c0f28': ('clone', 'done 2026-09-24 audit run 7 batch B1: TTMkvMergeProvider::videoOptionsFor() builds the options in onCutFinished and TTCurrentFrame::buildPlaybackMuxParams'),
}

# The rescan after the batches (code-audit-run7c): each new in-scope row judged.
RESCAN_RULINGS = {
 'd8d036e6829d7b8792a9546e875dfcc72be9ca1d': ('consolidate',
  'OPEN, deferred by the user (run 7 batch B7): the array-size idiom, now also in the kMpeg2MuxTargetFormats static_assert of run 7 batch A2'),  # clone x2 (common/ttencodernames.h:21-28)
 '47c175caa09d2c8b38a3df2bbf17876838dc19fb': ('deliberate',
  'the two Smart Cut failure blocks of runCut after run 7 batch B4: abortIfEngineAborted is shared, what remains is a log line and fail() with different texts'),  # clone x2 (data/tth26xcuttask.cpp:135-148)
 '96a6f98af88e1fe6a7791cac745e033190b443f9': ('deliberate',
  'declaration in ttavdata.h against the definition of the same cutSubtitleTracks overload - not a copy'),  # clone x2 (data/ttavdata.cpp:3132-3138)
 'f2e38e15e0798ba00cac72d974b40facb074b6e9': ('deliberate',
  'declaration in ttavdata.h against the definition of the other cutSubtitleTracks overload - not a copy'),  # clone x2 (data/ttavdata.cpp:3114-3119)
 '39fd9c86e3e6919a8f003225636bfca0cd44e1c4': ('deliberate',
  'rescan: the two QMessageBox constructions judged deliberate in run 7, re-fingerprinted because the lines moved'),  # clone x2 (data/ttavdata.cpp:1565-1568)
 'adc104b83c7abca94208bb756b9bff67b9a53b8a': ('deliberate',
  'rescan: the two cut-task value bundles judged deliberate in run 7 (DTO shape mirrored on purpose), re-fingerprinted after TTH26xCutParams gained TTMkvVideoOptions'),  # clone x2 (data/ttaudioonlycuttask.h:39-52)
 '814494809c2ee9fd0553c5ecfa47b7e519e92f7b': ('consolidate',
  'done 2026-09-24 audit run 7 rescan: const reference, possible since batch B5 made the TTMuxListDataItem getters const'),  # constVariableReference: Variable 'muxItem' can be declared a
 '99e6535e20dee8edfbfebcc2fe58a911d6ca70a6': ('consolidate',
  'OPEN: rescan of the run-7 entry "avio_open of the output file in mux() and muxAudioOnly()", re-fingerprinted'),  # clone x2 (extern/ttmkvmergeprovider.cpp:1107-1117)
 '0acae225f7c39149e9f3ef90b499091bdfe4c7bc': ('deliberate',
  'codec-parameter copy for the video (a failure fails the mux via setError) and for audio/subtitle inputs (a failure drops the input); different error channels'),  # clone x2 (extern/ttmkvmergeprovider.cpp:424-428)
 '94a60ab5a4495990e467c3c2635c0ca4529ad205': ('deliberate',
  'two logMkvMux debug lines (merged PAFF pair, frame packet) with different fields'),  # clone x2 (extern/ttmkvmergeprovider.cpp:698-702)
 'c4891c2b1d3c24124055dfaaf59cab2c7950ab12': ('deliberate',
  'the interleave loop moved out of mux() in run 7 (mux: 161 -> 46); one loop with abort poll, input selection, video step, offset, write, progress - each a helper call or a two-line block'),  # function 'writeInterleaved' has cognitive complexity of 36 (
 '5f3ddd9bd6c434949ff4c3dba92745b8c35d7f2d': ('deliberate',
  'one-line byte sums in the MKA progress lambda; std::accumulate with a projection lambda reads no better (same ruling as the run-7 useStlAlgorithm entries)'),  # useStlAlgorithm: Consider using std::accumulate algorithm in
 '1e4185e36dad4f7e170e71053984a27b6805b978': ('deliberate',
  'one-line byte sums in the MKA progress lambda; std::accumulate with a projection lambda reads no better (same ruling as the run-7 useStlAlgorithm entries)'),  # useStlAlgorithm: Consider using std::accumulate algorithm in
 '47ba56bc837291325a9aacbefa369ce78330a6a7': ('deliberate',
  'construction of the burst row and the aspect row of the preview dialog - per-widget setup, different widgets and slots'),  # clone x2 (gui/ttcutpreview.cpp:86-97)
 '5da14295cffc1c5426375d3d370c07404c97a403': ('deliberate',
  'two logUI debug lines (burst shift, aspect jump)'),  # clone x2 (gui/ttcutpreview.cpp:760-766)
 'c9779dbad1882ed0726265a775a5f9db7b350e17': ('deliberate',
  'layout rows for the burst and the aspect message - per-widget setup'),  # clone x2 (gui/ttcutpreview.cpp:128-132)
 '7aa3fcedf1bd248cf7980072d0151c600dfda826': ('deliberate',
  'cut-out and cut-in variant of the aspect message: different text, sign and icon'),  # clone x2 (gui/ttcutpreview.cpp:590-594)
 'ad526974d196c1e0cc56eaaa06230f2f63d792d1': ('consolidate',
  'done 2026-09-24 audit run 7 rescan: pointer to const'),  # constVariablePointer: Variable 'aStream' can be declared as 
 'd9cea437bc9d790403684a0787ba1d8c546f057c': ('consolidate',
  'done 2026-09-24 audit run 7 rescan: pointer to const'),  # constVariablePointer: Variable 'avItem' can be declared as p
}

RESCAN_NOTE = ("rescan after the run-7 batches: same class as a run-7 ruling, "
               "re-fingerprinted because the lines moved or the site count changed")

def scope_files():
    lines = (Path(__file__).resolve().parents[2] / "docs/code-map/output-mux.md").read_text().split("\n")
    out, inside = [], False
    for ln in lines:
        if ln.startswith("sources:"): inside = True; continue
        if inside:
            if ln.startswith("  - "): out.append(ln[4:].strip())
            elif ln.strip() == "---": break
    return out

# The run was scoped to the map's sources as they stood when it started (the
# map gained two files during the run); the rescan uses the current list.
RUN_SCOPE = ['extern/ttmkvmergeprovider.h', 'extern/ttmkvmergeprovider.cpp', 'extern/ttmplexprovider.h', 'extern/ttmplexprovider.cpp', 'extern/imuxprovider.h', 'extern/ttmuxlistdata.h', 'extern/ttmuxlistdata.cpp', 'data/ttmuxtask.h', 'data/ttmuxtask.cpp', 'data/tth26xcuttask.h', 'data/tth26xcuttask.cpp', 'data/ttaudioonlycuttask.cpp', 'data/ttpreviewclip.cpp', 'data/ttcutpreviewtask.cpp', 'data/ttplaybackmuxtask.cpp', 'data/ttavdata.cpp', 'gui/ttcurrentframe.cpp', 'gui/ttcutsettingsmuxer.cpp', 'common/ttstreamfiles.h', 'common/ttencodernames.h', 'avstream/ttesinfo.cpp']
SCOPE = scope_files()
def in_scope(row, files=None):
    files = SCOPE if files is None else files
    return any(loc.split(":")[0] in files for loc in (row.get("location") or "").split(";"))

store = vd.load(OUT)
stats = Counter()

rows = [r for r in csv.DictReader((SCAN / "candidates.tsv").open(), delimiter="\t")
        if r["status"] == "new" and in_scope(r, RUN_SCOPE)]
seen = set()
for r in rows:
    if r["fingerprint"] not in RULINGS:
        raise SystemExit(f"in-scope candidate without a ruling: {r['name']}")
    verdict, reason = RULINGS[r["fingerprint"]]
    store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
    seen.add(r["fingerprint"])
    stats[verdict] += 1
unused = set(RULINGS) - seen
if unused:
    raise SystemExit(f"{len(unused)} rulings match no candidate")

for fp, (kind, reason) in OLD_DONE.items():
    store[fp] = vd.Verdict(fp, kind, "consolidate", D, reason[:400])
    stats["old closed"] += 1

if RESCAN.exists():
    for r in csv.DictReader((RESCAN / "candidates.tsv").open(), delimiter="\t"):
        if r["status"] != "new" or not in_scope(r) or r["fingerprint"] in store:
            continue
        if r["fingerprint"] in RESCAN_RULINGS:
            verdict, reason = RESCAN_RULINGS[r["fingerprint"]]
        else:
            verdict, reason = "deliberate", RESCAN_NOTE
        store[r["fingerprint"]] = vd.Verdict(r["fingerprint"], r["kind"], verdict, D, reason[:400])
        stats["rescan " + verdict] += 1

vd.save(OUT, store)
print(f"rulings: {len(RULINGS)}   store size: {len(store)}")
print(dict(stats))
