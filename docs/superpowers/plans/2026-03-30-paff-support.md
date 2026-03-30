# PAFF (Separated Fields) Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix H.264 PAFF interlaced stream handling so field-pairs are counted as single frames, fixing navigation, Smart Cut, and frame rate.

**Architecture:** TTNaluParser gets SPS parsing + field-pair merging in `buildAccessUnits()`. TTFFmpegWrapper gets independent PAFF detection in `buildFrameIndex()` + field-pair merging. ttcut-demux gets interlaced frame rate correction. TTESSmartCut needs zero changes (interlaced encoder flags already work).

**Tech Stack:** C++ (Qt5), H.264 bitstream parsing (Exp-Golomb), libav/ffmpeg, bash (ttcut-demux)

**Build:** `make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)`

**Test file:** `/media/Daten/Video_Tmp/ProjectX_Temp/Moon_Crash_(2022).264` (PAFF, 50fps field-rate = 25fps frame-rate)

---

### Task 1: TTNaluParser — SPS Parsing (TTSpsInfo)

**Files:**
- Modify: `avstream/ttnaluparser.h` (add TTSpsInfo struct, new members)
- Modify: `avstream/ttnaluparser.cpp` (add parseH264SpsData, parsePpsData)

**Why:** To parse `field_pic_flag` from slice headers, we need `log2_max_frame_num_minus4` (to know bit-width of `frame_num`) and `frame_mbs_only_flag` (to know if `field_pic_flag` exists). Both come from the SPS. PPS maps PPS ID → SPS ID.

- [ ] **Step 1: Add TTSpsInfo struct and new members to ttnaluparser.h**

In `ttnaluparser.h`, add before `struct TTNalUnit`:

```cpp
// ----------------------------------------------------------------------------
// SPS info extracted for PAFF detection
// ----------------------------------------------------------------------------
struct TTSpsInfo {
    int spsId;
    int log2MaxFrameNumMinus4;    // needed to parse frame_num bit-width
    bool frameMbsOnlyFlag;        // false = may contain field-coded pictures
};
```

Add new fields to `TTNalUnit` struct (after `int ppsId`):

```cpp
    int frameNum;                // frame_num from slice header (-1 if not parsed)
    bool isField;                // field_pic_flag == 1
    bool isBottomField;          // bottom_field_flag == 1 (only valid if isField)
```

Add new field to `TTAccessUnit` struct (after `int gopIndex`):

```cpp
    bool isFieldCoded;           // true if AU was merged from two field pictures
```

Add new private members to `TTNaluParser` class:

```cpp
    // SPS/PPS info for PAFF detection
    QMap<int, TTSpsInfo> mSpsInfoMap;   // SPS ID -> SPS info
    QMap<int, int> mPpsToSpsMap;        // PPS ID -> SPS ID
    bool mIsPAFF;                       // true if any field-coded slices found

    // SPS/PPS parsing for PAFF
    void parseH264SpsData(const QByteArray& data);
    void parseH264PpsData(const QByteArray& data);
```

Add public accessors:

```cpp
    // PAFF detection
    bool isPAFF() const { return mIsPAFF; }
```

- [ ] **Step 2: Initialize new fields in constructor and parseNalUnit**

In `ttnaluparser.cpp`, in the constructor `TTNaluParser::TTNaluParser()`, add to initializer list:

```cpp
    , mIsPAFF(false)
```

In `TTNaluParser::closeFile()`, add after `mVPSList.clear()`:

```cpp
    mSpsInfoMap.clear();
    mPpsToSpsMap.clear();
    mIsPAFF = false;
```

In `TTNaluParser::parseNalUnit()`, add initialization for new TTNalUnit fields (after `nal.ppsId = -1`):

```cpp
    nal.frameNum = -1;
    nal.isField = false;
    nal.isBottomField = false;
```

In `buildAccessUnits()`, add initialization for new TTAccessUnit field. Where `currentAU.gopIndex = 0;` is set at the top, add:

```cpp
    currentAU.isFieldCoded = false;
```

And where `currentAU.gopIndex = currentGop;` is set in the new-AU block (inside the `if (isAUStart && !currentAU.nalIndices.isEmpty())` block), add:

```cpp
            currentAU.isFieldCoded = false;
```

- [ ] **Step 3: Implement parseH264SpsData()**

In `ttnaluparser.cpp`, add before `parseH264SliceHeader()`:

