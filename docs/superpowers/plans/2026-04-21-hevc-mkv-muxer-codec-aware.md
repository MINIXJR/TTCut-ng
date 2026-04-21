# HEVC MKV Muxer Codec-Aware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the MKV ES muxer codec-aware so HEVC video packets are written to the MKV instead of being silently dropped as non-VCL.

**Architecture:** Replace the hardcoded H.264 NAL-type check in `extern/ttmkvmergeprovider.cpp` with a codec-dispatched helper. Caller (`data/ttavdata.cpp`) passes the codec via a new explicit setter, analogous to `setIsPAFF`. No auto-detection — the pipeline already validates the codec at entry (`TTOpenVideoTask::run()`).

**Tech Stack:** Qt 5, qmake, libavformat/libavcodec (matroska muxer), in-tree `avstream/ttnaluparser.h` for H.264 NAL type constants.

**Spec reference:** `docs/superpowers/specs/2026-04-21-hevc-mkv-muxer-codec-aware-design.md`

---

## File Structure

**Files touched (all existing):**

- `extern/ttmkvmergeprovider.h` — Public API: add `setVideoCodecId()`; private member `mVideoCodecId`.
- `extern/ttmkvmergeprovider.cpp` — Implementation: constructor init, `isVclNalByte()` helper, entry log, two call-site replacements.
- `data/ttavdata.cpp` — Caller: hand the codec to the muxer at line 1581.

**No new files, no file moves.**

---

## Notes for the Implementer

