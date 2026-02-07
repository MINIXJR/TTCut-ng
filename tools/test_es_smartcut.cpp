/*
 * Test program for TTESSmartCut with Audio Support
 * Tests the ES Smart Cut engine with video and audio cutting
 */

#include "../extern/ttessmartcut.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

// Audio track info from .info file
struct AudioTrackInfo {
    QString filename;
    QString codec;  // "ac3", "mp2", etc.
};

// Parse .info file to find audio tracks
QList<AudioTrackInfo> parseInfoFile(const QString& videoFile)
{
    QList<AudioTrackInfo> audioTracks;

    QFileInfo fi(videoFile);
    QString infoFile = fi.absolutePath() + "/" + fi.completeBaseName();
    // Remove _video suffix if present
    if (infoFile.endsWith("_video")) {
        infoFile = infoFile.left(infoFile.length() - 6);
    }
    infoFile += ".info";

    QFile file(infoFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "  No .info file found: " << infoFile.toStdString() << std::endl;
        return audioTracks;
    }

    std::cout << "  Parsing .info file: " << infoFile.toStdString() << std::endl;

    QTextStream in(&file);
    QString basePath = fi.absolutePath() + "/";

    // Parse INI-style format:
    // audio_0_file=Petrocelli_audio_deu.mp2
    // audio_0_codec=mp2
    QMap<int, AudioTrackInfo> trackMap;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();

        // Skip comments and empty lines
        if (line.isEmpty() || line.startsWith("#") || line.startsWith("[")) {
            continue;
        }

        // Parse key=value
        int eqPos = line.indexOf('=');
        if (eqPos < 0) continue;

        QString key = line.left(eqPos).trimmed();
        QString value = line.mid(eqPos + 1).trimmed();

        // Parse audio_N_file and audio_N_codec
        if (key.startsWith("audio_") && key.contains("_file")) {
            // Extract track number: audio_0_file -> 0
            QStringList parts = key.split('_');
            if (parts.size() >= 3) {
                int trackNum = parts[1].toInt();
                trackMap[trackNum].filename = basePath + value;
            }
        } else if (key.startsWith("audio_") && key.contains("_codec")) {
            QStringList parts = key.split('_');
            if (parts.size() >= 3) {
                int trackNum = parts[1].toInt();
                trackMap[trackNum].codec = value.toLower();
            }
        }
    }

    // Convert map to list, filtering out incomplete entries
    for (auto it = trackMap.begin(); it != trackMap.end(); ++it) {
        const AudioTrackInfo& track = it.value();
        if (!track.filename.isEmpty() && !track.codec.isEmpty()) {
            if (QFileInfo::exists(track.filename)) {
                audioTracks.append(track);
                std::cout << "    Audio track " << it.key() << ": " << track.filename.toStdString()
                          << " (" << track.codec.toStdString() << ")" << std::endl;
            } else {
                std::cerr << "    Audio track " << it.key() << " not found: "
                          << track.filename.toStdString() << std::endl;
            }
        }
    }

    return audioTracks;
}

// Cut audio file using libav (stream-copy, no re-encoding)
bool cutAudio(const QString& inputFile, const QString& outputFile,
              double startTime, double endTime)
{
    AVFormatContext* inCtx = nullptr;
    AVFormatContext* outCtx = nullptr;
    int ret;

    // Open input
    ret = avformat_open_input(&inCtx, inputFile.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "    Failed to open audio input: " << inputFile.toStdString() << std::endl;
        return false;
    }

    ret = avformat_find_stream_info(inCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&inCtx);
        return false;
    }

    // Find audio stream
    int audioStreamIdx = -1;
    for (unsigned int i = 0; i < inCtx->nb_streams; i++) {
        if (inCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIdx = i;
            break;
        }
    }

    if (audioStreamIdx < 0) {
        std::cerr << "    No audio stream found" << std::endl;
        avformat_close_input(&inCtx);
        return false;
    }

    AVStream* inStream = inCtx->streams[audioStreamIdx];

    // Create output
    ret = avformat_alloc_output_context2(&outCtx, nullptr, nullptr, outputFile.toUtf8().constData());
    if (ret < 0) {
        avformat_close_input(&inCtx);
        return false;
    }

    AVStream* outStream = avformat_new_stream(outCtx, nullptr);
    if (!outStream) {
        avformat_close_input(&inCtx);
        avformat_free_context(outCtx);
        return false;
    }

    // Copy codec parameters
    avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
    outStream->codecpar->codec_tag = 0;
    outStream->time_base = inStream->time_base;

    // Open output file
    if (!(outCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outCtx->pb, outputFile.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            avformat_close_input(&inCtx);
            avformat_free_context(outCtx);
            return false;
        }
    }

    // Write header
    ret = avformat_write_header(outCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&inCtx);
        if (!(outCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outCtx->pb);
        avformat_free_context(outCtx);
        return false;
    }

    // Seek to start time
    int64_t startPts = (int64_t)(startTime * AV_TIME_BASE);
    av_seek_frame(inCtx, -1, startPts, AVSEEK_FLAG_BACKWARD);

    int64_t endPts = (int64_t)(endTime * AV_TIME_BASE);
    int64_t ptsOffset = -1;
    int packetCount = 0;

    AVPacket* pkt = av_packet_alloc();

    while (av_read_frame(inCtx, pkt) >= 0) {
        if (pkt->stream_index != audioStreamIdx) {
            av_packet_unref(pkt);
            continue;
        }

        // Convert packet timestamp to seconds
        double pktTime = pkt->pts * av_q2d(inStream->time_base);

        // Skip packets before start time
        if (pktTime < startTime - 0.1) {  // Small tolerance
            av_packet_unref(pkt);
            continue;
        }

        // Stop after end time
        if (pktTime > endTime + 0.1) {
            av_packet_unref(pkt);
            break;
        }

        // Record first PTS for offset calculation
        if (ptsOffset < 0) {
            ptsOffset = pkt->pts;
        }

        // Adjust timestamps to start from 0
        pkt->pts -= ptsOffset;
        pkt->dts -= ptsOffset;
        pkt->stream_index = 0;

        // Rescale timestamps
        av_packet_rescale_ts(pkt, inStream->time_base, outStream->time_base);

        ret = av_interleaved_write_frame(outCtx, pkt);
        if (ret < 0) {
            av_packet_unref(pkt);
            break;
        }

        packetCount++;
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);

    // Write trailer
    av_write_trailer(outCtx);

    // Cleanup
    avformat_close_input(&inCtx);
    if (!(outCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outCtx->pb);
    avformat_free_context(outCtx);

    std::cout << "    Audio cut complete: " << packetCount << " packets" << std::endl;
    return packetCount > 0;
}

