# CLI-to-Library Migration Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace all external CLI tool calls (ffmpeg, mkvmerge, mplex) with direct libav C API calls to eliminate QProcess overhead and external binary dependencies.

**Architecture:** Modify `TTFFmpegWrapper::cutAudioStream()` to use libav stream-copy API instead of ffmpeg CLI. Rewrite `TTMkvMergeProvider` to use libav's matroska muxer. Remove dead code (`smartCutElementaryStream()`, `cutAndMuxElementaryStreams()`, `extractSegment()`). Replace mplex with libav mpegts muxer.

**Tech Stack:** libavformat, libavcodec, libavutil (already linked via `ttcut-ng.pro`)

**Build:** `make clean && qmake ttcut-ng.pro && make -j$(nproc)` (qmake dependency tracking is broken — always clean rebuild)

**Test:** Load Clouseau project, run preview (5 clips), burst shift, final cut → MKV output with A/V sync.

---

### Task 1: Remove Dead Code

Three functions in `ttffmpegwrapper.cpp` are never called from the active codebase. They contain 12+ `system()` calls, ffprobe queries, and legacy ffmpeg workflows. Remove them to reduce the migration surface.

**Files:**
- Modify: `extern/ttffmpegwrapper.cpp`
- Modify: `extern/ttffmpegwrapper.h`

**Step 1: Identify dead functions**

Search for callers of these functions. Expected: no callers outside the functions themselves.

```bash
grep -rn "extractSegment\|smartCutElementaryStream[^V]\|cutAndMuxElementaryStreams\|smartCutElementaryStreamV2" \
  --include="*.cpp" --include="*.h" data/ gui/ | grep -v "^extern/"
```

Expected output: No matches (only self-references in extern/).

**Step 2: Remove `extractSegment()` from header**

In `extern/ttffmpegwrapper.h`, remove:
```cpp
    bool extractSegment(const QString& outputFile, int startFrame, int endFrame, bool reencode = false);
```

**Step 3: Remove `extractSegment()` implementation**

In `extern/ttffmpegwrapper.cpp`, remove the function `TTFFmpegWrapper::extractSegment()` (lines ~1157-1219, approximately 62 lines including the ffmpeg QProcess call).

**Step 4: Remove `smartCutElementaryStream()` from header**

In `extern/ttffmpegwrapper.h`, remove:
```cpp
    bool smartCutElementaryStream(const QString& inputFile, const QString& audioFile,
                                   const QString& outputFile,
                                   const QList<QPair<double, double>>& cutList,
                                   double frameRate);
```

**Step 5: Remove `smartCutElementaryStream()` implementation**

In `extern/ttffmpegwrapper.cpp`, remove the entire function (lines ~1598-1966, approximately 370 lines including 12 `system()` calls, 2 ffprobe queries, multiple ffmpeg/mkvmerge invocations). This is the biggest single dead code removal.

**Step 6: Remove `smartCutElementaryStreamV2()` from header**

In `extern/ttffmpegwrapper.h`, remove:
```cpp
    bool smartCutElementaryStreamV2(const QString& inputFile, const QString& audioFile,
                                     const QString& outputFile,
                                     const QList<QPair<double, double>>& cutList,
                                     double frameRate);
```

**Step 7: Remove `smartCutElementaryStreamV2()` implementation**

In `extern/ttffmpegwrapper.cpp`, remove (lines ~1970-2067, approximately 100 lines). This wraps TTESSmartCut but is never called.

**Step 8: Remove `cutAndMuxElementaryStreams()` from header**

In `extern/ttffmpegwrapper.h`, remove:
```cpp
    bool cutAndMuxElementaryStreams(const QString& videoES, const QString& audioES,
                                    const QString& outputFile,
                                    const QList<QPair<double, double>>& cutList,
                                    double frameRate);
```

**Step 9: Remove `cutAndMuxElementaryStreams()` implementation**

In `extern/ttffmpegwrapper.cpp`, remove (lines ~2525-2683, approximately 160 lines). Contains mkvmerge and ffmpeg CLI calls.

**Step 10: Remove helper functions used only by dead code**

