/*
 * probe_copystart - classify the copy-start keyframe of each planned seam.
 *
 * For every cut-in frame index given on the command line, finds the first
 * keyframe AU at/after it (= the stream-copy start the smart cut will use)
 * and reports whether it is a true IDR. This decides the verification tier
 * for the bridgeFrameNum change: non-IDR seams must stay bit-identical,
 * IDR seams may change bitstream but must stay pixel-identical.
 */
#include <QCoreApplication>
#include <QTextStream>
#include "avstream/ttnaluparser.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout), err(stderr);
    if (argc < 3) {
        err << "usage: probe_copystart <es-file> <cutInIndex> [<cutInIndex>...]\n";
        return 2;
    }
    TTNaluParser parser;
    if (!parser.openFile(argv[1]) || !parser.parseFile()) {
        err << "cannot open/parse " << argv[1] << "\n";
        return 1;
    }
    out << "codec=" << parser.codecName()
        << " AUs=" << parser.accessUnitCount() << "\n";
    if (QString(argv[2]) == "--list-idrs") {
        // List every true-IDR AU (for placing cut-ins just before one).
        for (int i = 0; i < parser.accessUnitCount(); ++i) {
            TTAccessUnit au = parser.accessUnitAt(i);
            if (au.isIDR)
                out << "IDR AU=" << i << " startOffset=" << au.startOffset << "\n";
        }
        return 0;
    }
    for (int a = 2; a < argc; ++a) {
        int cutIn = QString(argv[a]).toInt();
        int kf = parser.findKeyframeAfter(cutIn);
        if (kf < 0) { out << "cutIn=" << cutIn << " NO KEYFRAME AFTER\n"; continue; }
        TTAccessUnit au = parser.accessUnitAt(kf);
        out << "cutIn=" << cutIn << " keyframeAU=" << kf
            << " isIDR=" << (au.isIDR ? 1 : 0)
            << " sliceType=" << au.sliceType
            << " startOffset=" << au.startOffset
            << " size=" << (au.endOffset - au.startOffset) << "\n";
    }
    return 0;
}