// Mux video and audio using mkvmerge
bool muxWithMkvmerge(const QString& videoFile, const QStringList& audioFiles,
                     const QString& outputFile, double frameRate)
{
    QStringList args;
    args << "-o" << outputFile;

    // Video with frame duration
    int frameDurationNs = (int)(1000000000.0 / frameRate);
    args << "--default-duration" << QString("0:%1ns").arg(frameDurationNs);
    args << videoFile;

    // Audio tracks
    for (const QString& audioFile : audioFiles) {
        args << audioFile;
    }

    std::cout << "  Muxing with mkvmerge..." << std::endl;
    std::cout << "    Command: mkvmerge " << args.join(" ").toStdString() << std::endl;

    QProcess proc;
    proc.start("mkvmerge", args);
    proc.waitForFinished(60000);

    if (proc.exitCode() != 0) {
        std::cerr << "    mkvmerge failed: " << proc.readAllStandardError().toStdString() << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.264|input.265> [start_sec] [end_sec]" << std::endl;
        std::cerr << "  Default: cut from 10s to 20s" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Audio files are auto-detected from .info file in same directory." << std::endl;
        std::cerr << "Output: <basename>_smartcut_<start>-<end>.mkv (with audio)" << std::endl;
        std::cerr << "        <basename>_smartcut_<start>-<end>.<ext> (video only)" << std::endl;
        return 1;
    }

    QString inputFile = argv[1];
    double startTime = (argc > 2) ? QString(argv[2]).toDouble() : 10.0;
    double endTime = (argc > 3) ? QString(argv[3]).toDouble() : 20.0;

    if (!QFileInfo::exists(inputFile)) {
        std::cerr << "Error: File not found: " << inputFile.toStdString() << std::endl;
        return 1;
    }

    // Create output filenames
    QFileInfo fi(inputFile);
    QString baseName = fi.absolutePath() + "/" + fi.completeBaseName() +
                       "_smartcut_" + QString::number(startTime) + "-" +
                       QString::number(endTime);
    QString videoOutputFile = baseName + "." + fi.suffix();
    QString mkvOutputFile = baseName + ".mkv";

    std::cout << "=============================================" << std::endl;
    std::cout << "TTESSmartCut Test (with Audio)" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "Input:  " << inputFile.toStdString() << std::endl;
    std::cout << "Cut:    " << startTime << "s - " << endTime << "s" << std::endl;
    std::cout << std::endl;

    // Find audio tracks from .info file
    std::cout << "Looking for audio tracks..." << std::endl;
    QList<AudioTrackInfo> audioTracks = parseInfoFile(inputFile);
    std::cout << "  Found " << audioTracks.size() << " audio track(s)" << std::endl;
    std::cout << std::endl;

    // Create smart cut engine
    TTESSmartCut smartCut;

    // Initialize
    std::cout << "Initializing video parser..." << std::endl;
    if (!smartCut.initialize(inputFile)) {
        std::cerr << "Error: " << smartCut.lastError().toStdString() << std::endl;
        return 1;
    }

    std::cout << "  Codec:      " << (smartCut.codecType() == NALU_CODEC_H264 ? "H.264" : "H.265") << std::endl;
    std::cout << "  Frames:     " << smartCut.frameCount() << std::endl;
    std::cout << "  GOPs:       " << smartCut.gopCount() << std::endl;
    std::cout << "  Frame rate: " << smartCut.frameRate() << " fps" << std::endl;
    std::cout << std::endl;

    // Create cut list (segments to KEEP)
    QList<QPair<double, double>> cutList;
    cutList.append(qMakePair(startTime, endTime));

    // Analyze cut points
    std::cout << "Analyzing cut points..." << std::endl;
    int startFrame = qRound(startTime * smartCut.frameRate());
    int endFrame = qRound(endTime * smartCut.frameRate());

    QList<QPair<int, int>> frameList;
    frameList.append(qMakePair(startFrame, endFrame));

    auto segments = smartCut.analyzeCutPoints(frameList);

    for (int i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        std::cout << "  Segment " << i << ":" << std::endl;
        std::cout << "    Frames: " << seg.startFrame << " - " << seg.endFrame << std::endl;
        std::cout << "    Needs re-encode at start: " << (seg.needsReencodeAtStart ? "YES" : "no") << std::endl;
        if (seg.needsReencodeAtStart) {
            std::cout << "    Re-encode frames: " << seg.reencodeStartFrame << " - " << seg.reencodeEndFrame << std::endl;
        }
        if (seg.streamCopyStartFrame >= 0) {
            std::cout << "    Stream-copy frames: " << seg.streamCopyStartFrame << " - " << seg.streamCopyEndFrame << std::endl;
        }
    }
    std::cout << std::endl;

    // Perform the video cut
    std::cout << "Performing video smart cut..." << std::endl;
    if (!smartCut.smartCut(videoOutputFile, cutList)) {
        std::cerr << "Error: " << smartCut.lastError().toStdString() << std::endl;
        return 1;
    }

    std::cout << std::endl;
    std::cout << "Video cut results:" << std::endl;
    std::cout << "  Stream-copied frames: " << smartCut.framesStreamCopied() << std::endl;
    std::cout << "  Re-encoded frames:    " << smartCut.framesReencoded() << std::endl;
    std::cout << "  Output size:          " << smartCut.bytesWritten() << " bytes" << std::endl;
    std::cout << std::endl;

    // Cut audio tracks
    QStringList cutAudioFiles;
    if (!audioTracks.isEmpty()) {
        std::cout << "Cutting audio tracks..." << std::endl;

        for (int i = 0; i < audioTracks.size(); ++i) {
            const AudioTrackInfo& track = audioTracks[i];
            QString audioOutputFile = baseName + "_audio" + QString::number(i+1) + "." + track.codec;

            std::cout << "  Track " << (i+1) << ": " << track.filename.toStdString() << std::endl;

            if (cutAudio(track.filename, audioOutputFile, startTime, endTime)) {
                cutAudioFiles.append(audioOutputFile);
            } else {
                std::cerr << "    WARNING: Audio track " << (i+1) << " cutting failed" << std::endl;
            }
        }
        std::cout << std::endl;
    }

    // Mux video and audio
    if (!cutAudioFiles.isEmpty()) {
        std::cout << "Creating final MKV with audio..." << std::endl;
        if (muxWithMkvmerge(videoOutputFile, cutAudioFiles, mkvOutputFile, smartCut.frameRate())) {
            std::cout << std::endl;
            std::cout << "=============================================" << std::endl;
            std::cout << "SUCCESS!" << std::endl;
            std::cout << "=============================================" << std::endl;
            std::cout << "Output file: " << mkvOutputFile.toStdString() << std::endl;
            std::cout << std::endl;
            std::cout << "To play: mpv \"" << mkvOutputFile.toStdString() << "\"" << std::endl;

            // Cleanup temporary audio files
            for (const QString& f : cutAudioFiles) {
                QFile::remove(f);
            }
        } else {
            std::cerr << "WARNING: Muxing failed. Video-only output available." << std::endl;
            std::cout << "Video file: " << videoOutputFile.toStdString() << std::endl;
        }
    } else {
        // No audio - just wrap video in MKV
        std::cout << "No audio tracks - creating video-only MKV..." << std::endl;
        QStringList emptyAudio;
        if (muxWithMkvmerge(videoOutputFile, emptyAudio, mkvOutputFile, smartCut.frameRate())) {
            std::cout << std::endl;
            std::cout << "=============================================" << std::endl;
            std::cout << "SUCCESS (video only)!" << std::endl;
            std::cout << "=============================================" << std::endl;
            std::cout << "Output file: " << mkvOutputFile.toStdString() << std::endl;
            std::cout << std::endl;
            std::cout << "To play: mpv \"" << mkvOutputFile.toStdString() << "\"" << std::endl;
        }
    }

    return 0;
}
