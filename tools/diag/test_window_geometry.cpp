/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/* Headless acceptance for gui/ttwindowgeometry: the plain-text window-state  */
/* keys, the clamp to the available area, and the one-time decode of the      */
/* legacy QByteArray blob. Uses a scratch .conf, touches no user settings.    */
/*----------------------------------------------------------------------------*/

#include "../../gui/ttwindowgeometry.h"

#include <QApplication>
#include <QWidget>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <cstdio>

static int failures = 0;

static void check(bool cond, const char* what)
{
  printf(cond ? "PASS: %s\n" : "FAIL: %s\n", what);
  if (!cond) ++failures;
}

static void checkRect(const QRect& got, const QRect& want, const char* what)
{
  const bool ok = (got == want);
  if (ok) {
    printf("PASS: %s = %d,%d %dx%d\n", what, got.x(), got.y(), got.width(), got.height());
  } else {
    printf("FAIL: %s: got %d,%d %dx%d, want %d,%d %dx%d\n", what,
           got.x(), got.y(), got.width(), got.height(),
           want.x(), want.y(), want.width(), want.height());
    ++failures;
  }
}

static QString scratchPath()
{
  return QDir::temp().filePath("ttcut-geom-test.conf");
}

static void testRoundTrip()
{
  QFile::remove(scratchPath());
  QSettings s(scratchPath(), QSettings::IniFormat);

  ttSaveWindowGeometry(s, "MainWindow", QRect(320, 180, 1920, 1080), false);
  s.sync();

  const TTWindowGeometry g = ttLoadWindowGeometry(s, "MainWindow");
  check(g.valid, "round trip: loaded value is valid");
  checkRect(g.rect, QRect(320, 180, 1920, 1080), "round trip: rect");
  check(!g.maximized, "round trip: maximized false");

  // The point of the whole change: the file must be readable.
  QFile f(scratchPath());
  f.open(QIODevice::ReadOnly | QIODevice::Text);
  const QString text = QString::fromUtf8(f.readAll());
  check(text.contains("x=320"),        "file contains x=320");
  check(text.contains("y=180"),        "file contains y=180");
  check(text.contains("width=1920"),   "file contains width=1920");
  check(text.contains("height=1080"),  "file contains height=1080");
  check(text.contains("maximized=false"), "file contains maximized=false");
  check(!text.contains("@ByteArray"),  "file contains no @ByteArray");
}

static void testMaximizedRoundTrip()
{
  QFile::remove(scratchPath());
  QSettings s(scratchPath(), QSettings::IniFormat);

  // Maximised: the NORMAL rect is what gets stored.
  ttSaveWindowGeometry(s, "MainWindow", QRect(100, 50, 1280, 720), true);
  s.sync();

  const TTWindowGeometry g = ttLoadWindowGeometry(s, "MainWindow");
  check(g.maximized, "maximized round trip: flag true");
  checkRect(g.rect, QRect(100, 50, 1280, 720), "maximized round trip: normal rect kept");
}

static void testMissingAndPartial()
{
  QFile::remove(scratchPath());
  QSettings s(scratchPath(), QSettings::IniFormat);

  check(!ttLoadWindowGeometry(s, "MainWindow").valid, "absent group is invalid");

  // A half-written group must not produce a half-valid window.
  s.setValue("MainWindow/x", 10);
  s.setValue("MainWindow/y", 10);
  s.sync();
  check(!ttLoadWindowGeometry(s, "MainWindow").valid, "partial group is invalid");

  // Nonsense sizes are rejected rather than clamped into existence.
  s.setValue("MainWindow/width", 0);
  s.setValue("MainWindow/height", -5);
  s.sync();
  check(!ttLoadWindowGeometry(s, "MainWindow").valid, "non-positive size is invalid");
}

static void testClamp()
{
  const QRect avail(0, 0, 2560, 1440);

  checkRect(ttClampToArea(QRect(320, 180, 1920, 1080), avail),
            QRect(320, 180, 1920, 1080), "clamp: fitting rect untouched");

  checkRect(ttClampToArea(QRect(0, 0, 4000, 3000), avail),
            QRect(0, 0, 2560, 1440), "clamp: oversized shrunk to area");

  checkRect(ttClampToArea(QRect(2000, 1200, 1200, 800), avail),
            QRect(1360, 640, 1200, 800), "clamp: overhanging pushed back inside");

  checkRect(ttClampToArea(QRect(-300, -200, 800, 600), avail),
            QRect(0, 0, 800, 600), "clamp: negative origin pulled to area");

  // Offset screen (second monitor): the area origin must be respected.
  const QRect avail2(2560, 0, 2194, 1234);
  checkRect(ttClampToArea(QRect(0, 0, 800, 600), avail2),
            QRect(2560, 0, 800, 600), "clamp: rect moved onto offset area");
}

