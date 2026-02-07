/*
 * Test program for TTNaluParser
 * Compile: g++ -I.. -I/usr/include/x86_64-linux-gnu/qt5 -fPIC test_nalu_parser.cpp ../avstream/ttnaluparser.cpp -o test_nalu_parser -lQt5Core
 * Usage: ./test_nalu_parser <input.264|input.265>
 */

#include "../avstream/ttnaluparser.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <iostream>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.264|input.265>" << std::endl;
        return 1;
    }

    QString inputFile = argv[1];

    if (!QFileInfo::exists(inputFile)) {
        std::cerr << "Error: File not found: " << inputFile.toStdString() << std::endl;
        return 1;
    }

    std::cout << "=============================================" << std::endl;
    std::cout << "TTNaluParser Test" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "Input: " << inputFile.toStdString() << std::endl;
    std::cout << std::endl;

    TTNaluParser parser;

    // Open file
    std::cout << "Opening file..." << std::endl;
    if (!parser.openFile(inputFile)) {
        std::cerr << "Error: " << parser.lastError().toStdString() << std::endl;
        return 1;
    }
    std::cout << "  Codec: " << parser.codecName().toStdString() << std::endl;

    // Parse file
    std::cout << "Parsing file..." << std::endl;
    if (!parser.parseFile()) {
        std::cerr << "Error: " << parser.lastError().toStdString() << std::endl;
        return 1;
    }

    // Print statistics
    std::cout << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "Results" << std::endl;
    std::cout << "=============================================" << std::endl;
    std::cout << "NAL Units:      " << parser.nalUnitCount() << std::endl;
    std::cout << "Access Units:   " << parser.accessUnitCount() << std::endl;
    std::cout << "GOPs:           " << parser.gopCount() << std::endl;
    std::cout << "SPS count:      " << parser.spsCount() << std::endl;
    std::cout << "PPS count:      " << parser.ppsCount() << std::endl;
    if (parser.codecType() == NALU_CODEC_H265) {
        std::cout << "VPS count:      " << parser.vpsCount() << std::endl;
    }

    // Print first 10 GOPs
    std::cout << std::endl;
    std::cout << "First 10 GOPs:" << std::endl;
    for (int i = 0; i < qMin(10, parser.gopCount()); ++i) {
        TTGopInfo gop = parser.gopAt(i);
        std::cout << "  GOP " << i << ": frames " << gop.startAU << " - " << gop.endAU
                  << " (" << gop.frameCount << " frames)" << std::endl;
    }

    // Print first 20 frames
    std::cout << std::endl;
    std::cout << "First 20 frames:" << std::endl;
    for (int i = 0; i < qMin(20, parser.accessUnitCount()); ++i) {
        TTAccessUnit au = parser.accessUnitAt(i);
        QString type = au.isKeyframe ? "I" : (au.sliceType == 0 ? "P" : (au.sliceType == 1 ? "B" : "?"));
        std::cout << "  Frame " << i << ": " << type.toStdString()
                  << ", NALs: " << au.nalIndices.size()
                  << ", GOP: " << au.gopIndex << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Test complete." << std::endl;

    return 0;
}