Check if `isTimestampIncluded()` and `getRangeMode()` (static helpers around lines 1224-1260) are still needed after dead code removal. Remove if only used by removed functions.

**Step 11: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && make -j$(nproc)
```

Expected: Clean build, no unresolved symbols. No functional change.

**Step 12: Commit**

```bash
git add extern/ttffmpegwrapper.cpp extern/ttffmpegwrapper.h
git commit -m "Remove dead smart cut and extract functions (~700 lines)

These functions were superseded by TTESSmartCut and are never called.
Removes 12+ system() calls, 2 ffprobe queries, and multiple ffmpeg/mkvmerge
CLI invocations."
```

---

### Task 2: Replace `cutAudioStream()` with libav Stream-Copy

The most frequently called CLI tool: `cutAudioStream()` in `ttffmpegwrapper.cpp` starts 1-3 ffmpeg processes per audio track per cut/preview. Replace with direct libav API.

**Files:**
- Modify: `extern/ttffmpegwrapper.cpp` (function `cutAudioStream`, lines ~2215-2390)
- Modify: `extern/ttffmpegwrapper.h` (no signature change needed)

**Step 1: Read the current `cutAudioStream()` implementation**

Read `extern/ttffmpegwrapper.cpp` lines 2215-2390 to understand the current CLI approach:
- Single segment: `ffmpeg -ss start -to end -c:a copy output`
- Multi-segment: N× ffmpeg per segment → temp files → `ffmpeg -f concat -c:a copy`

**Step 2: Replace with libav implementation**

Replace the entire function body of `cutAudioStream()` with libav API calls. The function signature stays the same:

```cpp
bool TTFFmpegWrapper::cutAudioStream(const QString& inputFile,
                                      const QString& outputFile,
                                      const QList<QPair<double, double>>& cutList)
