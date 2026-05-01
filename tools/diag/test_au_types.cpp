/*
 * Debug tool: Dump Access Unit types from TTNaluParser
 * Usage: test_au_types <es_file> <start_au> <end_au>
 */
#include "../../avstream/ttnaluparser.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <es_file> <start_au> <end_au>" << std::endl;
        return 1;
    }

    QString esFile = argv[1];
    int startAU = atoi(argv[2]);
    int endAU = atoi(argv[3]);

    TTNaluParser parser;
    if (!parser.openFile(esFile)) {
        std::cerr << "Failed to open: " << esFile.toStdString() << std::endl;
        return 1;
    }

    std::cout << "Parsing " << esFile.toStdString() << "..." << std::endl;
    if (!parser.parseFile()) {
        std::cerr << "Parse failed" << std::endl;
        return 1;
    }

    std::cout << "Total AUs: " << parser.accessUnitCount() << std::endl;
    std::cout << "Codec: " << parser.codecName().toStdString() << std::endl;
    std::cout << std::endl;

    const char* sliceNames[] = {"P", "B", "I", "SP", "SI", "P_ALL", "B_ALL", "I_ALL", "SP_ALL", "SI_ALL"};

    std::cout << "AU#     | Keyframe | IDR  | SliceType | NAL types" << std::endl;
    std::cout << "--------|----------|------|-----------|----------" << std::endl;

    for (int i = startAU; i <= endAU && i < parser.accessUnitCount(); ++i) {
        TTAccessUnit au = parser.accessUnitAt(i);
        const char* stName = (au.sliceType >= 0 && au.sliceType <= 9) ? sliceNames[au.sliceType] : "?";

        printf("AU %5d | %-8s | %-4s | %-9s |", i,
               au.isKeyframe ? "YES" : "no",
               au.isIDR ? "YES" : "no",
               stName);

        // Show NAL types in this AU
        for (int ni : au.nalIndices) {
            TTNalUnit nal = parser.nalUnitAt(ni);
            printf(" %d", nal.type);
            if (nal.isSlice) {
                const char* nstName = (nal.sliceType >= 0 && nal.sliceType <= 9) ? sliceNames[nal.sliceType] : "?";
                printf("(%s)", nstName);
            }
        }
        printf("\n");
    }

    // Show findKeyframeBefore for each AU in range
    std::cout << std::endl;
    for (int i = startAU; i <= endAU && i < parser.accessUnitCount(); ++i) {
        int kf = parser.findKeyframeBefore(i);
        int kfAfter = parser.findKeyframeAfter(i);
        std::cout << "AU " << i << ": findKeyframeBefore=" << kf
                  << "  findKeyframeAfter=" << kfAfter << std::endl;
    }

    return 0;
}
