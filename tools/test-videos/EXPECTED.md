# TTCut-ng Search Test Matrix — Expected Results

Six synthetic test videos under `cache/`, all sharing the same 120-second Tux timeline. Per-codec frame numbers differ because the timeline maps differently at 50fps vs 25fps and decode-vs-display order varies with B-frame configuration.

## Common Timeline (display-time-based)

| Time (s) | Content                                  | Has Logo? |
|----------|------------------------------------------|-----------|
| 0 – 30   | Tux moves L→R on BLUE                    | nein      |
| **30 – 31** | **BLACK 1** (1s pure black)            | nein      |
| 31 – 60  | Tux moves R→L on RED                     | nein      |
| **60 – 61** | **BLACK 2** (1s pure black)            | nein      |
| 61 – 90  | Tux on GREEN + Tux logo top-right        | **ja**    |
| **90 – 91** | **BLACK 3** (1s pure black)            | nein      |
| 91 – 120 | Checkerboard (testsrc2)                  | nein      |

## Per-File Frame-Number Tables

### tux_hevc4k_cra_test.265 (HEVC Main 10, 4K, 50fps progressive)

| Search                          | Expected Frame Range  | Notes |
|----------------------------------|----------------------|-------|
| Black-Frame Forward from 0       | 1500 ± bframes (decode order) | First IDR_N_LP at decode 1496 (display 1500) |
| Black-Frame Forward from 1600    | 3000 ± bframes       | Second BLACK GOP |
| Black-Frame Backward from 5999   | within 4500-4549     | Third BLACK GOP |
| Scene-Change Forward from 0      | near 1500            | BLUE→BLACK |
| Logo Forward from 0              | near 3050            | Logo appears at GREEN start |

### tux_h264_1080p_progressive_test.264 (H.264 High, 1080p, 50fps progressive)

| Search                          | Expected Frame Range  |
|----------------------------------|----------------------|
| Black-Frame Forward from 0       | 1500 ± bframes       |
| Black-Frame Forward from 1600    | 3000 ± bframes       |
| Scene-Change Forward from 0      | near 1500            |

### tux_h264_1080i_mbaff_test.264 (H.264 MBAFF, 1080i, 25fps display)

| Search                          | Expected Frame Range  | Notes |
|----------------------------------|----------------------|-------|
| Black-Frame Forward from 0       | 750 ± bframes        | 30s × 25fps = 750 |
| Black-Frame Forward from 800     | 1500 ± bframes       | 60s × 25fps |
| Scene-Change Forward from 0      | near 750             |       |

### tux_h264_1080i_paff_test.264 (H.264 PAFF, 1080i, 25fps display)

Same expectations as MBAFF. Synthesized via JM Reference Encoder; takes ~60min wall-clock per regeneration. If the JM encode failed or produced non-PAFF output, fall back to PAFF_Moon_Crash for PAFF-coverage validation.

### tux_mpeg2_576i_pal_test.m2v (MPEG-2 PAL DVB-SD, 576i, 25fps interlaced)

| Search                          | Expected Frame Range  |
|----------------------------------|----------------------|
| Black-Frame Forward from 0       | 750 ± bframes        |
| Black-Frame Forward from 800     | 1500 ± bframes       |
| Scene-Change Forward from 0      | near 750             |

### tux_mpeg2_720p_test.m2v (MPEG-2, 720p, 50fps progressive)

| Search                          | Expected Frame Range  |
|----------------------------------|----------------------|
| Black-Frame Forward from 0       | 1500 ± bframes       |
| Black-Frame Forward from 1600    | 3000 ± bframes       |
| Scene-Change Forward from 0      | near 1500            |

## Decode-Order vs Display-Order Note

TTCut-ng's `mFrameIndex` is in decode order (= packet order). For codecs with B-frames and open-GOP (HEVC, H.264 with bframes>0), the keyframe at the start of a GOP appears at a decode position that is up to `bframes` frames before its display position. The search returns the decode-order position. The result is correct for cutting (cut starts at the GOP boundary) but the frame number may surprise users who think in display order.

Example: HEVC test file BLACK 1 starts visually at display position 1500. The IDR_N_LP that begins that GOP is at decode position 1496. Search returns 1496.

## Test-Run Workflow

```bash
QT_QPA_PLATFORM=xcb /usr/local/src/TTCut-ng/ttcut-ng \
  /usr/local/src/TTCut-ng/tools/test-videos/cache/<file>.ttcut \
  > /usr/local/src/TTCut-ng/tools/test-videos/cache/search-<file>.log 2>&1

# After running searches:
grep -E 'BlackFrameSearch:|SceneChangeSearch:|LogoSearch:' search-<file>.log
grep -E 'BlackSearch result:|SceneSearch result:' search-<file>.log
```