```

New implementation:

```cpp
bool TTFFmpegWrapper::cutAudioStream(const QString& inputFile,
                                      const QString& outputFile,
                                      const QList<QPair<double, double>>& cutList)
{
    if (cutList.isEmpty()) {
        setError("Empty cut list");
        return false;
    }

    qDebug() << "cutAudioStream: libav stream-copy";
    qDebug() << "  Input:" << inputFile;
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  Segments:" << cutList.size();

    // Open input
    AVFormatContext* inFmtCtx = nullptr;
    int ret = avformat_open_input(&inFmtCtx, inputFile.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        setError(QString("Cannot open input: %1").arg(errbuf));
        return false;
    }

    ret = avformat_find_stream_info(inFmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&inFmtCtx);
        setError("Cannot find stream info");
        return false;
    }

    // Find audio stream
    int audioIdx = av_find_best_stream(inFmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIdx < 0) {
        avformat_close_input(&inFmtCtx);
        setError("No audio stream found");
        return false;
    }

    AVStream* inStream = inFmtCtx->streams[audioIdx];

    // Open output
    AVFormatContext* outFmtCtx = nullptr;
    ret = avformat_alloc_output_context2(&outFmtCtx, nullptr, nullptr,
                                          outputFile.toUtf8().constData());
    if (ret < 0 || !outFmtCtx) {
        avformat_close_input(&inFmtCtx);
        setError("Cannot create output context");
        return false;
    }

    // Create output stream with same codec parameters
    AVStream* outStream = avformat_new_stream(outFmtCtx, nullptr);
    if (!outStream) {
        avformat_close_input(&inFmtCtx);
        avformat_free_context(outFmtCtx);
        setError("Cannot create output stream");
        return false;
    }
    avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
    outStream->time_base = inStream->time_base;

    // Open output file
    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outFmtCtx->pb, outputFile.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            avformat_close_input(&inFmtCtx);
            avformat_free_context(outFmtCtx);
            setError("Cannot open output file");
            return false;
        }
    }

    ret = avformat_write_header(outFmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&inFmtCtx);
        avio_closep(&outFmtCtx->pb);
        avformat_free_context(outFmtCtx);
        setError("Cannot write output header");
        return false;
    }

    // Process each segment: seek + stream-copy packets within time range
    int64_t ptsOffset = 0;
    int64_t lastPts = 0;
    AVPacket pkt;

    for (int segIdx = 0; segIdx < cutList.size(); ++segIdx) {
        double startTime = cutList[segIdx].first;
        double endTime = cutList[segIdx].second;

        qDebug() << "  Segment" << segIdx << ":" << startTime << "->" << endTime;

        // Seek to just before the start time
        int64_t seekTs = static_cast<int64_t>(startTime * AV_TIME_BASE);
        av_seek_frame(inFmtCtx, -1, seekTs, AVSEEK_FLAG_BACKWARD);

        // Read and copy packets within the time range
        bool segmentStarted = false;
        while (av_read_frame(inFmtCtx, &pkt) >= 0) {
            if (pkt.stream_index != audioIdx) {
                av_packet_unref(&pkt);
                continue;
            }

            // Convert PTS to seconds
            double pktTime = pkt.pts * av_q2d(inStream->time_base);

            // Skip packets before start time
            if (pktTime < startTime - 0.001) {
                av_packet_unref(&pkt);
                continue;
            }

            // Stop at end time
            if (pktTime >= endTime) {
                av_packet_unref(&pkt);
                break;
            }

            if (!segmentStarted) {
                // Calculate PTS offset for this segment
                if (segIdx == 0) {
                    ptsOffset = -pkt.pts;  // First packet starts at PTS 0
                } else {
                    ptsOffset = lastPts - pkt.pts + 1;  // Continue from last segment
                }
                segmentStarted = true;
            }

            // Adjust PTS/DTS for output timeline
            pkt.pts += ptsOffset;
            pkt.dts += ptsOffset;
            pkt.stream_index = 0;  // Output has only one stream
            pkt.pos = -1;

            lastPts = pkt.pts;

            ret = av_write_frame(outFmtCtx, &pkt);
            av_packet_unref(&pkt);

            if (ret < 0) {
                qDebug() << "  Warning: av_write_frame failed for packet at" << pktTime;
            }
        }

        // Advance lastPts by one frame duration for gap between segments
        if (segmentStarted && inStream->codecpar->frame_size > 0) {
            int64_t frameDuration = av_rescale_q(inStream->codecpar->frame_size,
                AVRational{1, inStream->codecpar->sample_rate}, outStream->time_base);
            lastPts += frameDuration;
        } else {
            lastPts += 1;  // Minimal gap
        }
    }

    av_write_trailer(outFmtCtx);

    // Cleanup
    avformat_close_input(&inFmtCtx);
    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outFmtCtx->pb);
    avformat_free_context(outFmtCtx);

    // Verify output
    QFileInfo outInfo(outputFile);
    if (!outInfo.exists() || outInfo.size() == 0) {
        setError("Output file is empty or missing");
        return false;
    }

    qDebug() << "cutAudioStream: Complete, output size:" << outInfo.size() << "bytes";
    return true;
}
```

**Step 3: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && make -j$(nproc)
```

Expected: Clean build, no warnings.

**Step 4: Test with Clouseau project**

1. Start TTCut-ng, load Clouseau project
2. Run Preview → all 5 clips should have audio
3. Play each clip → audio should be in sync
4. Run final cut → MKV output with correct audio

**Step 5: Commit**

```bash
git add extern/ttffmpegwrapper.cpp
git commit -m "Replace cutAudioStream ffmpeg CLI with libav stream-copy

Eliminates 1-3 QProcess/ffmpeg invocations per audio cut.
Single-pass multi-segment support without temp files."
```

---

### Task 3: Replace `TTMkvMergeProvider` with libav Matroska Muxer

Replace the CLI-based mkvmerge muxer with direct libav matroska muxer. This eliminates the most complex external dependency.

**Files:**
- Modify: `extern/ttmkvmergeprovider.cpp` (complete rewrite of `mux()` and `buildCommandLine()`)
- Modify: `extern/ttmkvmergeprovider.h` (remove QProcess, add libav)
- No change to callers — the `mux()` API stays the same

**Step 1: Read current `TTMkvMergeProvider` implementation**

