# Smart Cut mmap Stream-Copy Optimization

## Goal

Replace per-frame QFile seek+read with mmap pointer access for Smart Cut stream-copy. Reduce ~420k syscalls to a handful of bulk writes. Target: 6GB film from ~130s to ~15s for stream-copy portion.

## Current State

- `TTNaluParser::readAccessUnitData(int index)` does `mFile.seek()` + `mFile.read()` per frame → QByteArray heap allocation
- `streamCopyFrames()` loops frame-by-frame: read → optional patch → write
- Parser already mmaps entire ES file for start-code scanning (`mMappedFile` in `findNextStartCode()`)
- mmap is discarded after parsing — not reused for frame data access
- 3 syscalls per frame (seek + read + write), ~140k frames for a 6GB film

## Design

### 1. Persistent mmap in TTNaluParser

Keep `mMappedFile` alive after `parseFile()`. Currently it persists until `closeFile()` — verify this is the case and ensure it stays mapped through the Smart Cut phase.

Add new API:

```cpp
// Returns pointer into mmap'd region for AU data. No copy, no syscall.
// Returns nullptr if not mapped or index invalid.
const uchar* accessUnitPtr(int index, int64_t& size) const;

// Check if file is memory-mapped
bool isMapped() const { return mMappedFile != nullptr; }
```

Implementation:
```cpp
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

Fallback: if `isMapped()` is false, `streamCopyFrames()` falls back to existing `readAccessUnitData()`.

### 2. Bulk-Write Path (no patching needed)

When `patchReorderFrames == 0` AND `frameNumDelta == 0` for a segment, no per-frame processing is required. The entire frame range is a contiguous byte region in the source file.

```cpp
if (canBulkCopy && mParser.isMapped()) {
    int64_t startSize, endSize;
    const uchar* startPtr = mParser.accessUnitPtr(startFrame, startSize);
    const uchar* endPtr = mParser.accessUnitPtr(endFrame, endSize);
    // endPtr + endSize = end of last frame
    int64_t totalSize = (endPtr + endSize) - startPtr;
    outFile.write(reinterpret_cast<const char*>(startPtr), totalSize);
}
```

This replaces N×(seek+read+write) with a single write() syscall.

**Contiguity assumption:** Access units from the same ES file are stored sequentially. AU[i].endOffset == AU[i+1].startOffset for consecutive frames. This is guaranteed by the parser which scans start codes sequentially.

### 3. Per-Frame mmap Path (patching needed)

When patching is required, frames still need individual processing. But mmap eliminates seek+read:

```cpp
int64_t auSize;
const uchar* auPtr = mParser.accessUnitPtr(i, auSize);
if (auPtr) {
    QByteArray auData(reinterpret_cast<const char*>(auPtr), auSize);
    // Patch operations on auData (existing code unchanged)
    outFile.write(auData);
} else {
    // Fallback to readAccessUnitData()
    QByteArray auData = mParser.readAccessUnitData(i);
    // ... same patching + write
}
```

Benefit: eliminates seek+read syscalls, only memcpy + write remain. ~140k fewer syscalls for patched segments.

### 4. Fallback

If mmap failed during parsing (e.g., insufficient address space), `mMappedFile` is nullptr. All paths fall back to existing `readAccessUnitData()` with zero behavior change.

### 5. Bulk-Copy Eligibility

A segment is eligible for bulk-copy when ALL of these are true:
- `mParser.isMapped()` — mmap is available
- `patchReorderFrames == 0` — no SPS max_num_reorder_frames patching
- `frameNumDelta == 0` — no frame_num rewriting
- Segment is pure stream-copy (no re-encode frames mixed in)

For a typical film with 4 segments: the first segment often has `frameNumDelta == 0` (starts at frame 0). Later segments may have non-zero `frameNumDelta`. In practice, ~99% of frames are in segments where patching is segment-wide constant, so the decision is per-segment, not per-frame.

### 6. Files Changed

| File | Change |
|------|--------|
| `avstream/ttnaluparser.h` | Add `accessUnitPtr()`, `isMapped()` |
| `avstream/ttnaluparser.cpp` | Implement `accessUnitPtr()`, ensure mmap persists |
| `extern/ttessmartcut.cpp` | Rewrite `streamCopyFrames()` with bulk-write and mmap per-frame paths |

### 7. Testing

- Build and cut a known H.264 file with `ttcut-quality-check` before and after
- Compare: SSIM, PTS consistency, A/V sync, frame count must be identical
- Measure wall-clock time for stream-copy phase (log timestamps)
- Test with large file (>4GB) to verify mmap handles large offsets
- Test fallback by temporarily forcing `mMappedFile = nullptr`

### 8. Risk Assessment

- **Low risk:** mmap already proven in parser, same memory region reused
- **No behavioral change:** output bytes are identical (same data, same order)
- **Graceful fallback:** existing code path preserved when mmap unavailable
- **Memory:** mmap doesn't consume RAM — pages are demand-paged from disk by kernel
