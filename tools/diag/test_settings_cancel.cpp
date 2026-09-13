// Gate for contract finding 1 of code-audit run 3 (2026-09-12): a settings
// page must not write TTSettings while the dialog is open, and Cancel must
// leave TTSettings as it was. Before the fix the muxer page wrote
// setMkvCreateChapters() on every toggle and TTCutMainWindow::openSettingsDialog()
// saved regardless of how the dialog closed, so a toggled-then-cancelled
// checkbox reached the configuration file.
//
// Runs offscreen without loading or saving any file: TTSettings starts from
// its compiled defaults, the dialog is driven through the widget tree.
//
// Build: cmake --build build --target test_settings_cancel
#include <QApplication>
#include <QCheckBox>
#include <cstdio>

#include "common/ttsettings.h"
#include "gui/ttcutsettingsdlg.h"

static int failures = 0;
static void expect(const char* name, bool ok)
{
  printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) failures++;
}

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  TTSettings* s = TTSettings::instance();
  s->setMkvCreateChapters(true);
  s->setMkvChapterInterval(5);

  {
    TTSettingsDialog dlg;
    QCheckBox* cb = dlg.findChild<QCheckBox*>("cbMkvCreateChapters");
    if (!cb) { printf("FAIL  cbMkvCreateChapters not found\n"); return 1; }
    expect("dialog shows the stored value", cb->isChecked());
    cb->setChecked(false);
    expect("toggle does not write TTSettings", s->mkvCreateChapters());
    dlg.reject();
    expect("cancel leaves TTSettings untouched", s->mkvCreateChapters());
  }
  {
    TTSettingsDialog dlg;
    QCheckBox* cb = dlg.findChild<QCheckBox*>("cbMkvCreateChapters");
    cb->setChecked(false);
    dlg.accept();
    expect("OK writes the page", !s->mkvCreateChapters());
  }
  printf("%s\n", failures ? "SETTINGS-CANCEL FAIL" : "SETTINGS-CANCEL PASS");
  return failures ? 1 : 0;
}
