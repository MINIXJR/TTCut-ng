#include "../avstream/ttnaluparser.h"
#include <QCoreApplication>
#include <iostream>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2) return 1;
    
    TTNaluParser parser;
    if (!parser.openFile(argv[1])) return 1;
    if (!parser.parseFile()) return 1;
    
    int idrCount = 0, isliceCount = 0;
    for (int i = 0; i < parser.accessUnitCount(); i++) {
        TTAccessUnit au = parser.accessUnitAt(i);
        if (au.isIDR) idrCount++;
        if (au.isKeyframe && !au.isIDR) isliceCount++;
    }
    
    std::cout << "Total frames: " << parser.accessUnitCount() << std::endl;
    std::cout << "IDR frames: " << idrCount << std::endl;
    std::cout << "I-slice (non-IDR) keyframes: " << isliceCount << std::endl;
    std::cout << "GOPs: " << parser.gopCount() << std::endl;
    
    // Show first 5 keyframes
    std::cout << "\nFirst 5 keyframes:" << std::endl;
    int shown = 0;
    for (int i = 0; i < parser.accessUnitCount() && shown < 5; i++) {
        TTAccessUnit au = parser.accessUnitAt(i);
        if (au.isKeyframe) {
            std::cout << "  Frame " << i << ": " << (au.isIDR ? "IDR" : "I-slice") << std::endl;
            shown++;
        }
    }
    return 0;
}