- **Build command:** `qmake ttcut-ng.pro && bear -- make -j$(nproc)`. Use `bear --` so `compile_commands.json` stays current for clangd. If only `.cpp` changed and not `.pro`, `bear -- make -j$(nproc)` alone is fine.
- **qmake dependency tracking is unreliable.** After changing headers, run `rm obj/ttmkvmergeprovider.o obj/ttavdata.o` before `make`. Easier: `make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)`.
- **No unit-test framework for this module.** Verification is manual: build must succeed, binary must run, cut must produce a playable MKV. "Failing test" steps below mean "reproduce the current bug first so you can recognize when it's fixed."
- **Test file (user's machine):** `Ausdrucksstarke_Designermode.265` — HEVC 4K, 50fps, referenced in project memory.
- **Debug log path:** `/usr/local/src/CLAUDE_TMP/ttcut-debug.log` (CLAUDE_TMP is the user's convention, see `~/.claude/CLAUDE.md`). Redirect with `./ttcut-ng 2>&1 | tee /usr/local/src/CLAUDE_TMP/TTCut-ng/ttcut-debug.log`.
- **Wayland:** the binary needs `QT_QPA_PLATFORM=xcb` on Wayland systems. Preface commands with that env var if you see a startup failure.
- **Commit style:** imperative, one logical change per commit (see `CLAUDE.md`). No co-author line needed for individual task commits — that's for the final consolidation.

---

## Task 0: Branch + Worktree

**Files:** none (git state only)

- [ ] **Step 1: Create a feature branch with a worktree**

```bash
cd /usr/local/src/TTCut-ng
git worktree add ../TTCut-ng.hevc-mkv-fix -b fix/hevc-mkv-codec-aware master
cd ../TTCut-ng.hevc-mkv-fix
```

Expected: new worktree at `../TTCut-ng.hevc-mkv-fix/`, branch `fix/hevc-mkv-codec-aware` created from `master`.

- [ ] **Step 2: Confirm the branch is clean and on top of master**

```bash
git status
git log --oneline -1
```

Expected: `On branch fix/hevc-mkv-codec-aware`, clean tree, HEAD at the latest master commit.

---

## Task 1: Reproduce the bug (establish baseline)

**Files:** none (runtime check only)

The point of this task is to confirm the bug exists before fixing it, so that after the fix you can tell the fix actually worked and didn't just accidentally fail in a different way.

- [ ] **Step 1: Build current master from the worktree**

```bash
qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: clean build, `ttcut-ng` binary in the worktree root.

- [ ] **Step 2: Load H.265 test file, perform a cut, capture the debug log**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng 2>&1 | tee /usr/local/src/CLAUDE_TMP/TTCut-ng/baseline-h265.log
```

In the UI: open the H.265 test file (`Ausdrucksstarke_Designermode.265` or similar HEVC ES), set a short cut (e.g. 10 seconds), run Cut → MKV. Quit after the cut completes.

- [ ] **Step 3: Confirm the bug in the log and in the output**

```bash
grep -c 'MKV PAFF: skip non-VCL video packet' /usr/local/src/CLAUDE_TMP/TTCut-ng/baseline-h265.log
ffprobe -v error -show_streams <output.mkv> 2>&1 | grep codec_type
```

Expected:
- The `grep -c` returns a large number (every video packet is dropped).
- `ffprobe` shows only `codec_type=audio`, no `codec_type=video`.

This is the bug. Keep this log for later comparison.

---

## Task 2: Add codec setter to the muxer header

**Files:**
- Modify: `extern/ttmkvmergeprovider.h`

- [ ] **Step 1: Add the libav codec-ID include**

Above the existing libav includes (if any) or at the top of the `#include` block, add:

```cpp
extern "C" {
#include <libavcodec/codec_id.h>
}
```

If `extern "C" { #include <...> }` blocks already exist in the header, add `<libavcodec/codec_id.h>` to one of them rather than creating a duplicate block.

- [ ] **Step 2: Add the setter directly below `setIsPAFF`**

In the `public:` section, immediately after the existing `setIsPAFF` inline definition (around line 81-84):

```cpp
    // Video codec of the ES input stream — used by the muxer to parse
    // NAL unit types correctly. H.264 and H.265 have different header layouts.
    // Caller (ttavdata.cpp) derives this from videoStream->streamType().
    void setVideoCodecId(enum AVCodecID codecId) { mVideoCodecId = codecId; }
```

- [ ] **Step 3: Add the private member beside `mIsPAFF`**

In the `private:` section, next to `bool mIsPAFF;` and `int mH264Log2MaxFrameNum;` (around line 104-105):

```cpp
    enum AVCodecID mVideoCodecId;
```

- [ ] **Step 4: Verify the header compiles as a unit**

```bash
make obj/ttmkvmergeprovider.o
```

Expected: recompiles cleanly. If there's a dependency failure with other files including this header, full rebuild:

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: entire build succeeds (the header change is additive).

- [ ] **Step 5: Commit**

```bash
git add extern/ttmkvmergeprovider.h
git commit -m "MKV muxer: add setVideoCodecId API"
```

---

## Task 3: Initialize the codec member and add the helper

**Files:**
- Modify: `extern/ttmkvmergeprovider.cpp`

- [ ] **Step 1: Add the header include for H.264 NAL type constants**

At the top of `extern/ttmkvmergeprovider.cpp`, next to the existing project-local includes:

```cpp
#include "../avstream/ttnaluparser.h"
```

This gives us `H264::NAL_SLICE` (= 1) and `H264::NAL_IDR_SLICE` (= 5) as named constants instead of magic numbers.

- [ ] **Step 2: Initialize `mVideoCodecId` in the constructor**

Find the `TTMkvMergeProvider::TTMkvMergeProvider()` constructor. In the initializer list (or early assignment block, matching the style used for `mIsPAFF`), add:

```cpp
    , mVideoCodecId(AV_CODEC_ID_NONE)
```

or, if the constructor uses body assignments:

```cpp
    mVideoCodecId = AV_CODEC_ID_NONE;
```

Match the existing style for `mIsPAFF`.

- [ ] **Step 3: Add the file-local `isVclNalByte` helper**

Near the top of the `.cpp` file (after `#include`s, before any member functions), in an anonymous namespace or as a `static` free function:

```cpp
namespace {

// Returns true if the byte at `b` starts a Video Coding Layer NAL unit
// for the given codec. `b` must point to the first NAL payload byte
// (after the start code).
//
// H.264: 1-byte header, 5-bit nal_unit_type in bits 0-4.
//        VCL types: 1 (non-IDR slice), 5 (IDR slice).
// H.265: 2-byte header, 6-bit nal_unit_type in bits 1-6 of first byte.
//        VCL types: 0-31 per HEVC spec.
bool isVclNalByte(enum AVCodecID codec, const uint8_t* b)
{
    switch (codec) {
        case AV_CODEC_ID_H264: {
            uint8_t nt = b[0] & 0x1F;
            return nt == H264::NAL_SLICE || nt == H264::NAL_IDR_SLICE;
        }
        case AV_CODEC_ID_HEVC: {
            uint8_t nt = (b[0] >> 1) & 0x3F;
            return nt <= 31;
        }
        default:
            Q_ASSERT_X(false, "isVclNalByte",
                       "unexpected video codec in MKV ES mux path");
            return false;
    }
}

} // namespace
```

- [ ] **Step 4: Add an entry log at the start of `mux()`**

Find `bool TTMkvMergeProvider::mux(...)` (the main entry point). After the existing initial log lines (e.g. `qDebug() << "MKV mux:" << outputFile;`), add:

```cpp
    qDebug() << "MKV mux: videoCodecId =" << avcodec_get_name(mVideoCodecId);
```

If `avcodec_get_name` requires an additional include, add `extern "C" { #include <libavcodec/avcodec.h> }` — but in practice the existing libav includes already pull this in. Compile first; only add if the compiler complains.

- [ ] **Step 5: Build**

```bash
rm -f obj/ttmkvmergeprovider.o
bear -- make -j$(nproc)
```

Expected: clean compile. The helper is unused so far (the two call-sites still use the hardcoded pattern) — no warnings because it's in an anonymous namespace and will be referenced in Tasks 4 and 5. If the compiler warns about an unused function, accept the warning for one task — it will be resolved by the next task.

If an unused-function warning blocks the build (`-Werror`), temporarily silence it with `[[maybe_unused]]`:

```cpp
[[maybe_unused]] bool isVclNalByte(...)
```

Then remove the attribute in Task 4 once the first call-site uses it.

- [ ] **Step 6: Commit**

```bash
git add extern/ttmkvmergeprovider.cpp
git commit -m "MKV muxer: add codec-aware VCL NAL helper and init"
```

---

## Task 4: Switch the non-PAFF VCL check to the helper

**Files:**
- Modify: `extern/ttmkvmergeprovider.cpp` (around lines 811-829 in the pre-fix file — the "Non-PAFF video: check for VCL NAL" block)

- [ ] **Step 1: Locate the block**

```bash
grep -n 'Non-PAFF video: check for VCL NAL' extern/ttmkvmergeprovider.cpp
```

Expected: one match. The block begins with a comment and spans roughly 20 lines.

- [ ] **Step 2: Replace the hardcoded H.264 pattern with the helper**

Current code (to be replaced):

```cpp
                } else if (in.outIdx == 0 && in.pkt->data && in.pkt->size > 0) {
                    // Non-PAFF video: check for VCL NAL
                    const uint8_t* d = in.pkt->data;
                    int sz = in.pkt->size;
                    for (int p = 0; p < sz - 3; p++) {
                        if (d[p] == 0 && d[p+1] == 0) {
                            int s = -1;
                            if (d[p+2] == 1) s = p + 3;
                            else if (d[p+2] == 0 && p+3 < sz && d[p+3] == 1) s = p + 4;
                            if (s >= 0 && s < sz) {
                                uint8_t nt = d[s] & 0x1F;
                                if (nt == 1 || nt == 5) { hasVclNal = true; break; }
                            }
                        }
                    }
                    if (!hasVclNal && sz >= 1) {
                        uint8_t nt = d[0] & 0x1F;
                        if (nt == 1 || nt == 5) hasVclNal = true;
                    }
                }
```

Replace with:

```cpp
                } else if (in.outIdx == 0 && in.pkt->data && in.pkt->size > 0) {
                    // Non-PAFF video: check for VCL NAL (codec-aware)
                    const uint8_t* d = in.pkt->data;
                    int sz = in.pkt->size;
                    for (int p = 0; p < sz - 3; p++) {
                        if (d[p] == 0 && d[p+1] == 0) {
                            int s = -1;
                            if (d[p+2] == 1) s = p + 3;
                            else if (d[p+2] == 0 && p+3 < sz && d[p+3] == 1) s = p + 4;
                            if (s >= 0 && s < sz && isVclNalByte(mVideoCodecId, d + s)) {
                                hasVclNal = true; break;
                            }
                        }
                    }
                    if (!hasVclNal && sz >= 1 && isVclNalByte(mVideoCodecId, d)) {
                        hasVclNal = true;
                    }
                }
```

- [ ] **Step 3: Build**

```bash
rm -f obj/ttmkvmergeprovider.o
bear -- make -j$(nproc)
```

Expected: clean compile. Any `[[maybe_unused]]` from Task 3 can now be removed if added — the helper is now referenced.

- [ ] **Step 4: Commit**

```bash
git add extern/ttmkvmergeprovider.cpp
git commit -m "MKV muxer: use codec-aware VCL check in non-PAFF path"
```

---

## Task 5: Switch the PAFF field-pair inner skip-loop to the helper

**Files:**
- Modify: `extern/ttmkvmergeprovider.cpp` (around lines 860-876 in the pre-fix file — the "Skip non-VCL packets between field pairs (e.g. SEI)" block)

- [ ] **Step 1: Locate the block**

```bash
grep -n 'Skip non-VCL packets between field pairs' extern/ttmkvmergeprovider.cpp
```

Expected: one match. The block is inside the PAFF branch and scans ahead for the second field packet.

- [ ] **Step 2: Replace the hardcoded H.264 pattern with the helper**

Current code (to be replaced):

```cpp
                    // Skip non-VCL packets between field pairs (e.g. SEI)
                    while (!in.eof && in.pkt->data && in.pkt->size > 0) {
                        const uint8_t* nd = in.pkt->data;
                        int nsz = in.pkt->size;
                        bool nextIsVcl = false;
                        for (int p = 0; p < nsz - 3; p++) {
                            if (nd[p] == 0 && nd[p+1] == 0) {
                                int s = -1;
                                if (nd[p+2] == 1) s = p + 3;
                                else if (nd[p+2] == 0 && p+3 < nsz && nd[p+3] == 1) s = p + 4;
                                if (s >= 0 && s < nsz) {
                                    uint8_t nt = nd[s] & 0x1F;
                                    if (nt == 1 || nt == 5) { nextIsVcl = true; break; }
                                }
                            }
                        }
                        if (nextIsVcl) break;
                        qDebug() << "  MKV PAFF: skip non-VCL between fields, sz=" << nsz;
                        av_packet_unref(in.pkt);
                        readNextPacket(in);
                    }
```

Replace with:

```cpp
                    // Skip non-VCL packets between field pairs (e.g. SEI).
                    // This path is H.264-only (mIsPAFF implies H.264), but use
                    // the codec-aware helper for consistency.
                    while (!in.eof && in.pkt->data && in.pkt->size > 0) {
                        const uint8_t* nd = in.pkt->data;
                        int nsz = in.pkt->size;
                        bool nextIsVcl = false;
                        for (int p = 0; p < nsz - 3; p++) {
                            if (nd[p] == 0 && nd[p+1] == 0) {
                                int s = -1;
                                if (nd[p+2] == 1) s = p + 3;
                                else if (nd[p+2] == 0 && p+3 < nsz && nd[p+3] == 1) s = p + 4;
                                if (s >= 0 && s < nsz && isVclNalByte(mVideoCodecId, nd + s)) {
                                    nextIsVcl = true; break;
                                }
                            }
                        }
                        if (nextIsVcl) break;
                        qDebug() << "  MKV PAFF: skip non-VCL between fields, sz=" << nsz;
                        av_packet_unref(in.pkt);
                        readNextPacket(in);
                    }
```

- [ ] **Step 3: Build**

```bash
rm -f obj/ttmkvmergeprovider.o
bear -- make -j$(nproc)
```

Expected: clean compile.

- [ ] **Step 4: Commit**

```bash
git add extern/ttmkvmergeprovider.cpp
git commit -m "MKV muxer: use codec-aware VCL check in PAFF field-skip loop"
```

---

## Task 6: Caller passes the codec

**Files:**
- Modify: `data/ttavdata.cpp` (around line 1581, inside the MKV mux branch in `onCutFinished`)

- [ ] **Step 1: Locate the setIsPAFF call**

```bash
grep -n 'setIsPAFF' data/ttavdata.cpp
```

Expected: two matches (there are two MKV mux branches in this file). Focus on the one at ~line 1581, inside `onCutFinished`.

- [ ] **Step 2: Verify the libav codec-ID header is already included**

```bash
grep -n 'libavcodec/codec_id.h\|libavformat/avformat.h\|libavcodec/avcodec.h' data/ttavdata.cpp
```

Expected: at least one libav header is already included. `codec_id.h` is pulled in transitively via `avformat.h` and `avcodec.h`. If not present, add:

```cpp
extern "C" {
#include <libavcodec/codec_id.h>
}
```

- [ ] **Step 3: Add the `setVideoCodecId` call after `setIsPAFF`**

Current code at line 1581:

```cpp
        mkvProvider->setIsPAFF(videoStream->isPAFF(), videoStream->paffLog2MaxFrameNum());
```

Add immediately after:

```cpp
        AVCodecID codecId = (videoStream->streamType() == TTAVTypes::h265_video)
                          ? AV_CODEC_ID_HEVC
                          : AV_CODEC_ID_H264;
        mkvProvider->setVideoCodecId(codecId);
```

- [ ] **Step 4: Check if a second MKV mux branch exists and also needs the call**

```bash
grep -n 'mkvProvider.setIsPAFF\|mkvProvider->setIsPAFF' data/ttavdata.cpp
```

If there are two distinct call sites (one for container-remux at ~line 1464, one for ES-mux at ~line 1581), only the **ES-mux** path needs `setVideoCodecId`. The container-remux path uses libav auto-detection over a full container — not the ES-mux loop that this fix targets.

Skip Step 5 of this task for the container-remux site; the fix is not needed there.

Inspect `grep -B 20 'mkvProvider.setIsPAFF' data/ttavdata.cpp | grep -E 'ES mux|Container-Remux|Container remux|container remux'` to confirm which branch is which before deciding.

- [ ] **Step 5: Build**

```bash
rm -f obj/ttavdata.o
bear -- make -j$(nproc)
```

Expected: clean compile.

- [ ] **Step 6: Commit**

```bash
git add data/ttavdata.cpp
git commit -m "Pass video codec ID to MKV muxer for codec-aware NAL parsing"
```

---

## Task 7: Manual verification — H.265 fix works

**Files:** none (runtime check)

- [ ] **Step 1: Full rebuild to be safe**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: clean build.

- [ ] **Step 2: Run a H.265 cut, compare log and output against Task 1 baseline**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng 2>&1 | tee /usr/local/src/CLAUDE_TMP/TTCut-ng/postfix-h265.log
```

In the UI: open the same H.265 test file as in Task 1. Make a comparable short cut. Run Cut → MKV. Quit.

- [ ] **Step 3: Verify the entry log shows the correct codec**

```bash
grep 'MKV mux: videoCodecId' /usr/local/src/CLAUDE_TMP/TTCut-ng/postfix-h265.log
```

Expected: a line like `MKV mux: videoCodecId = hevc`.

- [ ] **Step 4: Verify packets are no longer dropped**

```bash
grep -c 'skip non-VCL video packet' /usr/local/src/CLAUDE_TMP/TTCut-ng/postfix-h265.log
```

Expected: 0 (or dramatically lower than the baseline — HEVC ES may genuinely contain a handful of non-VCL NALs like AUD/SEI at the packet level).

- [ ] **Step 5: Verify the MKV has a video stream**

```bash
ffprobe -v error -show_streams <output.mkv> 2>&1 | grep -E 'codec_type|codec_name'
```

Expected: shows at least one `codec_type=video` with `codec_name=hevc`, plus audio.

- [ ] **Step 6: Play the MKV in mpv to check actual picture + sound**

```bash
mpv --no-config <output.mkv>
```

Expected: video plays with picture and sound, no black screen. Seek to a segment boundary — no artifacts.

If any expectation fails, stop and investigate. Do not proceed to Task 8.

---

## Task 8: Regression test — H.264 still works

**Files:** none (runtime check)

- [ ] **Step 1: Run a H.264 non-PAFF cut**

```bash
QT_QPA_PLATFORM=xcb ./ttcut-ng 2>&1 | tee /usr/local/src/CLAUDE_TMP/TTCut-ng/postfix-h264.log
```

In the UI: open a standard H.264 ES (progressive or MBAFF — any DVB recording). Make a short cut. Quit after cut.

- [ ] **Step 2: Verify output**

```bash
grep 'MKV mux: videoCodecId' /usr/local/src/CLAUDE_TMP/TTCut-ng/postfix-h264.log
ffprobe -v error -show_streams <output.mkv> 2>&1 | grep codec_name
mpv --no-config <output.mkv>
```

Expected:
- log shows `videoCodecId = h264`
- ffprobe shows `codec_name=h264`
- mpv plays video + audio, no artifacts

- [ ] **Step 3: (If available) run a H.264 PAFF cut**

If you have a PAFF test file (e.g. the `Moon_Crash_(2022).264` referenced in project memory), repeat Step 1-2 with it. The PAFF code path inside the muxer was only changed to call the same helper — the H.264 case returns the same result as the old hardcoded check, so PAFF output should be byte-identical to previous runs.

Quality-check tool can verify objectively if the user has a saved baseline:

```bash
tools/ttcut-quality-check.py --ref <baseline.mkv> --new <output.mkv>
```

Expected: 7/7 PASS (or whatever the baseline used to report).

If only H.264 non-PAFF test is available, continue to Task 9 — PAFF verification can be handled during final user review.

---

## Task 9: Defensive check — forgotten setter triggers Q_ASSERT

**Files:** none (runtime check). Optional but recommended.

- [ ] **Step 1: Temporarily comment out the `setVideoCodecId` call**

```bash
sed -i.bak 's|mkvProvider->setVideoCodecId(codecId);|// mkvProvider->setVideoCodecId(codecId);|' data/ttavdata.cpp
grep 'setVideoCodecId' data/ttavdata.cpp
```

Expected: the line is now commented out.

- [ ] **Step 2: Rebuild and run a cut**

```bash
bear -- make -j$(nproc)
QT_QPA_PLATFORM=xcb ./ttcut-ng 2>&1 | tee /usr/local/src/CLAUDE_TMP/TTCut-ng/assert-check.log
```

Start a cut in the UI.

Expected in a debug build: `Q_ASSERT_X` fires with message `unexpected video codec in MKV ES mux path`, process aborts on the first video packet. In a release build (`QT_NO_DEBUG` defined), no assert — video will be dropped like before the fix. Check which mode `ttcut-ng.pro` builds in:

```bash
grep -E 'CONFIG \+\=|CONFIG \-\=' ttcut-ng.pro | head
```

- [ ] **Step 3: Restore the call and rebuild**

```bash
mv data/ttavdata.cpp.bak data/ttavdata.cpp
grep 'setVideoCodecId' data/ttavdata.cpp
bear -- make -j$(nproc)
```

Expected: file restored to include the call, build passes. Confirm with a final H.265 cut if paranoid.

This task is a sanity check, not a committable change. Do **not** commit Step 1's modification.

---

## Task 10: Squash + merge + update TODO

**Files:**
- Modify: `TODO.md` (remove the "H.265 MKV-Muxing" entry)

- [ ] **Step 1: Verify the feature-branch commit sequence**

```bash
git log --oneline master..HEAD
```

Expected: commits from Tasks 2-6 (4 incremental commits), plus the TODO update next.

- [ ] **Step 2: Remove the obsolete TODO entry**

Open `TODO.md` and delete the entire block under **Low Priority** starting with `- **H.265 MKV-Muxing: Video-Pakete werden als non-VCL verworfen**` through the last indented line of that entry (ends with `- Getrennt von Muxer-UI-Cleanup (2026-04-19) dokumentiert — eigener Brainstorm+Spec nötig`). About 10 lines.

```bash
grep -n 'H.265 MKV-Muxing' TODO.md
```

Expected after the edit: no match.

- [ ] **Step 3: Commit the TODO cleanup**

```bash
git add TODO.md
git commit -m "TODO: remove obsolete H.265 MKV-muxing entry (fixed)"
```

- [ ] **Step 4: Decide on squash**

The user's workflow squashes trivial incremental commits when merging feature branches. Offer this choice explicitly — **do not squash without confirmation**. Ask the user: "Squash the 5 fix commits into one before merging, or keep them as history?" If yes:

```bash
git rebase -i master
# In the editor, keep the first fix commit as 'pick', change all others to 'fixup'.
# Leave the TODO commit as a separate 'pick'.
```

- [ ] **Step 5: Merge and final build**

Hand off to the user for the merge decision (use `superpowers:finishing-a-development-branch`). Do **not** merge to master or push without explicit user confirmation — see project memory for this user's push policy.

---

## Self-Review Checklist (ran before finalizing)

- **Spec coverage:**
  - API change (setter + member) → Task 2 ✅
  - Constructor init → Task 3 Step 2 ✅
  - Helper `isVclNalByte` → Task 3 Step 3 ✅
  - Entry log → Task 3 Step 4 ✅
  - Non-PAFF call-site replacement → Task 4 ✅
  - PAFF field-skip call-site replacement → Task 5 ✅
  - Caller update → Task 6 ✅
  - H.265 verification → Task 7 ✅
  - H.264 regression → Task 8 ✅
  - Defensive (forgotten setter) → Task 9 ✅
  - Rollback → implicit via worktree + individual commits

- **Placeholders:** none.

- **Type consistency:** `AVCodecID` / `enum AVCodecID` used consistently. Member `mVideoCodecId` matches between header and `.cpp`. Helper signature `isVclNalByte(enum AVCodecID, const uint8_t*)` matches both call sites.

---

## Rollback

Per-task commits on a feature branch. Rollback options:
- Abort before merge: `git worktree remove ../TTCut-ng.hevc-mkv-fix` and `git branch -D fix/hevc-mkv-codec-aware`.
- Abort after merge: `git revert <merge-commit>` on master.