Read `extern/ttmkvmergeprovider.cpp` and `extern/ttmkvmergeprovider.h` to understand:
- `mux()` signature and behavior
- `buildCommandLine()` feature mapping
- Track options (default-duration, language, sync offset)
- Chapter file support
- Error handling (exit codes 0/1 allowed)

**Step 2: Add libav includes to header**

In `extern/ttmkvmergeprovider.h`, add:

```cpp
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/dict.h>
}
```

Remove `QProcess` member, add helper method declaration:

```cpp
// Remove: QProcess* mProcess;
// Add:
bool writePackets(AVFormatContext* outCtx, const QString& inputFile,
                  int outStreamIdx, int64_t syncOffsetMs);
```

**Step 3: Rewrite `mux()` with libav**

Replace the `mux()` function body:

```cpp
bool TTMkvMergeProvider::mux(const QString& outputFile,
                              const QString& videoFile,
                              const QStringList& audioFiles,
                              const QStringList& subtitleFiles)
{
    if (videoFile.isEmpty() || !QFile::exists(videoFile)) {
        setError(QString("Video file not found: %1").arg(videoFile));
        return false;
    }

    qDebug() << "TTMkvMergeProvider::mux (libav matroska)";
    qDebug() << "  Output:" << outputFile;
    qDebug() << "  Video:" << videoFile;

    // Allocate output context for Matroska
    AVFormatContext* outCtx = nullptr;
    int ret = avformat_alloc_output_context2(&outCtx, nullptr, "matroska",
                                              outputFile.toUtf8().constData());
    if (ret < 0 || !outCtx) {
        setError("Cannot create matroska output context");
        return false;
    }

    // Set title metadata
    QString title = decodeVdrName(QFileInfo(videoFile).completeBaseName());
    if (!title.isEmpty()) {
        av_dict_set(&outCtx->metadata, "title", title.toUtf8().constData(), 0);
    }

    // --- Add video stream ---
    AVFormatContext* videoInCtx = nullptr;
    ret = avformat_open_input(&videoInCtx, videoFile.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        avformat_free_context(outCtx);
        setError(QString("Cannot open video: %1").arg(videoFile));
        return false;
    }
    avformat_find_stream_info(videoInCtx, nullptr);

    int videoInIdx = av_find_best_stream(videoInCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoInIdx < 0) {
        avformat_close_input(&videoInCtx);
        avformat_free_context(outCtx);
        setError("No video stream in input");
        return false;
    }

    AVStream* videoOut = avformat_new_stream(outCtx, nullptr);
    avcodec_parameters_copy(videoOut->codecpar, videoInCtx->streams[videoInIdx]->codecpar);
    videoOut->time_base = videoInCtx->streams[videoInIdx]->time_base;

    // Apply default duration if set (overrides time_base for frame timing)
    if (mTrackOptions.contains(0) && !mTrackOptions[0].defaultDuration.isEmpty()) {
        QString dur = mTrackOptions[0].defaultDuration;
        // Parse "Xns" format
        if (dur.endsWith("ns")) {
            int64_t ns = dur.left(dur.length() - 2).toLongLong();
            if (ns > 0) {
                // Set r_frame_rate from nanosecond duration
                videoOut->r_frame_rate = av_make_q(1000000000, (int)ns);
                videoOut->avg_frame_rate = videoOut->r_frame_rate;
            }
        }
    }

    // --- Add audio streams ---
    struct AudioInput {
        AVFormatContext* fmtCtx;
        int streamIdx;
        int outStreamIdx;
    };
    QList<AudioInput> audioInputs;

    QRegularExpression langRe("_([a-z]{3})(?:_\\d+)?$");

    for (int i = 0; i < audioFiles.size(); i++) {
        const QString& audioFile = audioFiles[i];
        if (!QFile::exists(audioFile)) continue;

        AVFormatContext* audioInCtx = nullptr;
        ret = avformat_open_input(&audioInCtx, audioFile.toUtf8().constData(), nullptr, nullptr);
        if (ret < 0) continue;
        avformat_find_stream_info(audioInCtx, nullptr);

        int audioInIdx = av_find_best_stream(audioInCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (audioInIdx < 0) {
            avformat_close_input(&audioInCtx);
            continue;
        }

        AVStream* audioOut = avformat_new_stream(outCtx, nullptr);
        avcodec_parameters_copy(audioOut->codecpar, audioInCtx->streams[audioInIdx]->codecpar);
        audioOut->time_base = audioInCtx->streams[audioInIdx]->time_base;

        // Set language metadata
        QString lang;
        if (i < mAudioLanguages.size() && !mAudioLanguages[i].isEmpty()) {
            lang = mAudioLanguages[i];
        } else {
            QRegularExpressionMatch m = langRe.match(QFileInfo(audioFile).completeBaseName());
            if (m.hasMatch()) lang = m.captured(1);
        }
        if (!lang.isEmpty()) {
            av_dict_set(&audioOut->metadata, "language", lang.toUtf8().constData(), 0);
        }

        audioInputs.append({audioInCtx, audioInIdx, (int)outCtx->nb_streams - 1});
    }

    // --- Add subtitle streams ---
    struct SubInput {
        AVFormatContext* fmtCtx;
        int streamIdx;
        int outStreamIdx;
    };
    QList<SubInput> subInputs;

    for (int i = 0; i < subtitleFiles.size(); i++) {
        const QString& subFile = subtitleFiles[i];
        if (!QFile::exists(subFile)) continue;

        AVFormatContext* subInCtx = nullptr;
        ret = avformat_open_input(&subInCtx, subFile.toUtf8().constData(), nullptr, nullptr);
        if (ret < 0) continue;
        avformat_find_stream_info(subInCtx, nullptr);

        int subInIdx = av_find_best_stream(subInCtx, AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0);
        if (subInIdx < 0) {
            avformat_close_input(&subInCtx);
            continue;
        }

        AVStream* subOut = avformat_new_stream(outCtx, nullptr);
        avcodec_parameters_copy(subOut->codecpar, subInCtx->streams[subInIdx]->codecpar);
        subOut->time_base = subInCtx->streams[subInIdx]->time_base;

        // Language
        QString lang;
        if (i < mSubtitleLanguages.size() && !mSubtitleLanguages[i].isEmpty()) {
            lang = mSubtitleLanguages[i];
        }
        if (!lang.isEmpty()) {
            av_dict_set(&subOut->metadata, "language", lang.toUtf8().constData(), 0);
        }

        subInputs.append({subInCtx, subInIdx, (int)outCtx->nb_streams - 1});
    }

    // --- Add chapters from chapter file ---
    if (!mChapterFile.isEmpty() && QFile::exists(mChapterFile)) {
        QFile cf(mChapterFile);
        if (cf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QRegularExpression chapterRe("CHAPTER(\\d+)=(\\d{2}):(\\d{2}):(\\d{2})\\.(\\d{3})");
            QRegularExpression nameRe("CHAPTER(\\d+)NAME=(.+)");
            QTextStream in(&cf);
            QList<QPair<int64_t, QString>> chapters;

            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                QRegularExpressionMatch tm = chapterRe.match(line);
                if (tm.hasMatch()) {
                    int64_t ms = tm.captured(2).toInt() * 3600000LL
                               + tm.captured(3).toInt() * 60000LL
                               + tm.captured(4).toInt() * 1000LL
                               + tm.captured(5).toInt();
                    chapters.append({ms, ""});
                }
                QRegularExpressionMatch nm = nameRe.match(line);
                if (nm.hasMatch() && !chapters.isEmpty()) {
                    chapters.last().second = nm.captured(2).trimmed();
                }
            }

            outCtx->nb_chapters = chapters.size();
            outCtx->chapters = (AVChapter**)av_malloc(chapters.size() * sizeof(AVChapter*));
            for (int i = 0; i < chapters.size(); i++) {
                AVChapter* ch = (AVChapter*)av_mallocz(sizeof(AVChapter));
                ch->id = i;
                ch->time_base = {1, 1000};
                ch->start = chapters[i].first;
                ch->end = (i + 1 < chapters.size()) ? chapters[i + 1].first : INT64_MAX;
                if (!chapters[i].second.isEmpty()) {
                    av_dict_set(&ch->metadata, "title", chapters[i].second.toUtf8().constData(), 0);
                }
                outCtx->chapters[i] = ch;
            }
        }
    }

    // --- Open output and write header ---
    if (!(outCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outCtx->pb, outputFile.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            setError("Cannot open output file for writing");
            goto cleanup;
        }
    }

    ret = avformat_write_header(outCtx, nullptr);
    if (ret < 0) {
        setError("Cannot write matroska header");
        goto cleanup;
    }

    // --- Write video packets ---
    {
        AVPacket pkt;
        while (av_read_frame(videoInCtx, &pkt) >= 0) {
            if (pkt.stream_index == videoInIdx) {
                av_packet_rescale_ts(&pkt, videoInCtx->streams[videoInIdx]->time_base,
                                     videoOut->time_base);
                // Apply video sync offset
                if (mVideoSyncOffsetMs != 0) {
                    int64_t offsetTs = av_rescale_q(mVideoSyncOffsetMs,
                        {1, 1000}, videoOut->time_base);
                    pkt.pts += offsetTs;
                    pkt.dts += offsetTs;
                }
                pkt.stream_index = 0;
                pkt.pos = -1;
                av_interleaved_write_frame(outCtx, &pkt);
            }
            av_packet_unref(&pkt);
        }
    }

    // --- Write audio packets ---
    for (const auto& ai : audioInputs) {
        AVPacket pkt;
        while (av_read_frame(ai.fmtCtx, &pkt) >= 0) {
            if (pkt.stream_index == ai.streamIdx) {
                av_packet_rescale_ts(&pkt,
                    ai.fmtCtx->streams[ai.streamIdx]->time_base,
                    outCtx->streams[ai.outStreamIdx]->time_base);
                // Apply audio sync offset
                if (mAudioSyncOffsetMs != 0) {
                    int64_t offsetTs = av_rescale_q(mAudioSyncOffsetMs,
                        {1, 1000}, outCtx->streams[ai.outStreamIdx]->time_base);
                    pkt.pts += offsetTs;
                    pkt.dts += offsetTs;
                }
                pkt.stream_index = ai.outStreamIdx;
                pkt.pos = -1;
                av_interleaved_write_frame(outCtx, &pkt);
            }
            av_packet_unref(&pkt);
        }
    }

    // --- Write subtitle packets ---
    for (const auto& si : subInputs) {
        AVPacket pkt;
        while (av_read_frame(si.fmtCtx, &pkt) >= 0) {
            if (pkt.stream_index == si.streamIdx) {
                av_packet_rescale_ts(&pkt,
                    si.fmtCtx->streams[si.streamIdx]->time_base,
                    outCtx->streams[si.outStreamIdx]->time_base);
                pkt.stream_index = si.outStreamIdx;
                pkt.pos = -1;
                av_interleaved_write_frame(outCtx, &pkt);
            }
            av_packet_unref(&pkt);
        }
    }

    av_write_trailer(outCtx);
    qDebug() << "TTMkvMergeProvider::mux complete:" << outputFile;

cleanup:
    // Close all inputs
    avformat_close_input(&videoInCtx);
    for (auto& ai : audioInputs) avformat_close_input(&ai.fmtCtx);
    for (auto& si : subInputs) avformat_close_input(&si.fmtCtx);

    // Close output
    if (outCtx) {
        if (!(outCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outCtx->pb);
        avformat_free_context(outCtx);
    }

    return ret >= 0;
}
```

