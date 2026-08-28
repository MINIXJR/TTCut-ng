/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Headless acceptance for the quick-jump thumbnail height setting: the       */
/* default, the save/load round trip, and — the case that matters for         */
/* existing users — a config file written before the setting existed, which   */
/* must fall back to the default instead of yielding a zero-height tile.      */
/*                                                                            */
/* Isolated through XDG_CONFIG_HOME (TTSettings hardcodes                     */
/* QSettings("TTCut-ng", "TTCut-ng")), so the real user settings are never    */
/* touched. The env var is set before QCoreApplication exists, because        */
/* QSettings resolves its path on first use.                                  */
/*----------------------------------------------------------------------------*/

#include "../../common/ttsettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>

#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void check(bool cond, const char* what)
{
  printf(cond ? "PASS: %s\n" : "FAIL: %s\n", what);
  if (!cond) ++failures;
}

static void checkInt(int got, int want, const char* what)
{
  const bool ok = (got == want);
  printf(ok ? "PASS: %s = %d\n" : "FAIL: %s: got %d, want %d\n", what, got, want);
  if (!ok) ++failures;
}

int main(int argc, char** argv)
{
  setvbuf(stdout, nullptr, _IONBF, 0);

  // Must happen before anything constructs a QSettings.
  const QString scratch = QDir::temp().filePath("ttcut-thumbheight-test");
  QDir(scratch).removeRecursively();
  QDir().mkpath(scratch);
  qputenv("XDG_CONFIG_HOME", scratch.toLocal8Bit());

  QCoreApplication app(argc, argv);
  printf("config home: %s\n", qPrintable(scratch));

  TTSettings* s = TTSettings::instance();

  // 1. The default, on a config that does not exist yet.
  s->load();
  checkInt(s->quickJumpThumbHeight(), TTSettings::kQuickJumpThumbHeightDefault,
           "default height with no config");

  // 2. Round trip: a set value survives save + load.
  s->setQuickJumpThumbHeight(150);   // deliberately not the default
  s->save();
  s->setQuickJumpThumbHeight(1);        // clobber in memory
  s->load();
  checkInt(s->quickJumpThumbHeight(), 150, "height after save + load");

  // 3. The upgrade case: a config written before this setting existed has no
  //    such key. Reading it must not yield 0 — a zero height would collapse
  //    every tile and make every thumbnail decode pointless.
  //
  //    Note how load() falls back: `settings.value(key, mMember)` uses the
  //    CURRENT member as the default, not the class's initial value. That is
  //    the pattern every setting here follows, and it is why this test cannot
  //    simply clobber the member first — it would get its own clobbered value
  //    back and prove nothing. At real startup the member still holds its
  //    initialiser, which is what makes the fallback correct. This test
  //    reproduces that state rather than a mid-session overwrite.
  {
    QSettings raw("TTCut-ng", "TTCut-ng");
    raw.beginGroup("Settings");
    raw.beginGroup("Common");
    raw.remove("QuickJumpThumbHeight/");
    raw.endGroup();
    raw.endGroup();
    raw.sync();
    check(!raw.contains("Settings/Common/QuickJumpThumbHeight/"),
          "key removed from the config file");
  }
  s->setQuickJumpThumbHeight(TTSettings::kQuickJumpThumbHeightDefault);  // fresh-process state
  s->load();
  checkInt(s->quickJumpThumbHeight(), TTSettings::kQuickJumpThumbHeightDefault,
           "height survives a config without the key");
  check(s->quickJumpThumbHeight() > 0, "height is never zero after loading an old config");

  QDir(scratch).removeRecursively();
  return failures == 0 ? 0 : 1;
}
