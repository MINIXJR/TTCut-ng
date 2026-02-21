# Preview Performance Optimization Design

**Goal:** Reduce H.264/H.265 preview generation time from ~30s to under 10s (3-4 cuts).

**Approach:** Measure first, then optimize the actual bottleneck.

## Current Pipeline (per preview clip)

1. ES Parsing (`TTNaluParser::parseFile()`) — once per preview series (shared)
2. SmartCut (`smartCutFrames()`) — analyze + stream-copy + optional re-encode
3. Audio Cut (`cutAudioStream()`) — lossless stream-copy via libav
4. MKV Mux (`mkvProvider.mux()`) — ES inputs → interleaved MKV

For N cuts: N+1 preview MKVs, each ~12.5 seconds of video.

## Phase 1: Timing Instrumentation

Add QElapsedTimer at 5 measurement points in `ttcutpreviewtask.cpp`:

1. **ES Parsing** — `sharedSmartCut->initialize()` (once)
2. **SmartCut** — `smartCutFrames()` (per clip)
3. **Audio Cut** — `cutAudioStream()` (per clip)
4. **MKV Mux** — `mkvProvider.mux()` (per clip)
5. **Clip Total** — sum of 2+3+4

Output format:
```
Preview: ES parsing: NNNNms
Preview clip 1/5: SmartCut NNms (X re-enc, Y stream-copy), Audio NNms, Mux NNms, Total NNms
Preview: Total time: NNNNms
```

No logic changes — only timing instrumentation.

## Phase 2: Optimize Based on Results

Candidate optimizations depending on which phase dominates:

| Bottleneck | Optimization |
|-----------|-------------|
| ES Parsing | Cache/reuse parser results from stream opening |
| SmartCut re-encode | "ultrafast" preset for preview, shorter clips |
| SmartCut stream-copy | Unlikely bottleneck, I/O bound |
| MKV Mux | Reduce avformat_open_input overhead |
| Audio Cut | Unlikely bottleneck |

## Constraints

- Preview must show transitions at cut points (SmartCut required)
- Quality at transitions must be visible (no pure stream-copy shortcut)
- Preview duration configurable (currently 25s total, 12.5s per side)
