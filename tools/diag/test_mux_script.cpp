// Is the mplex batch script a shell script, and does it hold only what mplex
// can mux?
//
// Code-audit run 7, hypothesis H5 (measured): writeMuxScript() wrote
// "# TTCut - Mplex script ver. 1.0" as line 1 and "#!/bin/sh" as line 2, so
// the file had no shebang; and TTAVData::onH26xCutFinished() appended its
// .mkv result to the same mux list, which gave the script a line
// "mplex -f8 -o x.mpg x.mkv" for every earlier H.264/H.265 cut of the session.
//
// The shebang half is checked here. The list half is a removal (the H.26x
// path no longer appends); this harness pins the other side of it: every
// list item becomes exactly one mplex line, with the target's -f number.
//
//   usage: test_mux_script <workdir>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cstdio>

#include "common/ttsettings.h"
#include "extern/ttmplexprovider.h"
#include "extern/ttmuxlistdata.h"

static int fail(const char* what) { fprintf(stderr, "FAIL: %s\n", what); return 1; }

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: %s <workdir>\n", argv[0]); return 2; }
    const QDir work(argv[1]);

    TTSettings::instance()->setCutDirPath(work.absolutePath());
    TTSettings::instance()->setMuxOutputPath(work.absolutePath());
    TTSettings::instance()->setWorkingMpeg2Target(3);   // Generic MPEG2, -f3

    TTMuxListData list;
    for (const char* name : {"first", "second"}) {
        TTMuxListDataItem item;
        item.setVideoName(work.filePath(QString("%1.m2v").arg(name)));
        item.appendAudioFile(work.filePath(QString("%1_001.mp2").arg(name)), "deu");
        list.appendItem(item);
    }

    TTMplexProvider provider(&list);
    provider.writeMuxScript();

    QFile script(work.filePath("muxscript.sh"));
    if (!script.open(QIODevice::ReadOnly | QIODevice::Text)) return fail("no muxscript.sh written");
    const QStringList lines = QTextStream(&script).readAll().split('\n', Qt::SkipEmptyParts);
    for (const QString& l : lines) printf("| %s\n", qPrintable(l));

    if (lines.isEmpty() || lines.first() != "#!/bin/sh")
        return fail("line 1 is not the #!/bin/sh shebang");
    int mplexLines = 0;
    for (const QString& l : lines) {
        if (!l.startsWith("mplex ")) continue;
        ++mplexLines;
        if (!l.startsWith("mplex -f3 ")) return fail("an mplex line does not carry the chosen target -f3");
    }
    if (mplexLines != 2) return fail("not exactly one mplex line per list item");
    if (!(script.permissions() & QFile::ExeOwner)) return fail("script is not executable");

    printf("PASS: shebang on line 1, one mplex line per item, target -f3\n");
    return 0;
}