```cpp
// ----------------------------------------------------------------------------
// Parse H.264 SPS to extract fields needed for PAFF detection
// H.264 spec Table 7-3: Sequence Parameter Set RBSP syntax
// We need: sps_id, log2_max_frame_num_minus4, frame_mbs_only_flag
// ----------------------------------------------------------------------------
void TTNaluParser::parseH264SpsData(const QByteArray& data)
{
    if (data.size() < 5) return;

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    int bitPos = 8;  // Skip NAL header byte

    // profile_idc (8 bits)
    int profileIdc = static_cast<int>(readBits(bytes, data.size(), bitPos, 8));

    // constraint_set0..5_flags (6 bits) + reserved (2 bits) = 8 bits
    readBits(bytes, data.size(), bitPos, 8);

    // level_idc (8 bits)
    readBits(bytes, data.size(), bitPos, 8);

    // seq_parameter_set_id (ue(v))
    int spsId = static_cast<int>(readExpGolombUE(bytes, data.size(), bitPos));

    // For High profile and above, parse chroma_format_idc etc.
    if (profileIdc == 100 || profileIdc == 110 || profileIdc == 122 ||
        profileIdc == 244 || profileIdc == 44  || profileIdc == 83  ||
        profileIdc == 86  || profileIdc == 118 || profileIdc == 128 ||
        profileIdc == 138 || profileIdc == 139 || profileIdc == 134 ||
        profileIdc == 135) {

        // chroma_format_idc (ue(v))
        int chromaFormatIdc = static_cast<int>(readExpGolombUE(bytes, data.size(), bitPos));
        if (chromaFormatIdc == 3) {
            // separate_colour_plane_flag (1 bit)
            readBits(bytes, data.size(), bitPos, 1);
        }

        // bit_depth_luma_minus8 (ue(v))
        readExpGolombUE(bytes, data.size(), bitPos);
        // bit_depth_chroma_minus8 (ue(v))
        readExpGolombUE(bytes, data.size(), bitPos);

        // qpprime_y_zero_transform_bypass_flag (1 bit)
        readBits(bytes, data.size(), bitPos, 1);

        // seq_scaling_matrix_present_flag (1 bit)
        uint32_t scalingMatrixPresent = readBits(bytes, data.size(), bitPos, 1);
        if (scalingMatrixPresent) {
            int numLists = (chromaFormatIdc != 3) ? 8 : 12;
            for (int i = 0; i < numLists; i++) {
                uint32_t listPresent = readBits(bytes, data.size(), bitPos, 1);
                if (listPresent) {
                    // Skip scaling list (delta_scale values)
                    int sizeOfList = (i < 6) ? 16 : 64;
                    int lastScale = 8;
                    int nextScale = 8;
                    for (int j = 0; j < sizeOfList; j++) {
                        if (nextScale != 0) {
                            int deltaScale = readExpGolombSE(bytes, data.size(), bitPos);
                            nextScale = (lastScale + deltaScale + 256) % 256;
                        }
                        lastScale = (nextScale == 0) ? lastScale : nextScale;
                    }
                }
            }
        }
    }

    // log2_max_frame_num_minus4 (ue(v))
    int log2MaxFrameNumMinus4 = static_cast<int>(readExpGolombUE(bytes, data.size(), bitPos));

    // pic_order_cnt_type (ue(v))
    int pocType = static_cast<int>(readExpGolombUE(bytes, data.size(), bitPos));
    if (pocType == 0) {
        // log2_max_pic_order_cnt_lsb_minus4 (ue(v))
        readExpGolombUE(bytes, data.size(), bitPos);
    } else if (pocType == 1) {
        // delta_pic_order_always_zero_flag (1 bit)
        readBits(bytes, data.size(), bitPos, 1);
        // offset_for_non_ref_pic (se(v))
        readExpGolombSE(bytes, data.size(), bitPos);
        // offset_for_top_to_bottom_field (se(v))
        readExpGolombSE(bytes, data.size(), bitPos);
        // num_ref_frames_in_pic_order_cnt_cycle (ue(v))
        int numRefFrames = static_cast<int>(readExpGolombUE(bytes, data.size(), bitPos));
        for (int i = 0; i < numRefFrames; i++) {
            readExpGolombSE(bytes, data.size(), bitPos);  // offset_for_ref_frame[i]
        }
    }

    // max_num_ref_frames (ue(v))
    readExpGolombUE(bytes, data.size(), bitPos);
    // gaps_in_frame_num_value_allowed_flag (1 bit)
    readBits(bytes, data.size(), bitPos, 1);
    // pic_width_in_mbs_minus1 (ue(v))
    readExpGolombUE(bytes, data.size(), bitPos);
    // pic_height_in_map_units_minus1 (ue(v))
    readExpGolombUE(bytes, data.size(), bitPos);

    // frame_mbs_only_flag (1 bit) — THIS IS WHAT WE NEED
    bool frameMbsOnlyFlag = (readBits(bytes, data.size(), bitPos, 1) == 1);

    TTSpsInfo info;
    info.spsId = spsId;
    info.log2MaxFrameNumMinus4 = log2MaxFrameNumMinus4;
    info.frameMbsOnlyFlag = frameMbsOnlyFlag;
    mSpsInfoMap[spsId] = info;

    if (!frameMbsOnlyFlag) {
        qDebug() << "  SPS" << spsId << ": frame_mbs_only_flag=0 (may contain field pictures)"
                 << "log2_max_frame_num_minus4=" << log2MaxFrameNumMinus4;
    }
}
```

- [ ] **Step 4: Implement parseH264PpsData()**

In `ttnaluparser.cpp`, add after `parseH264SpsData()`:

```cpp
// ----------------------------------------------------------------------------
// Parse H.264 PPS to extract PPS ID → SPS ID mapping
// H.264 spec Table 7-9: Picture Parameter Set RBSP syntax
// ----------------------------------------------------------------------------
void TTNaluParser::parseH264PpsData(const QByteArray& data)
{
    if (data.size() < 2) return;

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    int bitPos = 8;  // Skip NAL header byte

    // pic_parameter_set_id (ue(v))
    int ppsId = static_cast<int>(readExpGolombUE(bytes, data.size(), bitPos));
    // seq_parameter_set_id (ue(v))
    int spsId = static_cast<int>(readExpGolombUE(bytes, data.size(), bitPos));

    mPpsToSpsMap[ppsId] = spsId;
}
```

- [ ] **Step 5: Call SPS/PPS parsing from parseFile()**

In `ttnaluparser.cpp`, in `parseFile()`, add after the `parseNalUnit()` call and the existing SPS/PPS/VPS tracking block (around line 215-216). Replace the existing parameter set tracking:

```cpp
            // Track parameter sets (deduplicated after parse loop when sizes are set)
            if (nal.isSPS) mSPSList.append(nalCount - 1);
            if (nal.isPPS) mPPSList.append(nalCount - 1);
            if (nal.isVPS) mVPSList.append(nalCount - 1);
```

with:

```cpp
            // Track parameter sets (deduplicated after parse loop when sizes are set)
            if (nal.isSPS) {
                mSPSList.append(nalCount - 1);
                // Parse SPS for PAFF detection (H.264 only)
                if (mCodecType == NALU_CODEC_H264) {
                    mFile.seek(nal.dataOffset);
                    QByteArray spsData = mFile.read(qMin(nal.dataSize, (int64_t)512));
                    parseH264SpsData(spsData);
                }
            }
            if (nal.isPPS) {
                mPPSList.append(nalCount - 1);
                // Parse PPS for PPS→SPS mapping (H.264 only)
                if (mCodecType == NALU_CODEC_H264) {
                    mFile.seek(nal.dataOffset);
                    QByteArray ppsData = mFile.read(qMin(nal.dataSize, (int64_t)64));
                    parseH264PpsData(ppsData);
                }
            }
            if (nal.isVPS) mVPSList.append(nalCount - 1);
```

- [ ] **Step 6: Build and verify SPS/PPS parsing**

Run:
```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```
Expected: Compiles without errors. Run TTCut-ng with Moon_Crash test file and check debug output for `SPS ... frame_mbs_only_flag=0` message.

- [ ] **Step 7: Commit**

```bash
git add avstream/ttnaluparser.h avstream/ttnaluparser.cpp
git commit -m "Add H.264 SPS/PPS parsing for PAFF detection

Parse frame_mbs_only_flag and log2_max_frame_num_minus4 from SPS,
and PPS-to-SPS mapping from PPS. These are prerequisites for
field_pic_flag parsing in slice headers."
```

