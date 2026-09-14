// Gate for audit run 6, batch C: the job cut list has an owner.
//
// TTCutTreeView::cutListFromSelection built a `new TTCutList` (a QObject
// without parent) for every preview and every cut and handed the bare
// pointer on. None of the receivers freed it: TTCutMainWindow keeps it in
// mpPreviewOriginalCutList, TTAVData in mpRunningCutList, and
// TTCutPreviewTask owns only the preview list it derives itself. There was
// no delete for it anywhere - one leak per preview and per started cut.
// (The headless --auto-cut route was never affected: it passes
// TTAVData::cutList(), the global list.)
//
// The producer owns it now, so this gate watches destroyed(): starting a
// second job must free the first, and destroying the view must free the
// last. QObject::destroyed is used rather than a sanitizer because the diag
// harnesses are built without one - the same reason test_analysis_task_
// lifetime drives the real code path instead of measuring memory.
//
// Material-free (no stream is opened), offscreen, no dialogs.
//   usage: test_cut_job_ownership <workdir>
// Build via `cmake --build build --target test_cut_job_ownership`.
#include <QApplication>
#include <QDir>
#include <cstdio>

#include "data/ttavdata.h"
#include "data/ttcutlist.h"
#include "gui/ttcuttreeview.h"

namespace {

int failures = 0;
void check(bool ok, const char* what)
{
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}

} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);

  TTAVData avData;
  avData.setNonInteractive(true);

  TTCutTreeView* view = new TTCutTreeView(nullptr);
  view->setAVData(&avData);

  QList<TTCutList*> handed;      // the job lists the view emitted
  int destroyed = 0;
  QObject::connect(view, &TTCutTreeView::audioVideoCut,
                   [&](bool, TTCutList* list) {
                     handed << list;
                     QObject::connect(list, &QObject::destroyed,
                                      [&destroyed]() { destroyed++; });
                   });

  view->onAVCut();
  check(handed.count() == 1, "the first cut command produces a job list");
  check(destroyed == 0,      "it is still alive right after being handed over");

  view->onAVCut();
  check(handed.count() == 2, "the second cut command produces another one");
  // Not checked: that the two pointers differ - the first list is freed
  // before the second is allocated, so the allocator may hand back the same
  // address. The destroy count is the actual evidence.
  check(destroyed == 1,      "starting the second job freed the first");

  delete view;
  check(destroyed == 2, "destroying the view freed the last job list");

  printf("%s\n", failures ? "CUT-JOB-OWNERSHIP FAIL" : "CUT-JOB-OWNERSHIP PASS");
  return failures ? 1 : 0;
}