**Step 4: Remove `buildCommandLine()`, `mkvMergePath()`, `mkvMergeVersion()`, `isMkvMergeInstalled()`**

These are no longer needed. `isAvailable()` can return `true` unconditionally (libav is always linked).

**Step 5: Remove QProcess-based signal handlers**

Remove `onReadyReadStandardOutput()`, `onReadyReadStandardError()`, and the `mProcess` member.

**Step 6: Build and verify**

```bash
make clean && qmake ttcut-ng.pro && make -j$(nproc)
```

**Step 7: Test**

1. Load Clouseau project, run full preview → all 5 MKV clips generated
2. Final cut → MKV with video + 3 audio tracks + correct languages
3. Check A/V sync with mpv (should match original)
4. Verify chapters (if Clouseau project has chapter settings enabled)

**Step 8: Commit**

```bash
git add extern/ttmkvmergeprovider.cpp extern/ttmkvmergeprovider.h
git commit -m "Replace mkvmerge CLI with libav matroska muxer

TTMkvMergeProvider now uses avformat_alloc_output_context2('matroska')
instead of QProcess/mkvmerge. Supports: multi-audio, subtitles,
sync offset, chapters, language tags, VDR title decoding.
mkvmerge binary no longer required."
```

---

### Task 4: Replace `createTempMkvForPlayback()` in ttcurrentframe.cpp

