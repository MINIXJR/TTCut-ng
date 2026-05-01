# Security Audit 2026-05-01 — TTCut-ng v0.68.0

**Status:** completed, all findings remediated in the same session
**Baseline:** `docs/security-audit-2026-03-02.md` (commit `aea1809`)
**Threat model:** local desktop-only video editor; primary attack surface is
malicious media files (`.264` / `.265` / `.m2v` / `.ac3` / `.mp2` / `.aac` /
`.srt` / `.info` / `.ttcut`) that get opened by the user. No network, no
privileged operations.

## Executive Summary

The 22 fixes from the 2026-03-02 audit are all still in place — no
regressions. This audit found 16 new issues (1 High, 5 Medium, 6 Low,
4 Info) and re-evaluated the three Low-Risk findings that were skipped
last time.

| Severity | Count |
|----------|-------|
| Critical | 0 |
| High     | 1 |
| Medium   | 5 |
| Low      | 6 |
| Info     | 4 |

One audit-described finding (NEW-3, slice-type re-use) turned out to be a
**false positive** on direct code re-read — the loop already resets
`nalStart` to `-1` at the bottom of every outer-`if` iteration, so the
"old value leaks across iterations" path can't actually be reached.

## Re-evaluation of skipped 2026-03-02 findings

| ID  | 2026-03 Status | 2026-05 Status | Notes |
|-----|---------------|----------------|-------|
| L3  | skipped       | partly fixed, replaced by NEW-11 | Float-vs-int version compare is fixed; path-traversal via `<Name>` elements is now NEW-11. |
| L4  | skipped       | **fixed**       | `frame_rate_code` and `aspect_ratio_information` now have range guards; can be retired from backlog. |
| L6  | skipped       | still Low, more sites | Listed concretely as NEW-7, NEW-8, NEW-9. All resolved this session. |

## New Findings

### NEW-1 — AC3 `bitRate()` array OOB-read (HIGH)

`avstream/ttac3audioheader.cpp:96`

`AC3BitRate[]` has 38 entries; `frmsizecod` is a 6-bit field (0..63), so
values 38..63 read past the array. The H1-fix from the previous audit
guarded `AC3FrameLength`, but `bitRate()` was missed. Called from
`ttac3audiostream.cpp:139` while computing `frame_time`, so a manipulated
`.ac3` with `frmsizecod >= 38` produces an OOB read on every header
parse.

**Fix:** Bounds-check inside `bitRate()` returning `0` for invalid
`frmsizecod`. `sampleRate()` is technically safe (`fscod` is a 2-bit
field) and was left as-is.

### NEW-2 — Unbounded UE loops in SPS parsers (MEDIUM)

- `extern/ttessmartcut.cpp:1019` and `:4143` (`num_ref` loop in PoC type 1)
- `extern/ttessmartcut.cpp:4061` (`cpb_cnt_minus1` HRD loop)
- `extern/ttffmpegwrapper.cpp:499` (`n` loop in PoC type 1)
- `avstream/ttnaluparser.cpp:525` (`numRefFrames` loop in PoC type 1)

A manipulated SPS can encode `num_ref_frames_in_pic_order_cnt_cycle` or
`cpb_cnt_minus1` with up to ~2³¹ via the UE coding's leading-zeros
prefix. The follow-up loop iterates that many times, each iteration
calling another exp-Golomb read with bounds checks — so no memory
corruption, but a multi-second to indefinite UI hang on stream open.

**Fix:** Cap each counter against the H.264 spec's hard maximum
(`num_ref` ≤ 255 per 7.4.2.1.1; `cpb_cnt_minus1` ≤ 31 per E.1.2). On
overflow we either bail out of SPS parsing or clamp the loop bound.

### NEW-3 — `parseH264SliceTypeFromPacket` `nalStart` re-use (FALSE POSITIVE)

`avstream/ttnaluparser.cpp:1418`

