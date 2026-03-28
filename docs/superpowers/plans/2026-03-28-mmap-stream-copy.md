# mmap Stream-Copy Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace per-frame QFile seek+read in Smart Cut stream-copy with mmap pointer access and bulk writes, reducing ~420k syscalls to a handful.

**Architecture:** Reuse the existing mmap from TTNaluParser (already maps entire ES file for start-code scanning). Add `accessUnitPtr()` for zero-copy frame access. Rewrite `streamCopyFrames()` with two paths: bulk-write for unpatched segments, mmap-based per-frame for patched segments.

**Tech Stack:** Qt5 QFile::map (mmap wrapper), C++ raw pointer I/O

---

### Task 1: Add `accessUnitPtr()` and `isMapped()` to TTNaluParser

**Files:**
- Modify: `avstream/ttnaluparser.h:227` (add method declarations)
- Modify: `avstream/ttnaluparser.cpp:833` (add implementation after `readAccessUnitData`)

- [ ] **Step 1: Add method declarations to ttnaluparser.h**

After line 227 (`QByteArray readAccessUnitData(int index);`), add:

```cpp
    // Zero-copy access to AU data via mmap (returns nullptr if not mapped)
    const uchar* accessUnitPtr(int index, int64_t& size) const;
    bool isMapped() const { return mMappedFile != nullptr; }
```

- [ ] **Step 2: Implement `accessUnitPtr()` in ttnaluparser.cpp**

After `readAccessUnitData()` (after line 833), add:

```cpp
// ----------------------------------------------------------------------------
// Zero-copy pointer to Access Unit data via mmap
// Returns nullptr if file is not memory-mapped or index is invalid
// ----------------------------------------------------------------------------
const uchar* TTNaluParser::accessUnitPtr(int index, int64_t& size) const
{
    if (!mMappedFile || index < 0 || index >= mAccessUnits.size()) {
        size = 0;
        return nullptr;
    }

    const TTAccessUnit& au = mAccessUnits[index];
    size = au.endOffset - au.startOffset;
    return mMappedFile + au.startOffset;
}
```

- [ ] **Step 3: Build**

```bash
cd /usr/local/src/TTCut-ng && rm -f obj/ttnaluparser.o && bear -- make -j$(nproc)
```

Expected: compiles without errors.

- [ ] **Step 4: Commit**

```bash
git add avstream/ttnaluparser.h avstream/ttnaluparser.cpp
git commit -m "Add accessUnitPtr() for zero-copy mmap frame access

Returns const uchar* into memory-mapped ES file. No syscalls,
no heap allocation. Falls back to nullptr if mmap unavailable."
```

---

### Task 2: Rewrite `streamCopyFrames()` with mmap bulk-write and per-frame paths

**Files:**
- Modify: `extern/ttessmartcut.cpp:1239-1279` (rewrite `streamCopyFrames`)

- [ ] **Step 1: Replace `streamCopyFrames()` implementation**

Replace lines 1239-1279 in `extern/ttessmartcut.cpp` with:

