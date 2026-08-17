/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Regression harness for the dotted-output-name bug (2026-08-17): the cut    */
/* dialog rebuilt the output name with a blind completeBaseName(), which      */
/* treats everything after the LAST dot as an extension. A title with dots    */
/* ("...#2E" from VDR masking, or "Mission 1.5") lost its last segment on     */
/* every rebuild. The fix strips only extensions the dialog itself attaches   */
/* (TTCutAVCutDlg::stripKnownExtension).                                      */
/*                                                                            */
/*   usage: QT_QPA_PLATFORM=offscreen test_output_name                        */
/*   Build: cmake --build build --target test_output_name                     */
/*                                                                            */
/* Reads TTSettings but never calls getCommonData()/save(), so the user       */
/* configuration stays untouched.                                             */
/*----------------------------------------------------------------------------*/

#include "../../gui/ttcutavcutdlg.h"

#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <cstdio>

static int failures = 0;

static void checkEq(const QString& got, const QString& want, const char* what)
{
  const bool ok = (got == want);
  printf("%s: %s: \"%s\"", ok ? "PASS" : "FAIL", what, qPrintable(got));
  if (!ok) printf(" (want \"%s\")", qPrintable(want));
  printf("\n");
  if (!ok) ++failures;
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);

  // Helper unit cases
  checkEq(TTCutAVCutDlg::stripKnownExtension("Name..#2E.mkv"), "Name..#2E", "strip .mkv");
  checkEq(TTCutAVCutDlg::stripKnownExtension("Name..#2E"),     "Name..#2E", "keep #2E tail");
  checkEq(TTCutAVCutDlg::stripKnownExtension("Mission 1.5"),   "Mission 1.5", "keep numeric tail");
  checkEq(TTCutAVCutDlg::stripKnownExtension("x.h264"),        "x", "strip .h264");
  checkEq(TTCutAVCutDlg::stripKnownExtension("x.MKV"),         "x", "strip case-insensitive");
  checkEq(TTCutAVCutDlg::stripKnownExtension("plain"),         "plain", "no dot");

  // Dialog end-to-end: setText drives textChanged -> updateOutputFilename().
  TTCutAVCutDlg dlg(nullptr, false);
  auto* le = dlg.findChild<QLineEdit*>("leOutputFile");
  auto* cb = dlg.findChild<QCheckBox*>("cbAddSuffix");
  if (!le || !cb) { printf("FAIL: dialog widgets not found\n"); return 1; }
  cb->setChecked(false);

  const QString base = "03x02_-_Das_Leben_ist_hart..#2E";
  le->setText(base);
  // The rebuild hangs off the suffix checkbox / codec change, not off the
  // text field — toggle the checkbox there and back to drive it twice.
  cb->setChecked(true);
  cb->setChecked(false);
  QApplication::processEvents();
  const QString got = le->text();
  // Container extension depends on the current settings (mkv or mpg); the
  // invariant under test is that the dotted base survives untouched.
  const bool ok = got.startsWith(base + ".") &&
                  (got.endsWith(".mkv") || got.endsWith(".mpg"));
  printf("%s: dialog rebuild: \"%s\"\n", ok ? "PASS" : "FAIL", qPrintable(got));
  if (!ok) ++failures;

  printf(failures ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", failures);
  return failures ? 1 : 0;
}