Manual code review showed the audit's claim was incorrect: the `nalStart
= -1` reset at the bottom of the `if (00 00)` outer block always runs
when entered, so the "leaked nalStart from a previous iteration" path
can't occur. No fix needed.

### NEW-4 — `.info` `decode_error_regions` unbounded (MEDIUM)

`avstream/ttesinfo.cpp:232`

The region-count value was used directly as a loop bound. A manipulated
`.info` with `decode_error_regions=2000000000` would hang the UI for
many seconds even with empty per-region values.

**Fix:** Clamp via `qBound(0, …, 4096)`.

### NEW-5 — `.info` `es_extra_frames` unbounded list (MEDIUM)

`avstream/ttesinfo.cpp:218`

Comma-separated frame-index list was parsed without size limit; a
millions-of-entries `.info` would consume unbounded memory.

**Fix:** Hard cap at 100 000 entries.

### NEW-6 — `rewriteTempRefData` missing offset bounds-check (MEDIUM)

`avstream/ttmpeg2videostream.cpp:974`

Sister of the M5 GOP-rewrite fix: `headerOffset() < bufferStartOffset`
underflows the unsigned subtraction, then casts to `int` — `offset`
ends up either negative or huge, and the subsequent `buffer[offset]`
write corrupts the heap.

**Fix:** Reject inputs with `headerOffset() < bufferStartOffset` and
require the resulting `offset + 1 < 262144` (matches the existing
`rewriteGOP` guard).

### NEW-7 — `av_new_packet` return value ignored (LOW)

- `extern/ttessmartcut.cpp:2041, :2841`
- `extern/ttmkvmergeprovider.cpp:922`

Under memory pressure, `av_new_packet` can return `-ENOMEM` and leave
`packet->data == nullptr`; the immediately-following `memcpy` then
crashes.

**Fix:** Check return code, free the packet, propagate failure (return
`false` from the calling helper, ultimately surfacing as a cut/mux
error rather than a crash).

### NEW-8 — `av_malloc`/`av_mallocz` without NULL check (LOW)

- `extern/ttessmartcut.cpp:2600` (decoder `extradata`)
- `extern/ttmkvmergeprovider.cpp:441` (chapter array + per-chapter alloc)

Same pattern as NEW-7. Adds explicit failure paths and partial-cleanup
in the chapter-array case so `nb_chapters` is set only for entries we
actually allocated successfully.

### NEW-9 — `avcodec_alloc_context3` chain without NULL/return checks (LOW)

`extern/ttffmpegwrapper.cpp:1995, :2307`

Two issues here:

1. `avcodec_alloc_context3()` and `avcodec_parameters_to_context()`
   results were dereferenced directly. On allocation failure or
   incompatible `codecpar`, the next field write crashes.
2. `cutAudioStream`'s acmod-renormalization path set
   `needsReencode = false` on decoder allocation failure, but then
   continued into the decode block — a *latent* NULL deref of
   `ac3DecCtx` even before the new finding. The original code had this
   bug; the audit incidentally exposed it.

**Fix:** Restructure to `if (needsReencode) { … allocate … }` followed
by a separate `if (needsReencode) { decode } else { stream-copy }`
block, so any allocation failure now correctly falls through to the
stream-copy fast path.

### NEW-10 — IDD reader switch without `default` case (LOW)

`avstream/ttmpeg2videostream.cpp:464`

`newHeader` was never reset between iterations; an unknown `headerType`
left a stale pointer to the *previous* iteration's header (already
owned by the list) and the next `header_list->add(newHeader)` re-added
it — a double-add that would cause a double-free during destruction.

**Fix:** Reset `newHeader = NULL;` at the top of each iteration, add a
`default:` case that warns and `continue`s.

### NEW-11 — `.ttcut` path-traversal via `<Name>` (LOW)

`data/ttcutprojectdata.cpp:144, :180, :534, :609-610`

`<Name>` elements were passed unmodified to `doOpenVideoStream` /
`doOpenAudioStream` / `doOpenSubtitleStream`, and `<CutDirPath>` /
`<CutVideoName>` were copied directly into the global `TTCut::*`
strings. Information disclosure from a malicious `.ttcut` was the
worst-case scenario (e.g. `.srt` parser reading `/etc/passwd` and
showing it as subtitles), with the matching write-disclosure if the
user later runs a cut.

**Fix:** Introduced `resolveProjectPath()` helper that:

- Rejects strings containing NUL or other control bytes.
- Rejects path segments equal to `..` (path-traversal hallmark).
- Anchors relative paths to the `.ttcut` file's directory (which is
  what users would expect anyway).
- Leaves absolute paths intact (existing projects continue to load).

Also added explicit `size() < 2` guards before `at(0).text()` /
`at(1).text()` accesses (subsumes NEW-13).

`CutVideoName` is filename-only, so we additionally reject `/`, `\` and
control bytes; `CutDirPath` runs through the same `resolveProjectPath`.

### NEW-12 — `TTFileBuffer::readLine` missing length cap (LOW)

`avstream/ttfilebuffer.cpp:343`

A single-line file (or a chunk before the first delimiter) could grow
the returned `QString` arbitrarily.

**Fix:** Cap at 1 MiB. Realistic SRT/VDR-marks lines are well under
that; the cap is a backstop, not a normal-case limit.

### NEW-13 — Subtitle XML `at(0)/at(1)` bounds (INFO)

Subsumed by the NEW-11 fix (`size() < 2` guards now in place for all
parse* methods).

### NEW-14 — `mChapterFile` validation (INFO)

`extern/ttmkvmergeprovider.cpp:378`

In current code the path is always produced internally by
`generateChapterFile()`; the audit's exploit scenario isn't reachable.
Added defense-in-depth: `setChapterFile` rejects paths containing
control bytes regardless of caller.

### NEW-15 — `vdr-demux-example.sh` unsafe `eval` (INFO)

`tools/vdr-demux-example.sh:148`

`eval "SELECTED_DIRS=($SELECTED)"` would execute backticks/`$()`
embedded in directory names or kdialog responses. Practically very
hard to trigger (VDR controls the recording-dir names), but a real
defense-in-depth issue.

**Fix:** `kdialog --separate-output` plus `mapfile -t` parses the
selection without invoking the shell parser. Same `dialog` codepath
already used `mapfile` and was left untouched.

### NEW-16 — NAL-parser missed emulation-prevention stripping (INFO)

`avstream/ttnaluparser.cpp:451` (`parseH264SpsData`)

The H.264 SPS parser worked directly off the raw NAL body. Streams with
scaling lists or VUI HRD parameters past an EP byte (`00 00 03`) get
shifted bit positions, producing wrong `log2MaxFrameNumMinus4` /
`frameMbsOnlyFlag` values. No memory corruption, but the wrong values
propagate into PAFF detection and slice-header parsing.

The slice-header parsers (`parseH264SliceHeader`,
`parseH265SliceHeader`, `parseH264SliceTypeFromPacket`) only read the
first ~3 bytes of the NAL body, where EP escapes essentially never
occur — those were left alone for now.

**Fix:** Added `ttNaluRemoveEpb()` helper inside `ttnaluparser.cpp`
and call it at the start of `parseH264SpsData`.

## Methodology Notes

- All 22 fixes from `aea1809` were re-checked against the current code.
  None were removed or weakened; the existing guards
  (`AC3FrameLength` bounds, `frame_rate_code` range,
  `audio_track_count` cap, MPEG2 IDD offset check, the various
  null/length guards added in March) are still present.
- The audit was structured around the existing severity scheme from
  `2026-03-02.md`. New findings keep the same "Critical → RCE / High
  → memory corruption from a manipulated file / Medium → DoS / Low →
  crash with low impact / Info → defense-in-depth" mapping.
- For each finding, we consciously decided whether to fail-closed
  (reject and bail out) or fail-open with a warning. Parser entry
  points fail-closed; provider/resource paths log and continue when
  doing so doesn't risk further corruption.
