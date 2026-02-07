/*
 * Test program for TTESSmartCut with Project File Support
 * Reads cut points from TTCut .prj file and performs Smart Cut
 */

#include "../extern/ttessmartcut.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QXmlStreamReader>
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

// Cut segment from project file
struct CutSegment {
    int cutIn;
    int cutOut;
};

// Project data
struct ProjectData {
    QString videoFile;
    QStringList audioFiles;
    QList<CutSegment> cuts;
};

// Parse TTCut .prj file
bool parseProjectFile(const QString& prjFile, ProjectData& project)
{
    QFile file(prjFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "Error: Cannot open project file: " << prjFile.toStdString() << std::endl;
        return false;
    }

    QXmlStreamReader xml(&file);
    QString basePath = QFileInfo(prjFile).absolutePath() + "/";

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QString("Name")) {
                QString name = xml.readElementText();
                // Check if this is video or audio based on parent element
                if (project.videoFile.isEmpty()) {
                    // First Name element is video
                    if (QFileInfo(name).isAbsolute()) {
                        project.videoFile = name;
                    } else {
                        project.videoFile = basePath + name;
                    }
                }
            } else if (xml.name() == QString("Audio")) {
                // Read audio name
                while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QString("Audio"))) {
                    xml.readNext();
                    if (xml.tokenType() == QXmlStreamReader::StartElement && xml.name() == QString("Name")) {
                        QString audioName = xml.readElementText();
                        if (QFileInfo(audioName).isAbsolute()) {
                            project.audioFiles.append(audioName);
                        } else {
                            project.audioFiles.append(basePath + audioName);
                        }
                    }
                }
            } else if (xml.name() == QString("Cut")) {
                CutSegment seg;
                seg.cutIn = -1;
                seg.cutOut = -1;

                while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name() == QString("Cut"))) {
                    xml.readNext();
                    if (xml.tokenType() == QXmlStreamReader::StartElement) {
                        if (xml.name() == QString("CutIn")) {
                            seg.cutIn = xml.readElementText().toInt();
                        } else if (xml.name() == QString("CutOut")) {
                            seg.cutOut = xml.readElementText().toInt();
                        }
                    }
                }

                if (seg.cutIn >= 0 && seg.cutOut >= 0) {
                    project.cuts.append(seg);
                }
            }
        }
    }

    if (xml.hasError()) {
        std::cerr << "XML parse error: " << xml.errorString().toStdString() << std::endl;
        return false;
    }

    return !project.videoFile.isEmpty() && !project.cuts.isEmpty();
}

