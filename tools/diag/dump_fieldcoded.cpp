// Diag: dump the frame indices flagged isFieldCoded in TTFFmpegWrapper's frame
// index (MERGED numbering — after mergePAFFFieldsInIndex collapsed the field
// pairs; each hit is the top field of a pair). Used to inspect H.264 PAFF
// field-pair structure; see also test_rawmap for the raw->merged map itself.
// Usage: dump_fieldcoded <es_file> [spot_index ...]
#include "../../extern/ttffmpegwrapper.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: %s <es_file> [spot_index ...]\n", argv[0]); return 2; }

    TTFFmpegWrapper w;
    if (!w.openFile(argv[1]))       { fprintf(stderr, "openFile failed\n");       return 1; }
    if (!w.buildFrameIndex(-1))     { fprintf(stderr, "buildFrameIndex failed\n"); return 1; }

    const QList<TTFrameInfo>& idx = w.frameIndex();
    int total = 0;
    printf("total frames: %d\n", idx.size());
    printf("field-coded positions (first 60):\n");
    for (int i = 0; i < idx.size(); ++i) {
        if (idx[i].isFieldCoded) {
            if (total < 60) printf("%d ", i);
            total++;
        }
    }
    printf("\ntotal field-coded: %d\n", total);

    auto isFC = [&](int i){ return i >= 0 && i < idx.size() && idx[i].isFieldCoded; };
    for (int a = 2; a < argc; ++a) {
        int s = atoi(argv[a]);
        printf("spot %d: %s\n", s, isFC(s) ? "field-coded" : "not field-coded");
    }
    return 0;
}