```cpp
bool TTESSmartCut::streamCopyFrames(QFile& outFile, int startFrame, int endFrame,
                                     int patchReorderFrames, int frameNumDelta)
{
    qDebug() << "    Stream-copying frames" << startFrame << "->" << endFrame;
    if (frameNumDelta != 0) {
        qDebug() << "    frame_num delta:" << frameNumDelta
                 << "(MaxFrameNum=" << (1 << mLog2MaxFrameNum) << ")";
    }

    bool needsPatching = (patchReorderFrames > 0 && mParser.codecType() == NALU_CODEC_H264)
                      || (frameNumDelta != 0 && mLog2MaxFrameNum > 0 && mParser.codecType() == NALU_CODEC_H264);
    int maxFrameNum = (mLog2MaxFrameNum > 0) ? (1 << mLog2MaxFrameNum) : 0;

    // --- Bulk-write path: no patching needed, mmap available ---
    if (!needsPatching && mParser.isMapped()) {
        int64_t startSize, endSize;
        const uchar* startPtr = mParser.accessUnitPtr(startFrame, startSize);
        const uchar* endPtr = mParser.accessUnitPtr(endFrame, endSize);

        if (startPtr && endPtr) {
            int64_t totalSize = (endPtr + endSize) - startPtr;
            qDebug() << "    Bulk-write:" << (endFrame - startFrame + 1) << "frames,"
                     << (totalSize / (1024*1024)) << "MB";

            if (outFile.write(reinterpret_cast<const char*>(startPtr), totalSize) != totalSize) {
                setError(QString("Bulk write failed for frames %1-%2").arg(startFrame).arg(endFrame));
                return false;
            }

            mFramesStreamCopied += (endFrame - startFrame + 1);
            return true;
        }
        // Fall through to per-frame path if accessUnitPtr failed
    }

    // --- Per-frame path: patching required or mmap unavailable ---
    for (int i = startFrame; i <= endFrame; ++i) {
        QByteArray auData;

        // Prefer mmap over QFile seek+read
        if (mParser.isMapped()) {
            int64_t auSize;
            const uchar* auPtr = mParser.accessUnitPtr(i, auSize);
            if (auPtr) {
                auData = QByteArray(reinterpret_cast<const char*>(auPtr), auSize);
            }
        }

        // Fallback to QFile read
        if (auData.isEmpty()) {
            auData = mParser.readAccessUnitData(i);
        }

        if (auData.isEmpty()) {
            setError(QString("Failed to read frame %1").arg(i));
            return false;
        }

        // Patch H.264 SPS NALs inline if requested
        if (patchReorderFrames > 0 && mParser.codecType() == NALU_CODEC_H264) {
            auData = patchSpsNalsInAccessUnit(auData, patchReorderFrames);
        }

        // Patch H.264 frame_num for inter-segment continuity
        if (frameNumDelta != 0 && mLog2MaxFrameNum > 0 &&
            mParser.codecType() == NALU_CODEC_H264) {
            auData = patchFrameNumInAU(auData, mLog2MaxFrameNum, frameNumDelta, maxFrameNum);
        }

        // Write to output
        if (outFile.write(auData) != auData.size()) {
            setError(QString("Failed to write frame %1").arg(i));
            return false;
        }

        mFramesStreamCopied++;
    }

    return true;
}
```

- [ ] **Step 2: Build**

```bash
cd /usr/local/src/TTCut-ng && rm -f obj/ttessmartcut.o && bear -- make -j$(nproc)
```

Expected: compiles without errors.

- [ ] **Step 3: Commit**

```bash
git add extern/ttessmartcut.cpp
git commit -m "Rewrite streamCopyFrames with mmap bulk-write and per-frame paths

Bulk-write: when no patching needed, writes entire frame range in
one syscall via mmap pointer. Per-frame: uses mmap instead of
QFile seek+read, eliminating 2 syscalls per frame. Falls back to
QFile if mmap unavailable."
```

---

### Task 3: Functional Verification

**Files:**
- No code changes — testing only

- [ ] **Step 1: Build full project**

```bash
cd /usr/local/src/TTCut-ng && make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

Expected: compiles without errors or warnings in modified files.

- [ ] **Step 2: Test with H.264 project**

Open an existing H.264 project in TTCut-ng, perform a cut. Check the debug log for:
- `Bulk-write: N frames, M MB` — confirms bulk-write path is used
- No `Failed to read frame` or `Failed to write frame` errors

- [ ] **Step 3: Run quality check on cut output**

```bash
ttcut-quality-check \
  --video original.264 --audio original_deu.ac3 \
  --cut cut.mkv --cuts "START-END" --info original.info
```

Expected: all tests PASS. Stream metadata, PTS, duration, visual, A/V sync must be identical to pre-optimization results.

- [ ] **Step 4: Test with H.265 project**

Repeat step 2-3 with an H.265 file. H.265 streams don't use frame_num patching, so the bulk-write path should be used for all stream-copy segments.

- [ ] **Step 5: Test fallback path**

Temporarily add `mMappedFile = nullptr;` after the mmap call in `findNextStartCode()` (line 270 in ttnaluparser.cpp), rebuild, and cut a file. Verify the output is identical (same file size, same bytes). Remove the debug line and rebuild.

- [ ] **Step 6: Performance measurement**

Run a cut on a large file (>4GB) and compare wall-clock time with a previous cut of the same file. The stream-copy phase (visible in debug log timestamps) should be significantly faster.