// Cut audio file using libav (stream-copy, no re-encoding)
bool cutAudio(const QString& inputFile, const QString& outputFile,
              const QList<QPair<double, double>>& keepList)
{
    AVFormatContext* inCtx = nullptr;
    AVFormatContext* outCtx = nullptr;
    int ret;

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

    int audioStreamIdx = -1;
    for (unsigned int i = 0; i < inCtx->nb_streams; i++) {
        if (inCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIdx = i;
            break;
        }
    }

    if (audioStreamIdx < 0) {
        avformat_close_input(&inCtx);
        return false;
    }

    AVStream* inStream = inCtx->streams[audioStreamIdx];

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

    avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
    outStream->codecpar->codec_tag = 0;
    outStream->time_base = inStream->time_base;

    if (!(outCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outCtx->pb, outputFile.toUtf8().constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            avformat_close_input(&inCtx);
            avformat_free_context(outCtx);
            return false;
        }
    }

    ret = avformat_write_header(outCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&inCtx);
        if (!(outCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outCtx->pb);
        avformat_free_context(outCtx);
        return false;
    }

    int64_t outputPts = 0;
    int packetCount = 0;

    // Process each segment
    for (const auto& segment : keepList) {
        double startTime = segment.first;
        double endTime = segment.second;

        // Seek to start
        int64_t startPts = (int64_t)(startTime * AV_TIME_BASE);
        av_seek_frame(inCtx, -1, startPts, AVSEEK_FLAG_BACKWARD);

        AVPacket* pkt = av_packet_alloc();
        int64_t segmentStartPts = -1;

        while (av_read_frame(inCtx, pkt) >= 0) {
            if (pkt->stream_index != audioStreamIdx) {
                av_packet_unref(pkt);
                continue;
            }

            double pktTime = pkt->pts * av_q2d(inStream->time_base);

            if (pktTime < startTime - 0.05) {
                av_packet_unref(pkt);
                continue;
            }

            if (pktTime > endTime + 0.05) {
                av_packet_unref(pkt);
                break;
            }

            if (segmentStartPts < 0) {
                segmentStartPts = pkt->pts;
            }

            // Adjust timestamps for continuous output
            int64_t relativePts = pkt->pts - segmentStartPts;
            pkt->pts = outputPts + av_rescale_q(relativePts, inStream->time_base, outStream->time_base);
            pkt->dts = pkt->pts;
            pkt->stream_index = 0;

            ret = av_interleaved_write_frame(outCtx, pkt);
            packetCount++;
            av_packet_unref(pkt);
        }

        // Update output PTS for next segment
        if (segmentStartPts >= 0) {
            double segmentDuration = endTime - startTime;
            outputPts += (int64_t)(segmentDuration / av_q2d(outStream->time_base));
        }

        av_packet_free(&pkt);
    }

    av_write_trailer(outCtx);

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

    int frameDurationNs = (int)(1000000000.0 / frameRate);
    args << "--default-duration" << QString("0:%1ns").arg(frameDurationNs);
    args << videoFile;

    for (const QString& audioFile : audioFiles) {
        args << audioFile;
    }

    std::cout << "  Muxing with mkvmerge..." << std::endl;

    QProcess proc;
    proc.start("mkvmerge", args);
    proc.waitForFinished(120000);

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
        std::cerr << "Usage: " << argv[0] << " <project.prj>" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Reads cut points from TTCut project file and performs Smart Cut." << std::endl;
        std::cerr << "Output: <video_basename>_smartcut.mkv" << std::endl;
        return 1;
    }

    QString prjFile = argv[1];

    if (!QFileInfo::exists(prjFile)) {
        std::cerr << "Error: Project file not found: " << prjFile.toStdString() << std::endl;
        return 1;
    }

    std::cout << "=============================================" << std::endl;
    std::cout << "TTESSmartCut Project Test" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "Project: " << prjFile.toStdString() << std::endl;
    std::cout << std::endl;

    // Parse project file
    ProjectData project;
    if (!parseProjectFile(prjFile, project)) {
        std::cerr << "Error: Failed to parse project file" << std::endl;
        return 1;
    }

    std::cout << "Video:  " << project.videoFile.toStdString() << std::endl;
    for (int i = 0; i < project.audioFiles.size(); i++) {
        std::cout << "Audio " << (i+1) << ": " << project.audioFiles[i].toStdString() << std::endl;
    }
    std::cout << "Cuts:   " << project.cuts.size() << " segment(s)" << std::endl;
    for (int i = 0; i < project.cuts.size(); i++) {
        std::cout << "  Segment " << (i+1) << ": Frame " << project.cuts[i].cutIn
                  << " -> " << project.cuts[i].cutOut << std::endl;
    }
    std::cout << std::endl;

    // Verify files exist
    if (!QFileInfo::exists(project.videoFile)) {
        std::cerr << "Error: Video file not found: " << project.videoFile.toStdString() << std::endl;
        return 1;
    }

    // Create output filenames
    QFileInfo fi(project.videoFile);
    QString baseName = fi.absolutePath() + "/" + fi.completeBaseName() + "_smartcut";
    QString videoOutputFile = baseName + "." + fi.suffix();
    QString mkvOutputFile = baseName + ".mkv";

    // Initialize Smart Cut engine
    std::cout << "Initializing Smart Cut engine..." << std::endl;
    TTESSmartCut smartCut;

    if (!smartCut.initialize(project.videoFile)) {
        std::cerr << "Error: " << smartCut.lastError().toStdString() << std::endl;
        return 1;
    }

    double frameRate = smartCut.frameRate();
    std::cout << "  Codec:      " << (smartCut.codecType() == NALU_CODEC_H264 ? "H.264" : "H.265") << std::endl;
    std::cout << "  Frames:     " << smartCut.frameCount() << std::endl;
    std::cout << "  GOPs:       " << smartCut.gopCount() << std::endl;
    std::cout << "  Frame rate: " << frameRate << " fps" << std::endl;
    std::cout << std::endl;

    // Build frame-based cut list
    QList<QPair<int, int>> cutFrames;
    QList<QPair<double, double>> cutTimes;  // For audio

    for (const CutSegment& seg : project.cuts) {
        cutFrames.append(qMakePair(seg.cutIn, seg.cutOut));
        cutTimes.append(qMakePair(seg.cutIn / frameRate, seg.cutOut / frameRate));
    }

    // Analyze cut points
    std::cout << "Analyzing cut points..." << std::endl;
    auto segments = smartCut.analyzeCutPoints(cutFrames);

    for (int i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        std::cout << "  Segment " << (i+1) << ":" << std::endl;
        std::cout << "    Frames: " << seg.startFrame << " - " << seg.endFrame << std::endl;
        std::cout << "    Re-encode at start: " << (seg.needsReencodeAtStart ? "YES" : "no") << std::endl;
        if (seg.needsReencodeAtStart) {
            std::cout << "    Re-encode: " << seg.reencodeStartFrame << " - " << seg.reencodeEndFrame << std::endl;
        }
        if (seg.streamCopyStartFrame >= 0) {
            std::cout << "    Stream-copy: " << seg.streamCopyStartFrame << " - " << seg.streamCopyEndFrame << std::endl;
        }
    }
    std::cout << std::endl;

    // Perform video Smart Cut
    std::cout << "Performing video Smart Cut..." << std::endl;
    if (!smartCut.smartCutFrames(videoOutputFile, cutFrames)) {
        std::cerr << "Error: " << smartCut.lastError().toStdString() << std::endl;
        return 1;
    }

    std::cout << std::endl;
    std::cout << "Video cut results:" << std::endl;
    std::cout << "  Stream-copied: " << smartCut.framesStreamCopied() << " frames" << std::endl;
    std::cout << "  Re-encoded:    " << smartCut.framesReencoded() << " frames" << std::endl;
    std::cout << "  Lossless:      " << QString::number(100.0 * smartCut.framesStreamCopied() /
                (smartCut.framesStreamCopied() + smartCut.framesReencoded()), 'f', 1).toStdString() << "%" << std::endl;
    std::cout << std::endl;

    // Cut audio tracks
    QStringList cutAudioFiles;
    if (!project.audioFiles.isEmpty()) {
        std::cout << "Cutting audio tracks..." << std::endl;

        for (int i = 0; i < project.audioFiles.size(); i++) {
            const QString& audioFile = project.audioFiles[i];
            if (!QFileInfo::exists(audioFile)) {
                std::cerr << "  Audio track " << (i+1) << " not found, skipping" << std::endl;
                continue;
            }

            QString audioExt = QFileInfo(audioFile).suffix();
            QString cutAudioFile = baseName + "_audio" + QString::number(i+1) + "." + audioExt;

            std::cout << "  Track " << (i+1) << ": " << QFileInfo(audioFile).fileName().toStdString() << std::endl;

            if (cutAudio(audioFile, cutAudioFile, cutTimes)) {
                cutAudioFiles.append(cutAudioFile);
            }
        }
        std::cout << std::endl;
    }

    // Mux video and audio
    std::cout << "Creating final MKV..." << std::endl;
    if (muxWithMkvmerge(videoOutputFile, cutAudioFiles, mkvOutputFile, frameRate)) {
        std::cout << std::endl;
        std::cout << "=============================================" << std::endl;
        std::cout << "SUCCESS!" << std::endl;
        std::cout << "=============================================" << std::endl;
        std::cout << "Output: " << mkvOutputFile.toStdString() << std::endl;
        std::cout << std::endl;
        std::cout << "To play: mpv \"" << mkvOutputFile.toStdString() << "\"" << std::endl;

        // Show file info
        QFileInfo outInfo(mkvOutputFile);
        std::cout << "Size:   " << (outInfo.size() / (1024*1024)) << " MB" << std::endl;

        // Cleanup temp files
        QFile::remove(videoOutputFile);
        for (const QString& f : cutAudioFiles) {
            QFile::remove(f);
        }
    } else {
        std::cerr << "Muxing failed!" << std::endl;
        return 1;
    }

    return 0;
}