static void testDialogSize()
{
  QFile::remove(scratchPath());
  QSettings s(scratchPath(), QSettings::IniFormat);

  check(!ttLoadDialogSize(s, "QuickJumpDialog").valid, "dialog: absent is invalid");

  ttSaveDialogSize(s, "QuickJumpDialog", QSize(1141, 695));
  s.sync();

  const TTWindowGeometry g = ttLoadDialogSize(s, "QuickJumpDialog");
  check(g.valid, "dialog: loaded value is valid");
  check(g.rect.size() == QSize(1141, 695), "dialog: size round trip");

  QFile f(scratchPath());
  f.open(QIODevice::ReadOnly | QIODevice::Text);
  const QString text = QString::fromUtf8(f.readAll());
  check(text.contains("width=1141") && text.contains("height=695"),
        "dialog: file is plain text");
  check(!text.contains("@Size"), "dialog: file contains no @Size");
}

// The blob is produced by Qt itself here rather than pasted in as bytes, so
// the parser is checked against the real serialisation and not against notes
// about it. A widget is created but never shown — saveGeometry() only reads
// the object's own state, and under Wayland a client cannot place itself
// anyway.
static QByteArray makeBlob(const QRect& r, bool maximized)
{
  QWidget w;
  w.setGeometry(r);
  if (maximized) w.setWindowState(Qt::WindowMaximized);
  return w.saveGeometry();
}

static void testBlobDecode()
{
  const TTWindowGeometry g = ttDecodeGeometryBlob(makeBlob(QRect(320, 180, 1920, 1080), false));
  check(g.valid, "blob: plain blob decodes");
  checkRect(g.rect, QRect(320, 180, 1920, 1080), "blob: rect matches");
  check(!g.maximized, "blob: maximized false");

  const TTWindowGeometry m = ttDecodeGeometryBlob(makeBlob(QRect(100, 50, 1280, 720), true));
  check(m.valid, "blob: maximised blob decodes");
  check(m.maximized, "blob: maximized flag true");
  checkRect(m.rect, QRect(100, 50, 1280, 720), "blob: normal rect read, not the maximised one");

  check(!ttDecodeGeometryBlob(QByteArray()).valid,          "blob: empty rejected");
  check(!ttDecodeGeometryBlob(QByteArray("garbage")).valid, "blob: garbage rejected");

  QByteArray truncated = makeBlob(QRect(0, 0, 800, 600), false);
  truncated.truncate(20);
  check(!ttDecodeGeometryBlob(truncated).valid, "blob: truncated rejected");
}

static void testMigration()
{
  QFile::remove(scratchPath());
  {
    QSettings s(scratchPath(), QSettings::IniFormat);
    s.setValue("MainWindow/geometry", makeBlob(QRect(320, 180, 1920, 1080), false));
    s.sync();
  }

  QSettings s(scratchPath(), QSettings::IniFormat);
  check(ttMigrateGeometryBlob(s, "MainWindow"), "migration: reports it happened");
  s.sync();

  const TTWindowGeometry g = ttLoadWindowGeometry(s, "MainWindow");
  check(g.valid, "migration: plain keys now valid");
  checkRect(g.rect, QRect(320, 180, 1920, 1080), "migration: rect carried over");
  check(!s.contains("MainWindow/geometry"), "migration: blob key removed");

  // Running twice must not undo or re-run anything.
  check(!ttMigrateGeometryBlob(s, "MainWindow"), "migration: second run is a no-op");
  checkRect(ttLoadWindowGeometry(s, "MainWindow").rect, QRect(320, 180, 1920, 1080),
            "migration: values survive the second run");

  // Plain keys already present + a stale blob: plain keys win, blob is dropped.
  QFile::remove(scratchPath());
  QSettings s2(scratchPath(), QSettings::IniFormat);
  ttSaveWindowGeometry(s2, "MainWindow", QRect(10, 20, 640, 480), false);
  s2.setValue("MainWindow/geometry", makeBlob(QRect(999, 999, 100, 100), false));
  s2.sync();
  ttMigrateGeometryBlob(s2, "MainWindow");
  checkRect(ttLoadWindowGeometry(s2, "MainWindow").rect, QRect(10, 20, 640, 480),
            "migration: existing plain keys are not overwritten");
  check(!s2.contains("MainWindow/geometry"), "migration: stale blob removed anyway");
}