The "Play" button in the Current Frame widget creates a temporary MKV via mkvmerge CLI. Replace with libav.

**Files:**
- Modify: `gui/ttcurrentframe.cpp` (function around line 708)

**Step 1: Read current implementation**

Read `gui/ttcurrentframe.cpp` lines 700-730 to understand the mkvmerge call.

**Step 2: Replace mkvmerge with libav muxer**

Use the same `TTMkvMergeProvider::mux()` (now libav-based from Task 3) instead of a direct mkvmerge QProcess call. If the code already uses `TTMkvMergeProvider`, no change needed. If it calls mkvmerge directly via QProcess, replace with `TTMkvMergeProvider::mux()`.

**Step 3: Build, test playback, commit**

```bash
make clean && qmake ttcut-ng.pro && make -j$(nproc)
```

Test: Load H.264 file → click Play in Current Frame → should create temp MKV and launch mpv.

```bash
git add gui/ttcurrentframe.cpp
git commit -m "Use TTMkvMergeProvider for temp MKV creation in playback"
```

---

### Task 5: Replace system() mkvmerge call in ttcutpreviewtask.cpp

**Files:**
- Modify: `data/ttcutpreviewtask.cpp` (line 240)

**Step 1: Read current implementation**

Check if line 240 uses `system(qPrintable(muxCommand))` with a mkvmerge command string.

