/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// Gate for the aspect hint in the cut list (column 5) and in the warning
// before the cut.
//
// Video-only MPEG-2 project on the make_aspect_m2v fixture (run-gates.sh):
// 4:3 with a 16:9 run [A, B]. No audio track on purpose - the aspect part
// must not depend on one, unlike the burst part.
//
//   usage: test_aspect_hint <fixture.m2v> <A> <B> <workdir>
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>
#include <cstdlib>

#include "avstream/ttavstream.h"
#include "data/ttavdata.h"
#include "data/ttavlist.h"
#include "data/ttcutlist.h"
#include "gui/ttcuttreeview.h"

namespace {

int failures = 0;
void check(bool ok, const char* what, const QString& detail = QString())
{
  printf("%s: %s%s\n", ok ? "PASS" : "FAIL", what,
         detail.isEmpty() ? "" : qPrintable("  (" + detail + ")"));
  if (!ok) failures++;
}

QString hintOf(QTreeWidget* tree, int row)
{
  QTreeWidgetItem* it = tree->topLevelItem(row);
  return it ? it->text(5) : QStringLiteral("<no row>");
}
QString tipOf(QTreeWidget* tree, int row)
{
  QTreeWidgetItem* it = tree->topLevelItem(row);
  return it ? it->toolTip(5).replace('\n', " | ") : QString();
}
bool hasIcon(QTreeWidget* tree, int row)
{
  QTreeWidgetItem* it = tree->topLevelItem(row);
  return it && !it->icon(5).isNull();
}

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  if (argc < 5) {
    fprintf(stderr, "usage: %s <fixture.m2v> <A> <B> <workdir>\n", argv[0]);
    return 2;
  }
  const QString video = QFileInfo(QString::fromUtf8(argv[1])).absoluteFilePath();
  const int A = atoi(argv[2]);
  const int B = atoi(argv[3]);
  const QDir work(QString::fromUtf8(argv[4]));
  QDir().mkpath(work.absolutePath());

  const QString prj = work.absoluteFilePath("aspect.ttcut");
  {
    QFile f(prj);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    f.write(QString("<!DOCTYPE TTCut-Projectfile>\n<TTCut-Projectfile>\n <Version>1.0</Version>\n"
                    " <Video>\n  <Order>0</Order>\n  <Name>%1</Name>\n </Video>\n"
                    "</TTCut-Projectfile>\n").arg(video).toUtf8());
  }

  TTAVData avData;
  avData.setNonInteractive(true);
  QEventLoop loop;
  bool loaded = false;
  QObject::connect(&avData, &TTAVData::readProjectFileFinished, &loop,
                   [&](const QString&) { loaded = true; loop.quit(); });
  QObject::connect(&avData, &TTAVData::readProjectFileAborted, &loop,
                   [&]() { loop.quit(); });
  QTimer::singleShot(120000, &loop, &QEventLoop::quit);
  avData.readProjectFile(QFileInfo(prj));
  if (!loaded) loop.exec();
  check(loaded && avData.avCount() == 1, "video-only MPEG-2 project loaded");
  if (!loaded || avData.avCount() == 0) { printf("ASPECT-HINT FAIL\n"); return 1; }

  TTAVItem* item = avData.avItemAt(0);
  check(item->audioCount() == 0, "project has no audio track");

  TTCutTreeView view(nullptr);
  view.setAVData(&avData);
  QTreeWidget* tree = view.findChild<QTreeWidget*>();
  check(tree != nullptr, "the view has its tree widget");
  if (!tree) { printf("ASPECT-HINT FAIL\n"); return 1; }

  item->appendCutEntry(A + 5, B - 5);   // 0: inside the 16:9 run
  item->appendCutEntry(A - 1, B - 5);   // 1: one 4:3 picture at the cut-in
  item->appendCutEntry(A + 5, B + 1);   // 2: one 4:3 picture at the cut-out
  item->appendCutEntry(A - 2, B + 2);   // 3: 4:3 at both edges
  app.processEvents();
  check(tree->topLevelItemCount() == 4, "four rows in the cut list",
        QString::number(tree->topLevelItemCount()));

  check(hintOf(tree, 0).isEmpty() && !hasIcon(tree, 0),
        "no hint and no icon inside one aspect", hintOf(tree, 0));
  check(hintOf(tree, 1) == QStringLiteral("Aspect start"),
        "cut-in on 4:3 says 'Aspect start'", hintOf(tree, 1));
  check(tipOf(tree, 1).contains("Starts in 4:3") && tipOf(tree, 1).contains("16:9")
        && tipOf(tree, 1).contains(QString::number(A)) && tipOf(tree, 1).contains("(+1)"),
        "cut-in tooltip names both aspects, the target frame and the distance", tipOf(tree, 1));
  check(hasIcon(tree, 1), "aspect hint carries an icon");
  check(hintOf(tree, 2) == QStringLiteral("Aspect end"),
        "cut-out on 4:3 says 'Aspect end'", hintOf(tree, 2));
  check(tipOf(tree, 2).contains("Ends in 4:3") && tipOf(tree, 2).contains(QString::number(B))
        && tipOf(tree, 2).contains("(-1)"),
        "cut-out tooltip names the target frame and the distance", tipOf(tree, 2));
  check(hintOf(tree, 3) == QStringLiteral("Aspect start+end"),
        "both edges say 'Aspect start+end'", hintOf(tree, 3));

  // The warning before the cut: pure list first, then the non-interactive
  // confirm (--auto-cut semantics: log and proceed, never block).
  TTCutList* cuts = avData.cutList();
  const QStringList warnings = avData.cutWarnings(cuts);
  const QString joined = warnings.join(" | ");
  check(warnings.filter("Cut 1:").isEmpty(),
        "no warning for the cut inside one aspect", joined);
  check(warnings.contains(QString("Cut 2: starts in 4:3, the cut is 16:9 from frame %1").arg(A)),
        "cut 2 warns about its 4:3 start, without an audio track", joined);
  check(warnings.contains(QString("Cut 3: ends in 4:3, the cut is 16:9 up to frame %1").arg(B)),
        "cut 3 warns about its 4:3 end", joined);
  check(warnings.filter("Cut 4:").size() == 2, "cut 4 warns about both edges", joined);
  check(warnings.size() == 4, "four aspect warnings in total", QString::number(warnings.size()));
  check(avData.confirmCutWarnings(cuts), "non-interactive confirm proceeds");

  printf("%s\n", failures ? "ASPECT-HINT FAIL" : "ASPECT-HINT PASS");
  return failures ? 1 : 0;
}