---

### Task 2: TTNaluParser — field_pic_flag Parsing in Slice Headers

**Files:**
- Modify: `avstream/ttnaluparser.cpp` (extend parseH264SliceHeader)

**Why:** With SPS info available, we can now parse `frame_num`, `field_pic_flag`, and `bottom_field_flag` from H.264 slice headers.

- [ ] **Step 1: Extend parseH264SliceHeader()**

In `ttnaluparser.cpp`, replace the existing `parseH264SliceHeader()` method:

```cpp
bool TTNaluParser::parseH264SliceHeader(const QByteArray& data, TTNalUnit& nal)
{
    if (data.size() < 3) return false;

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    int bitPos = 8;  // Skip NAL header byte

    // first_mb_in_slice (ue(v))
    nal.firstMbInSlice = static_cast<int>(readExpGolombUE(bytes, data.size(), bitPos));

    // slice_type (ue(v))
    uint32_t sliceType = readExpGolombUE(bytes, data.size(), bitPos);
    // Normalize slice type (0-4 and 5-9 mean the same thing)
    if (sliceType > 4) sliceType -= 5;
    nal.sliceType = static_cast<int>(sliceType);

    // pic_parameter_set_id (ue(v))
    nal.ppsId = static_cast<int>(readExpGolombUE(bytes, data.size(), bitPos));

    // Look up SPS via PPS → SPS chain for frame_num bit-width and field info
    int spsId = mPpsToSpsMap.value(nal.ppsId, -1);
    if (spsId < 0 || !mSpsInfoMap.contains(spsId)) {
        return true;  // Can't parse further without SPS info — still valid
    }

    const TTSpsInfo& sps = mSpsInfoMap[spsId];

    // frame_num — u(log2_max_frame_num_minus4 + 4) bits
    int frameNumBits = sps.log2MaxFrameNumMinus4 + 4;
    nal.frameNum = static_cast<int>(readBits(bytes, data.size(), bitPos, frameNumBits));

    // field_pic_flag — only present if frame_mbs_only_flag == 0
    if (!sps.frameMbsOnlyFlag) {
        nal.isField = (readBits(bytes, data.size(), bitPos, 1) == 1);
        if (nal.isField) {
            nal.isBottomField = (readBits(bytes, data.size(), bitPos, 1) == 1);
        }
    }

    return true;
}
```

- [ ] **Step 2: Build and verify field_pic_flag parsing**

Run:
```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```
Expected: Compiles without errors. The field info is now parsed but not yet used for AU merging (that comes in Task 3).

- [ ] **Step 3: Commit**

```bash
git add avstream/ttnaluparser.cpp
git commit -m "Parse field_pic_flag and bottom_field_flag from H.264 slice headers

Extends parseH264SliceHeader() to read frame_num (using
log2_max_frame_num bit-width from SPS) and field_pic_flag
(when frame_mbs_only_flag == 0). Prerequisite for field-pair
merging in buildAccessUnits()."
```

---

### Task 3: TTNaluParser — Field-Pair Merging in buildAccessUnits()

**Files:**
- Modify: `avstream/ttnaluparser.cpp` (extend buildAccessUnits with Pass 2)

**Why:** After Pass 1 (existing code), consecutive top+bottom field AUs with the same `frame_num` must be merged into a single logical AU.

- [ ] **Step 1: Add field-pair merging pass to buildAccessUnits()**

In `ttnaluparser.cpp`, in `buildAccessUnits()`, add a second pass **after** the existing `qDebug() << "  Built" << mAccessUnits.size() << "access units";` line (around line 691), and **before** the closing `}` of the function:

```cpp
    // Pass 2: Merge field pairs (PAFF)
    // If SPS has frame_mbs_only_flag == 0, check for consecutive top+bottom
    // field AUs with the same frame_num and merge them.
    bool hasFieldSlices = false;
    if (mCodecType == NALU_CODEC_H264) {
        // Check if any SPS allows field coding
        bool spsAllowsFields = false;
        for (auto it = mSpsInfoMap.constBegin(); it != mSpsInfoMap.constEnd(); ++it) {
            if (!it.value().frameMbsOnlyFlag) {
                spsAllowsFields = true;
                break;
            }
        }

        if (spsAllowsFields) {
            // Merge consecutive top+bottom field AUs
            int mergeCount = 0;
            int i = 0;
            while (i < mAccessUnits.size() - 1) {
                TTAccessUnit& topAU = mAccessUnits[i];
                TTAccessUnit& botAU = mAccessUnits[i + 1];

                // Check: top field must have isField=true, isBottomField=false
                // Check: bottom field must have isField=true, isBottomField=true
                // Check: both must share the same frame_num
                bool topIsField = false;
                bool botIsField = false;
                int topFrameNum = -1;
                int botFrameNum = -1;

                // Get field info from first slice NAL in each AU
                for (int idx : topAU.nalIndices) {
                    if (mNalUnits[idx].isSlice) {
                        topIsField = mNalUnits[idx].isField;
                        topFrameNum = mNalUnits[idx].frameNum;
                        break;
                    }
                }
                for (int idx : botAU.nalIndices) {
                    if (mNalUnits[idx].isSlice) {
                        botIsField = mNalUnits[idx].isField;
                        botFrameNum = mNalUnits[idx].frameNum;
                        break;
                    }
                }

                // Check bottom_field_flag for correct field ordering
                bool topIsTop = false;
                bool botIsBot = false;
                for (int idx : topAU.nalIndices) {
                    if (mNalUnits[idx].isSlice && mNalUnits[idx].isField) {
                        topIsTop = !mNalUnits[idx].isBottomField;
                        break;
                    }
                }
                for (int idx : botAU.nalIndices) {
                    if (mNalUnits[idx].isSlice && mNalUnits[idx].isField) {
                        botIsBot = mNalUnits[idx].isBottomField;
                        break;
                    }
                }

                if (topIsField && botIsField && topIsTop && botIsBot &&
                    topFrameNum >= 0 && topFrameNum == botFrameNum) {
                    // Merge: append bottom AU's NAL indices to top AU
                    topAU.nalIndices.append(botAU.nalIndices);
                    topAU.endOffset = botAU.endOffset;
                    topAU.isFieldCoded = true;
                    hasFieldSlices = true;

                    // Inherit keyframe/IDR from bottom field if set
                    if (botAU.isKeyframe) topAU.isKeyframe = true;
                    if (botAU.isIDR) topAU.isIDR = true;

                    // Remove the bottom field AU
                    mAccessUnits.removeAt(i + 1);
                    mergeCount++;
                    // Don't increment i — check next pair starting from current
                } else {
                    i++;
                }
            }

            if (mergeCount > 0) {
                // Re-index AU decode indices
                for (int j = 0; j < mAccessUnits.size(); j++) {
                    mAccessUnits[j].index = j;
                    mAccessUnits[j].decodeIndex = j;
                }
                mIsPAFF = true;
                qDebug() << "  PAFF detected: merged" << mergeCount << "field pairs"
                         << "-> " << mAccessUnits.size() << "frames";
            }
        }
    }

    // Double validation: PAFF only if both SPS and actual slices agree
    if (!hasFieldSlices) {
        mIsPAFF = false;
    }
```

