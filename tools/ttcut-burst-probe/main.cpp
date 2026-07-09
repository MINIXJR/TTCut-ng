/*
 * ttcut-burst-probe - call TTFFmpegWrapper::detectAudioBurst directly.
 *
 * The burst detector could previously only be observed by clicking through the
 * GUI, so every statement about its thresholds was a claim rather than a
 * measurement. This tool asks the real detector about one cut boundary.
 *
 * Companion: tools/burst-analysis/ scans a whole stream for the boundaries
 * worth asking about; this tool delivers the authoritative answer.
 */
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QString>
#include <QTextStream>

#include "extern/ttffmpegwrapper.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("TTCut-ng");
    QCoreApplication::setApplicationName("ttcut-burst-probe");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Probe TTCut-ng's audio burst detector at a single cut boundary.");
    parser.addHelpOption();
    parser.addPositionalArgument("audio",    "Audio elementary stream (.ac3/.mp2)");
    parser.addPositionalArgument("boundary", "Boundary time in seconds");

    QCommandLineOption cutInOpt("cutin", "Use CutIn window semantics (default: CutOut)");
    parser.addOption(cutInOpt);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.size() != 2) {
        parser.showHelp(2);
    }

    bool ok = false;
    const double boundary = args.at(1).toDouble(&ok);   // locale-independent
    if (!ok) {
        QTextStream(stderr) << "invalid boundary time: " << args.at(1) << "\n";
        return 2;
    }

    // detectAudioBurst only assigns these on a positive detection.
    double burstDb = 0.0;
    double contextDb = 0.0;

    const bool present = TTFFmpegWrapper::detectAudioBurst(
        args.at(0), boundary, !parser.isSet(cutInOpt), burstDb, contextDb);

    QTextStream out(stdout);
    if (present) {
        out << "present=1"
            << " burstDb="   << QString::number(burstDb, 'f', 2)
            << " contextDb=" << QString::number(contextDb, 'f', 2)
            << " delta="     << QString::number(burstDb - contextDb, 'f', 2)
            << "\n";
        return 0;
    }
    out << "present=0\n";
    return 1;
}