**Step 2: Replace with TTMkvMergeProvider::mux()**

Replace the `system()` call with a `TTMkvMergeProvider` instance:

```cpp
TTMkvMergeProvider mkvProvider;
mkvProvider.setDefaultDuration("0", QString("%1ns").arg(frameDurationNs));
if (avOffsetMs != 0) mkvProvider.setAudioSyncOffset(avOffsetMs);
mkvProvider.mux(outputFile, videoFile, audioFiles, QStringList());
```

**Step 3: Build, test preview, commit**

---

### Task 6: Replace mplex with libav MPEG-TS Muxer (Optional/Legacy)

**Files:**
- Modify: `extern/ttmplexprovider.cpp`

**Step 1: Assess usage**

Check if mplex is still used for MPEG-2 cutting. If only used as fallback when mkvmerge is unavailable, and mkvmerge is now always available (via libav), this may be dead code.

**Step 2: If still needed, replace with libav mpegts muxer**

Same approach as MKV muxer but with `avformat_alloc_output_context2("mpegts", ...)`.

**Step 3: Build, test MPEG-2 workflow, commit**

---

### Task 7: Version Bump and Package Build

**Step 1: Bump version**

In `ttcut-ng.pro`, change `VERSION = 0.59.0` → `VERSION = 0.60.0`.

**Step 2: Build package**

```bash
echo "Version 0.60.0 - Replace CLI tools with libav API" | bash build-package.sh
git checkout -- debian/changelog
```

**Step 3: Commit version bump**

```bash
git add ttcut-ng.pro
git commit -m "Bump version to 0.60.0"
```

---

## Task Dependency Graph

```
Task 1 (Dead Code) → Task 2 (Audio) → Task 5 (Preview system())
                   → Task 3 (MKV)   → Task 4 (Playback MKV)
                                     → Task 5 (Preview system())
                   → Task 6 (mplex, optional)
                   → Task 7 (Version + Package)
```

Tasks 2 and 3 can run in parallel after Task 1.
Tasks 4 and 5 depend on Task 3.
Task 7 runs last.
