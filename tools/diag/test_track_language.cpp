// Track languages read from file names and shown in the per-row combo
// (audit run 8, docs/code-map/track-management.md H5):
//
//   - TTCut::langFromFilename passes the suffix through canonicalLangCode:
//     "ger" becomes "deu" (the code the preference list and the combo use),
//     a code TTCut does not know ("mul") stays as it is.
//   - TTCut::populateLanguageCombo selects the track's code; a code outside
//     the 30-entry list gets an entry of its own instead of showing "und".
//
//   usage: test_track_language
//
// Build via `cmake --build build --target test_track_language`.
#include <QApplication>
#include <QComboBox>

#include <cstdio>

#include "common/ttcut.h"

static int gFailures = 0;

static void check(bool ok, const QString& what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", qPrintable(what));
    if (!ok) gFailures++;
}

static void expectFileLang(const QString& file, const QString& want)
{
    const QString got = TTCut::langFromFilename(file);
    check(got == want, QString("langFromFilename(%1) = %2 (want %3)").arg(file, got, want));
}

static void expectCombo(const QString& lang, const QString& wantData, int wantCount)
{
    QComboBox combo;
    TTCut::populateLanguageCombo(&combo, lang);
    const QString data = combo.currentData().toString();
    check(data == wantData && combo.count() == wantCount,
          QString("combo for '%1': current %2, %3 entries (want %4, %5)")
              .arg(lang, data).arg(combo.count()).arg(wantData).arg(wantCount));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const int listSize = TTCut::languageCodes().size();

    expectFileLang("/x/Show_deu.ac3",   "deu");
    expectFileLang("/x/Show_ger.ac3",   "deu");
    expectFileLang("/x/Show_eng_1.mp2", "eng");
    expectFileLang("/x/Show_fre.srt",   "fra");
    expectFileLang("/x/Show_mul.mp2",   "mul");

    expectCombo("deu", "deu", listSize);
    expectCombo("qks", "qks", listSize);
    expectCombo("mul", "mul", listSize + 1);
    expectCombo("",    "und", listSize);

    printf("%s (%d failure%s)\n", gFailures ? "FAIL" : "PASS", gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