- [ ] **Step 2: Build and test with Moon_Crash file**

Run:
```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```
Expected: Compiles without errors. Run TTCut-ng with Moon_Crash test file. Debug output should show:
- `PAFF detected: merged NNNN field pairs -> MMMMM frames`
- Frame count should be approximately half of the previous count (~158k instead of ~317k)

- [ ] **Step 3: Commit**

```bash
git add avstream/ttnaluparser.cpp
git commit -m "Merge PAFF field-pairs in buildAccessUnits()

Pass 2 detects consecutive top+bottom field AUs sharing the same
frame_num and merges them into single logical AUs. Double validation
ensures PAFF is only activated when both SPS (frame_mbs_only_flag=0)
and actual slice headers (field_pic_flag=1) agree."
```

---

### Task 4: TTFFmpegWrapper — Independent PAFF Detection and Field Merging

**Files:**
- Modify: `extern/ttffmpegwrapper.h` (add PAFF members, TTFieldInfo)
- Modify: `extern/ttffmpegwrapper.cpp` (extend buildFrameIndex, skipCurrentFrame)

**Why:** TTFFmpegWrapper must independently detect PAFF and merge field packets, without depending on TTNaluParser (which isn't always available, e.g. for container files).

- [ ] **Step 1: Add PAFF declarations to ttffmpegwrapper.h**

In `ttffmpegwrapper.h`, add `isFieldCoded` to `TTFrameInfo` struct (after `int frameIndex`):

```cpp
    bool isFieldCoded;      // true if merged from two PAFF field packets
```

Add public accessor to `TTFFmpegWrapper` class (after `int frameCount()`):

```cpp
    bool isPAFF() const { return mIsPAFF; }
```

Add private members (after `bool mAnalysisMode`):

```cpp
    bool mIsPAFF;                       // PAFF stream detected
    int mH264Log2MaxFrameNum;           // from SPS, for frame_num parsing
    bool mH264FrameMbsOnlyFlag;         // from SPS, true = no field coding

    // H.264 PAFF field info from packet data
    struct TTFieldInfo {
        bool isField;        // field_pic_flag
        bool isBottomField;  // bottom_field_flag
        int frameNum;        // frame_num from slice header
    };
    TTFieldInfo parseH264FieldInfoFromPacket(const uint8_t* data, int size);
    void parseH264SpsFromExtradata(const uint8_t* data, int size);
```

- [ ] **Step 2: Initialize PAFF fields in constructor**

In `ttffmpegwrapper.cpp`, in the `TTFFmpegWrapper` constructor, initialize (find existing initializer list and add):

```cpp
    mIsPAFF = false;
    mH264Log2MaxFrameNum = 4;      // default: log2_max_frame_num_minus4=0 → 4 bits
    mH264FrameMbsOnlyFlag = true;  // default: frame-only (no fields)
```

In `closeFile()`, add reset:

```cpp
    mIsPAFF = false;
    mH264Log2MaxFrameNum = 4;
    mH264FrameMbsOnlyFlag = true;
```

- [ ] **Step 3: Implement parseH264SpsFromExtradata()**

In `ttffmpegwrapper.cpp`, add before `buildFrameIndex()`:

```cpp
// ----------------------------------------------------------------------------
// Parse H.264 SPS from codec extradata to get PAFF-relevant fields
// Uses same parsing logic as TTNaluParser::parseH264SpsData()
// ----------------------------------------------------------------------------
void TTFFmpegWrapper::parseH264SpsFromExtradata(const uint8_t* data, int size)
{
    if (!data || size < 5) return;

    // Scan for SPS NAL unit (type 7)
    int nalStart = -1;
    for (int pos = 0; pos < size - 4; pos++) {
        if (data[pos] == 0 && data[pos+1] == 0) {
            int start = -1;
            if (data[pos+2] == 1) start = pos + 3;
            else if (data[pos+2] == 0 && pos + 3 < size && data[pos+3] == 1) start = pos + 4;
            if (start >= 0 && start < size && (data[start] & 0x1F) == 7) {
                nalStart = start;
                break;
            }
        }
    }
    if (nalStart < 0) return;

    const uint8_t* sps = data + nalStart;
    int spsSize = size - nalStart;
    int bitPos = 8;  // Skip NAL header

    // profile_idc (8 bits)
    int profileIdc = static_cast<int>(TTNaluParser::readBits(sps, spsSize, bitPos, 8));
    // constraint flags + reserved (8 bits)
    TTNaluParser::readBits(sps, spsSize, bitPos, 8);
    // level_idc (8 bits)
    TTNaluParser::readBits(sps, spsSize, bitPos, 8);
    // sps_id (ue)
    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);

    // High profile chroma/scaling
    if (profileIdc == 100 || profileIdc == 110 || profileIdc == 122 ||
        profileIdc == 244 || profileIdc == 44  || profileIdc == 83  ||
        profileIdc == 86  || profileIdc == 118 || profileIdc == 128 ||
        profileIdc == 138 || profileIdc == 139 || profileIdc == 134 ||
        profileIdc == 135) {
        int chromaFormatIdc = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos));
        if (chromaFormatIdc == 3)
            TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);  // bit_depth_luma
        TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);  // bit_depth_chroma
        TTNaluParser::readBits(sps, spsSize, bitPos, 1);      // qpprime
        uint32_t scalingPresent = TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        if (scalingPresent) {
            int numLists = (chromaFormatIdc != 3) ? 8 : 12;
            for (int i = 0; i < numLists; i++) {
                if (TTNaluParser::readBits(sps, spsSize, bitPos, 1)) {
                    int listSize = (i < 6) ? 16 : 64;
                    int lastScale = 8, nextScale = 8;
                    for (int j = 0; j < listSize; j++) {
                        if (nextScale != 0) {
                            int delta = TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
                            nextScale = (lastScale + delta + 256) % 256;
                        }
                        lastScale = (nextScale == 0) ? lastScale : nextScale;
                    }
                }
            }
        }
    }

    // log2_max_frame_num_minus4 (ue)
    mH264Log2MaxFrameNum = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos)) + 4;

    // Skip POC fields
    int pocType = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos));
    if (pocType == 0) {
        TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);
    } else if (pocType == 1) {
        TTNaluParser::readBits(sps, spsSize, bitPos, 1);
        TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
        TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
        int n = static_cast<int>(TTNaluParser::readExpGolombUE(sps, spsSize, bitPos));
        for (int i = 0; i < n; i++)
            TTNaluParser::readExpGolombSE(sps, spsSize, bitPos);
    }

    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);  // max_num_ref_frames
    TTNaluParser::readBits(sps, spsSize, bitPos, 1);       // gaps_in_frame_num
    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);   // pic_width
    TTNaluParser::readExpGolombUE(sps, spsSize, bitPos);   // pic_height

    // frame_mbs_only_flag (1 bit)
    mH264FrameMbsOnlyFlag = (TTNaluParser::readBits(sps, spsSize, bitPos, 1) == 1);

    if (!mH264FrameMbsOnlyFlag) {
        qDebug() << "FFmpegWrapper SPS: frame_mbs_only_flag=0, log2_max_frame_num=" << mH264Log2MaxFrameNum;
    }
}
```

**Note:** This uses `TTNaluParser::readBits/readExpGolombUE/readExpGolombSE` which are static methods. They must be made accessible. In `ttnaluparser.h`, move these three methods from `private:` to `public:` (they are stateless utility functions).

- [ ] **Step 4: Make Exp-Golomb helpers public in ttnaluparser.h**

In `ttnaluparser.h`, move these three declarations from the `private:` section to the `public:` section:

```cpp
    // Exp-Golomb decoding (for slice header parsing)
    static uint32_t readExpGolombUE(const uint8_t* data, int dataSize, int& bitPos);
    static int32_t readExpGolombSE(const uint8_t* data, int dataSize, int& bitPos);
    static uint32_t readBits(const uint8_t* data, int dataSize, int& bitPos, int numBits);
```

- [ ] **Step 5: Implement parseH264FieldInfoFromPacket()**

In `ttffmpegwrapper.cpp`, add after `parseH264SpsFromExtradata()`:

```cpp
// ----------------------------------------------------------------------------
// Parse field_pic_flag and frame_num from H.264 packet data
// Used by buildFrameIndex() to detect PAFF field packets
// ----------------------------------------------------------------------------
TTFFmpegWrapper::TTFieldInfo TTFFmpegWrapper::parseH264FieldInfoFromPacket(const uint8_t* data, int size)
{
    TTFieldInfo result = {false, false, -1};
    if (!data || size < 4 || mH264FrameMbsOnlyFlag) return result;

    // Scan for VCL NAL start code (type 1 or 5)
    int nalStart = -1;
    for (int pos = 0; pos < size - 4; pos++) {
        if (data[pos] == 0 && data[pos+1] == 0) {
            int start = -1;
            if (data[pos+2] == 1) start = pos + 3;
            else if (data[pos+2] == 0 && pos + 3 < size && data[pos+3] == 1) start = pos + 4;
            if (start >= 0 && start < size) {
                uint8_t nalType = data[start] & 0x1F;
                if (nalType == 1 || nalType == 5) {
                    nalStart = start;
                    break;
                }
            }
        }
    }

    // Fallback: raw NAL data without start code
    if (nalStart < 0 && size >= 3) {
        uint8_t nalType = data[0] & 0x1F;
        if (nalType == 1 || nalType == 5) nalStart = 0;
    }
    if (nalStart < 0) return result;

    const uint8_t* nal = data + nalStart;
    int nalSize = size - nalStart;
    int bitPos = 8;  // Skip NAL header

    // first_mb_in_slice (ue) — skip
    TTNaluParser::readExpGolombUE(nal, nalSize, bitPos);
    // slice_type (ue) — skip
    TTNaluParser::readExpGolombUE(nal, nalSize, bitPos);
    // pps_id (ue) — skip
    TTNaluParser::readExpGolombUE(nal, nalSize, bitPos);

    // frame_num — u(log2_max_frame_num) bits
    result.frameNum = static_cast<int>(TTNaluParser::readBits(nal, nalSize, bitPos, mH264Log2MaxFrameNum));

    // field_pic_flag (1 bit) — only if frame_mbs_only_flag == 0
    result.isField = (TTNaluParser::readBits(nal, nalSize, bitPos, 1) == 1);
    if (result.isField) {
        result.isBottomField = (TTNaluParser::readBits(nal, nalSize, bitPos, 1) == 1);
    }

    return result;
}
```

- [ ] **Step 6: Extend buildFrameIndex() for PAFF field merging**

In `ttffmpegwrapper.cpp`, in `buildFrameIndex()`, add SPS extraction after opening. Find the line `qDebug() << "Building frame index for stream" << videoStreamIndex;` (around line 476). Add right after it:

```cpp
    // Parse SPS from extradata for PAFF detection (H.264 only)
    AVCodecID codecId = mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id;
    if (codecId == AV_CODEC_ID_H264) {
        uint8_t* extradata = mFormatCtx->streams[videoStreamIndex]->codecpar->extradata;
        int extradataSize = mFormatCtx->streams[videoStreamIndex]->codecpar->extradata_size;
        if (extradata && extradataSize > 0) {
            parseH264SpsFromExtradata(extradata, extradataSize);
        }
    }
```

Now modify the main packet loop. The current code appends one `TTFrameInfo` per packet. For PAFF, we need to buffer top-field packets and merge with subsequent bottom-field. Replace the body of the `while (av_read_frame(mFormatCtx, packet) >= 0)` loop (lines 479-537) with:

```cpp
    // Pending top-field info (for PAFF merging)
    TTFrameInfo pendingTopField;
    bool hasPendingTopField = false;

    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            TTFrameInfo frameInfo;
            frameInfo.pts = packet->pts;
            frameInfo.dts = packet->dts;
            frameInfo.fileOffset = packet->pos;
            frameInfo.packetSize = packet->size;
            frameInfo.isKeyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
            frameInfo.frameIndex = frameIndex;
            frameInfo.isFieldCoded = false;

            // Determine frame type
            if (frameInfo.isKeyframe) {
                frameInfo.frameType = AV_PICTURE_TYPE_I;
                // New GOP starts at keyframe
                if (frameIndex > 0) {
                    currentGOP++;
                }
            } else {
                // Parse slice_type from packet data for B-frame detection
                AVCodecID codecId = mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id;
                if (codecId == AV_CODEC_ID_H264) {
                    int sliceType = TTNaluParser::parseH264SliceTypeFromPacket(
                        packet->data, packet->size);
                    if (sliceType == H264::SLICE_B)
                        frameInfo.frameType = AV_PICTURE_TYPE_B;
                    else if (sliceType == H264::SLICE_I)
                        frameInfo.frameType = AV_PICTURE_TYPE_I;
                    else
                        frameInfo.frameType = AV_PICTURE_TYPE_P;
                } else if (codecId == AV_CODEC_ID_HEVC) {
                    int sliceType = TTNaluParser::parseH265SliceTypeFromPacket(
                        packet->data, packet->size);
                    if (sliceType == H265::SLICE_B)
                        frameInfo.frameType = AV_PICTURE_TYPE_B;
                    else if (sliceType == H265::SLICE_I)
                        frameInfo.frameType = AV_PICTURE_TYPE_I;
                    else
                        frameInfo.frameType = AV_PICTURE_TYPE_P;
                } else {
                    frameInfo.frameType = AV_PICTURE_TYPE_P;
                }
            }

            frameInfo.gopIndex = currentGOP;

            // PAFF field merging: check if this is a field packet
            bool merged = false;
            if (!mH264FrameMbsOnlyFlag && codecId == AV_CODEC_ID_H264) {
                TTFieldInfo fi = parseH264FieldInfoFromPacket(packet->data, packet->size);
                if (fi.isField) {
                    if (!fi.isBottomField) {
                        // Top field — buffer it, wait for bottom field
                        if (hasPendingTopField) {
                            // Previous top field had no matching bottom — emit as standalone
                            mFrameIndex.append(pendingTopField);
                            frameIndex++;
                        }
                        pendingTopField = frameInfo;
                        hasPendingTopField = true;
                        merged = true;  // Don't append yet
                    } else if (hasPendingTopField && fi.frameNum == pendingTopField.frameIndex) {
                        // Bottom field matching pending top field — merge
                        // Note: pendingTopField.frameIndex was set to frameIndex at the time,
                        // but we need to compare frame_num, not frameIndex. Store frame_num in
                        // a temporary variable instead.
                        // Actually: we need the frame_num from the top field too. Let's fix this.
                    }
                }
            }

            if (!merged) {
                // Flush any pending top field first
                if (hasPendingTopField) {
                    mFrameIndex.append(pendingTopField);
                    frameIndex++;
                    hasPendingTopField = false;
                }
                mFrameIndex.append(frameInfo);
                frameIndex++;
            }

            // Progress reporting
            int64_t progress = (frameIndex * 100) / estimatedFrames;
            if (progress != lastProgress && progress <= 100) {
                emit progressChanged(static_cast<int>(progress),
                    tr("Indexing frame %1...").arg(frameIndex));
                lastProgress = progress;
            }
        }

        av_packet_unref(packet);
    }

    // Flush last pending top field
    if (hasPendingTopField) {
        mFrameIndex.append(pendingTopField);
    }
```

Wait — the frame_num comparison in the merge logic above is incomplete. The field info needs to carry frame_num through. Let me revise the approach. We need to store the parsed `frame_num` from the top field and compare with the bottom field's. Since `TTFrameInfo` doesn't carry `frame_num`, use a local variable:

Replace the entire loop body with this cleaner version:

```cpp
    // PAFF field merging state
    TTFrameInfo pendingTopField;
    int pendingTopFrameNum = -1;
    bool hasPendingTopField = false;
    bool isH264 = (mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id == AV_CODEC_ID_H264);
    bool canHaveFields = isH264 && !mH264FrameMbsOnlyFlag;

    while (av_read_frame(mFormatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            TTFrameInfo frameInfo;
            frameInfo.pts = packet->pts;
            frameInfo.dts = packet->dts;
            frameInfo.fileOffset = packet->pos;
            frameInfo.packetSize = packet->size;
            frameInfo.isKeyframe = (packet->flags & AV_PKT_FLAG_KEY) != 0;
            frameInfo.frameIndex = 0;  // set below
            frameInfo.isFieldCoded = false;

            // Determine frame type from slice header
            if (frameInfo.isKeyframe) {
                frameInfo.frameType = AV_PICTURE_TYPE_I;
                if (frameIndex > 0) currentGOP++;
            } else {
                AVCodecID cid = mFormatCtx->streams[videoStreamIndex]->codecpar->codec_id;
                if (cid == AV_CODEC_ID_H264) {
                    int st = TTNaluParser::parseH264SliceTypeFromPacket(packet->data, packet->size);
                    frameInfo.frameType = (st == H264::SLICE_B) ? AV_PICTURE_TYPE_B :
                                          (st == H264::SLICE_I) ? AV_PICTURE_TYPE_I :
                                                                   AV_PICTURE_TYPE_P;
                } else if (cid == AV_CODEC_ID_HEVC) {
                    int st = TTNaluParser::parseH265SliceTypeFromPacket(packet->data, packet->size);
                    frameInfo.frameType = (st == H265::SLICE_B) ? AV_PICTURE_TYPE_B :
                                          (st == H265::SLICE_I) ? AV_PICTURE_TYPE_I :
                                                                   AV_PICTURE_TYPE_P;
                } else {
                    frameInfo.frameType = AV_PICTURE_TYPE_P;
                }
            }
            frameInfo.gopIndex = currentGOP;

            // PAFF field detection and merging
            TTFieldInfo fi = {false, false, -1};
            if (canHaveFields) {
                fi = parseH264FieldInfoFromPacket(packet->data, packet->size);
            }

            if (fi.isField && !fi.isBottomField) {
                // Top field — buffer it
                if (hasPendingTopField) {
                    // Orphan top field — emit standalone
                    pendingTopField.frameIndex = frameIndex;
                    mFrameIndex.append(pendingTopField);
                    frameIndex++;
                }
                pendingTopField = frameInfo;
                pendingTopFrameNum = fi.frameNum;
                hasPendingTopField = true;
            } else if (fi.isField && fi.isBottomField && hasPendingTopField &&
                       fi.frameNum == pendingTopFrameNum) {
                // Bottom field matching top field — merge into one frame
                pendingTopField.frameIndex = frameIndex;
                pendingTopField.packetSize += frameInfo.packetSize;
                pendingTopField.isFieldCoded = true;
                // Keyframe flag: inherit from either field
                if (frameInfo.isKeyframe) pendingTopField.isKeyframe = true;
                mFrameIndex.append(pendingTopField);
                frameIndex++;
                hasPendingTopField = false;
                mIsPAFF = true;
            } else {
                // Non-field packet (or mismatched bottom field)
                if (hasPendingTopField) {
                    pendingTopField.frameIndex = frameIndex;
                    mFrameIndex.append(pendingTopField);
                    frameIndex++;
                    hasPendingTopField = false;
                }
                frameInfo.frameIndex = frameIndex;
                mFrameIndex.append(frameInfo);
                frameIndex++;
            }

            // Progress reporting
            int64_t progress = (frameIndex * 100) / estimatedFrames;
            if (progress != lastProgress && progress <= 100) {
                emit progressChanged(static_cast<int>(progress),
                    tr("Indexing frame %1...").arg(frameIndex));
                lastProgress = progress;
            }
        }

        av_packet_unref(packet);
    }

    // Flush last pending top field
    if (hasPendingTopField) {
        pendingTopField.frameIndex = frameIndex;
        mFrameIndex.append(pendingTopField);
    }
```

- [ ] **Step 7: Fix frame rate when PAFF detected**

In `buildFrameIndex()`, after the packet loop and after the existing `// Seek back to beginning` block, find the section that calculates PTS from frame rate for ES files (around line 560, starting with `if (!mFrameIndex.isEmpty() && mFrameIndex[0].pts == AV_NOPTS_VALUE)`). Before this section, add PAFF frame rate correction:

```cpp
    // PAFF frame rate correction: if field-rate was reported, halve it
    if (mIsPAFF) {
        qDebug() << "PAFF stream detected: " << mFrameIndex.size() << "frames (merged from field pairs)";
    }
```

Then, inside the PTS calculation section, where `frameRate` is obtained from the `.info` file (around the `if (esInfo.isLoaded() && esInfo.frameRate() > 0)` block), add PAFF correction after setting frameRate:

```cpp
        // Correct field rate to frame rate for PAFF streams
        if (mIsPAFF && frameRate > 30) {
            qDebug() << "PAFF: correcting frame rate from" << frameRate << "to" << frameRate / 2.0;
            frameRate /= 2.0;
        }
```

- [ ] **Step 8: Adjust skipCurrentFrame() for PAFF**

In `ttffmpegwrapper.cpp`, in `skipCurrentFrame()`, the current implementation sends one packet and expects one frame. For PAFF, the decoder needs 2 packets per frame. Replace the method:

```cpp
bool TTFFmpegWrapper::skipCurrentFrame()
{
    if (!mFormatCtx || !mVideoCodecCtx) return false;

    if (!mDecodedFrame) {
        mDecodedFrame = av_frame_alloc();
        if (!mDecodedFrame) return false;
    }

    AVPacket* packet = av_packet_alloc();
    if (!packet) return false;

    // For PAFF: need to send 2 field packets per frame
    int packetsNeeded = mIsPAFF ? 2 : 1;
    bool decoded = false;

    for (int p = 0; p < packetsNeeded && !decoded; p++) {
        bool packetSent = false;
        while (av_read_frame(mFormatCtx, packet) >= 0) {
            if (packet->stream_index == mVideoStreamIndex) {
                int ret = avcodec_send_packet(mVideoCodecCtx, packet);
                if (ret < 0) {
                    av_packet_unref(packet);
                    continue;
                }
                av_packet_unref(packet);
                packetSent = true;

                // Try to receive frame after each packet
                int recvRet = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
                if (recvRet == 0) {
                    decoded = true;
                }
                break;
            }
            av_packet_unref(packet);
        }

        if (!packetSent && !decoded) break;
    }

    // EOF drain: flush decoder pipeline to retrieve buffered frames
    if (!decoded) {
        int ret = avcodec_send_packet(mVideoCodecCtx, nullptr);
        int recvRet = avcodec_receive_frame(mVideoCodecCtx, mDecodedFrame);
        if (recvRet == 0) {
            decoded = true;
            mDecoderDrained = true;
        } else {
            qDebug() << "skipCurrentFrame: EOF drain exhausted"
                     << "send_packet=" << ret << "receive_frame=" << recvRet;
        }
    }

    av_packet_free(&packet);
    return decoded;
}
```

- [ ] **Step 9: Build and test**

Run:
```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```
Expected: Compiles without errors. Open Moon_Crash in TTCut-ng:
- Frame count in status bar should be ~158k (not ~317k)
- Navigation should show correct content for each frame index
- Frame rate should be 25fps

- [ ] **Step 10: Commit**

```bash
git add extern/ttffmpegwrapper.h extern/ttffmpegwrapper.cpp avstream/ttnaluparser.h
git commit -m "Add independent PAFF detection to TTFFmpegWrapper

buildFrameIndex() parses SPS from extradata for frame_mbs_only_flag,
detects field packets via field_pic_flag, and merges consecutive
top+bottom field packets into single frame entries. Frame rate
corrected from field-rate to frame-rate when PAFF detected.
skipCurrentFrame() sends 2 packets per frame for PAFF streams."
```

---

### Task 5: ttcut-demux — Frame Rate Correction for Interlaced

**Files:**
- Modify: `tools/ttcut-demux/ttcut-demux`

**Why:** ffprobe reports field rate (50fps) for PAFF streams. The `.info` file should contain the frame rate (25fps).

- [ ] **Step 1: Add interlaced detection after frame rate parsing**

In `tools/ttcut-demux/ttcut-demux`, find the frame rate parsing section (around line 716-718):

```bash
    # Parse frame rate fraction (e.g., "25/1" or "30000/1001")
    FRAME_RATE_NUM=$(echo "$FRAME_RATE" | cut -d'/' -f1)
    FRAME_RATE_DEN=$(echo "$FRAME_RATE" | cut -d'/' -f2)
    FRAME_RATE_DEN="${FRAME_RATE_DEN:-1}"  # Default to 1 if not fractional
```

Add right after this block:

```bash
    # Detect interlaced: ffprobe reports field rate (e.g. 50fps) for interlaced,
    # but we need frame rate (25fps) for TTCut-ng
    if $ES_MODE; then
        FIELD_ORDER=$(ffprobe -v error -select_streams v:0 \
            -show_entries stream=field_order \
            -of default=noprint_wrappers=1:nokey=1 "$OUTPUT_VIDEO" 2>/dev/null)

        if [[ -n "$FIELD_ORDER" && "$FIELD_ORDER" != "progressive" && "$FIELD_ORDER" != "unknown" ]]; then
            ORIG_RATE="$FRAME_RATE_NUM/$FRAME_RATE_DEN"
            FRAME_RATE_NUM=$((FRAME_RATE_NUM / 2))
            FRAME_RATE="${FRAME_RATE_NUM}/${FRAME_RATE_DEN}"
            info "  Interlaced ($FIELD_ORDER): corrected frame rate from $ORIG_RATE to $FRAME_RATE"
        fi
    fi
```

- [ ] **Step 2: Test with a PAFF file**

Run `ttcut-demux` on a known PAFF recording and verify the `.info` file contains `frame_rate=25/1` instead of `frame_rate=50/1`.

- [ ] **Step 3: Commit**

```bash
git add tools/ttcut-demux/ttcut-demux
git commit -m "Correct frame rate for interlaced streams in ttcut-demux

Detect field_order via ffprobe. When interlaced, halve the frame rate
so .info file contains frame rate (25fps) instead of field rate (50fps).
Fixes PAFF streams where ffprobe incorrectly reports 50fps."
```

---

### Task 6: TTESInfo — PAFF Frame Rate Fallback

**Files:**
- Modify: `avstream/ttesinfo.h` (add correctFrameRateForPAFF method)
- Modify: `avstream/ttesinfo.cpp` (implement method)
- Modify: `extern/ttessmartcut.cpp` (call correction after parser confirms PAFF)
- Modify: `extern/ttffmpegwrapper.cpp` (call correction after PAFF detection)

**Why:** Old `.info` files generated before the ttcut-demux fix still contain 50fps for PAFF streams. TTCut-ng must correct this when the parser confirms PAFF.

- [ ] **Step 1: Add correctFrameRateForPAFF() to TTESInfo**

In `avstream/ttesinfo.h`, add public method:

```cpp
    // Correct field rate to frame rate for PAFF streams (old .info files)
    void correctFrameRateForPAFF();
```

In `avstream/ttesinfo.cpp`, add:

```cpp
// ----------------------------------------------------------------------------
// Correct field rate to frame rate for PAFF streams
// Old .info files may contain 50fps (field rate) instead of 25fps (frame rate)
// Called when TTNaluParser or TTFFmpegWrapper confirms PAFF
// ----------------------------------------------------------------------------
void TTESInfo::correctFrameRateForPAFF()
{
    // Only correct if rate looks like field rate (>30fps with denominator 1)
    if (mFrameRateNum > 30 && mFrameRateDen == 1) {
        int oldRate = mFrameRateNum;
        mFrameRateNum /= 2;
        qDebug() << "TTESInfo: PAFF frame rate correction:" << oldRate
                 << "/" << mFrameRateDen << " -> " << mFrameRateNum << "/" << mFrameRateDen;
    }
}
```

- [ ] **Step 2: Call correction from TTESSmartCut**

In `extern/ttessmartcut.cpp`, in `openFile()`, after `mParser.parseFile()` succeeds and after the frame rate is set from `.info` (around line 106 `mFrameRate = frameRate;`), add:

```cpp
    // Correct frame rate if PAFF detected with old .info file
    if (mParser.isPAFF() && mFrameRate > 30) {
        qDebug() << "TTESSmartCut: PAFF detected, correcting frame rate from" << mFrameRate
                 << "to" << mFrameRate / 2.0;
        mFrameRate /= 2.0;
    }
```

- [ ] **Step 3: Build and verify**

Run:
```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```
Expected: Compiles without errors. With old Moon_Crash .info file (50fps), TTCut-ng should auto-correct to 25fps.

- [ ] **Step 4: Commit**

```bash
git add avstream/ttesinfo.h avstream/ttesinfo.cpp extern/ttessmartcut.cpp extern/ttffmpegwrapper.cpp
git commit -m "Add PAFF frame rate fallback for old .info files

TTESInfo::correctFrameRateForPAFF() halves field rate when >30fps.
Called from TTESSmartCut and TTFFmpegWrapper when parser confirms PAFF.
Ensures old .info files with 50fps are corrected to 25fps."
```

---

### Task 7: Integration Test — Moon_Crash End-to-End

**Files:** None (manual test)

**Why:** Verify that all components work together correctly with the real PAFF test file.

- [ ] **Step 1: Full rebuild**

```bash
make clean && qmake ttcut-ng.pro && bear -- make -j$(nproc)
```

- [ ] **Step 2: Open Moon_Crash and verify frame count**

Run TTCut-ng, open `/media/Daten/Video_Tmp/ProjectX_Temp/Moon_Crash_(2022).264`.
Check debug output for:
- `PAFF detected: merged NNNN field pairs -> ~158k frames`
- Frame rate shown as 25fps (not 50fps)
- Frame count in status bar ~158k (not ~317k)

- [ ] **Step 3: Navigation test**

Navigate to various frame positions. Each frame should show unique content (not the same field twice). Use I-frame buttons to jump between keyframes — keyframe count should be approximately half of previous.

- [ ] **Step 4: Cut and preview test**

Set a CutIn and CutOut. Preview the cut. The first frame in preview should match the CutIn frame shown in the main window. No ~2x offset.

- [ ] **Step 5: Final cut test**

Execute the final cut. Open the output MKV in mpv:
- Frame rate should be 25fps
- Content at cut points should match what was selected in TTCut-ng
- A/V sync should be correct

- [ ] **Step 6: Regression test — progressive H.264**

Open a known progressive H.264 file (e.g. a typical DVB HD recording). Verify:
- Frame count identical to before
- No `PAFF detected` message in debug output
- Navigation, preview, and cut work exactly as before

- [ ] **Step 7: Report results to user**

Present test results. If all pass, the implementation is complete.