// Runs against a scratch XDG_CONFIG_HOME so the user's own config is never
// read or deleted by a test.
static void testStrayImport()
{
  const QString sandbox = QDir::temp().filePath("ttcut-geom-xdg");
  QDir(sandbox).removeRecursively();
  const QString strayDir = sandbox + "/Unknown Organization";
  QDir().mkpath(strayDir);
  qputenv("XDG_CONFIG_HOME", sandbox.toLocal8Bit());

  {
    QSettings stray(strayDir + "/TTCut-ng.conf", QSettings::IniFormat);
    stray.setValue("QuickJumpDialog/size", QSize(1141, 695));
    stray.sync();
  }

  QFile::remove(scratchPath());
  QSettings target(scratchPath(), QSettings::IniFormat);
  ttImportStrayQuickJumpSize(target);
  target.sync();

  const TTWindowGeometry g = ttLoadDialogSize(target, "QuickJumpDialog");
  check(g.valid, "stray import: value arrived");
  check(g.rect.size() == QSize(1141, 695), "stray import: size matches");
  check(!QFile::exists(strayDir + "/TTCut-ng.conf"), "stray import: file removed");
  check(!QDir(strayDir).exists(), "stray import: directory removed");

  // Second run must not undo anything.
  ttImportStrayQuickJumpSize(target);
  check(ttLoadDialogSize(target, "QuickJumpDialog").valid, "stray import: second run harmless");

  // A stray file holding someone else's keys must survive.
  QDir().mkpath(strayDir);
  {
    QSettings other(strayDir + "/TTCut-ng.conf", QSettings::IniFormat);
    other.setValue("QuickJumpDialog/size", QSize(800, 600));
    other.setValue("SomethingElse/keep", 42);
    other.sync();
  }
  QFile::remove(scratchPath());
  QSettings target2(scratchPath(), QSettings::IniFormat);
  ttImportStrayQuickJumpSize(target2);
  check(QFile::exists(strayDir + "/TTCut-ng.conf"),
        "stray import: foreign keys keep the file alive");

  QDir(sandbox).removeRecursively();
  qunsetenv("XDG_CONFIG_HOME");
}

// A float-typed QVariant cannot be rendered as text by QSettings, so it lands
// as @Variant(...). This asserts the property for the value types the
// application actually persists: nothing binary in a settings file.
static void testNoBinaryVariants()
{
  QFile::remove(scratchPath());
  {
    QSettings s(scratchPath(), QSettings::IniFormat);
    s.setValue("Navigation/BlackThreshold", 0.98);      // double: plain
    s.setValue("Navigation/SceneThreshold", 0.8);
    s.setValue("Navigation/LogoThreshold",  0.1);
    s.setValue("StreamPoints/SilenceMinDuration", 0.3);
    s.sync();
  }
  QFile f(scratchPath());
  f.open(QIODevice::ReadOnly | QIODevice::Text);
  const QString text = QString::fromUtf8(f.readAll());
  check(!text.contains("@Variant"), "settings: doubles are written in the clear");
  check(text.contains("BlackThreshold=0.98"), "settings: value is editable text");

  // Widening an old float without rounding is the trap: it produces
  // 0.9800000190734863, which is readable but useless to edit.
  const double widened = double(0.98f);
  const double rounded = qRound(widened * 1000.0) / 1000.0;
  check(widened != 0.98, "settings: raw widening does NOT equal the literal");
  check(rounded == 0.98, "settings: rounding to 3 decimals restores it");
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);

  // --decode <file>: report what the legacy blob in <file> holds, then exit.
  // Reads only; the caller is expected to pass a copy.
  if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--decode") {
    QSettings s(QString::fromLocal8Bit(argv[2]), QSettings::IniFormat);
    const TTWindowGeometry g =
        ttDecodeGeometryBlob(s.value("MainWindow/geometry").toByteArray());
    printf("valid=%d rect=%d,%d %dx%d maximized=%d\n", g.valid,
           g.rect.x(), g.rect.y(), g.rect.width(), g.rect.height(), g.maximized);
    return g.valid ? 0 : 1;
  }

  testRoundTrip();
  testMaximizedRoundTrip();
  testMissingAndPartial();
  testClamp();
  testDialogSize();
  testBlobDecode();
  testMigration();
  testStrayImport();
  testNoBinaryVariants();

  QFile::remove(scratchPath());
  printf(failures ? "\nRESULT: FAIL (%d)\n" : "\nRESULT: PASS\n", failures);
  return failures ? 1 : 0;
}
