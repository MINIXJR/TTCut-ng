#include "ttcutsettingsdlg.h"
#include "ttcutsettingsgeneral.h"
#include "ttcutsettingsnavigation.h"
#include "ttcutsettingssearch.h"
#include "ttcutsettingsaudio.h"
#include "ttcutsettingsencoderdefaults.h"
#include "ttcutsettingsmuxer.h"
#include "ttcutsettingspaths.h"
#include "ttcutsettingslogging.h"
#include "../common/ttsettings.h"

#include <QListWidgetItem>
#include <QIcon>
#include <QSettings>
#include <QStyle>
#include <QApplication>


TTSettingsDialog::TTSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
  setupUi(this);

  pageGeneral    = new TTCutSettingsGeneral(this);
  pageNavigation = new TTCutSettingsNavigation(this);
  pageSearch     = new TTCutSettingsSearch(this);
  pageAudio      = new TTCutSettingsAudio(this);
  pageEncoder    = new TTCutSettingsEncoderDefaults(this);
  pageMuxer      = new TTCutSettingsMuxer(this);
  pagePaths      = new TTCutSettingsPaths(this);
  pageLogging    = new TTCutSettingsLogging(this);

  // Load data into all pages
  pageGeneral->setTabData();
  pageNavigation->setTabData();
  pageSearch->setTabData();
  pageAudio->setTabData();
  pageEncoder->setTabData();
  pageMuxer->setTabData();
  pagePaths->setTabData();
  pageLogging->setTabData();

  // Populate sidebar list and stacked pages
  QStyle* s = QApplication::style();
  auto addCat = [this, s](const QString& title, const QIcon& icon, QWidget* page) {
    new QListWidgetItem(icon, title, categoryList);
    stackedPages->addWidget(page);
  };

  addCat(tr("Allgemein"),
         QIcon::fromTheme("preferences-system", s->standardIcon(QStyle::SP_ComputerIcon)),
         pageGeneral);
  addCat(tr("Navigation"),
         QIcon::fromTheme("go-jump", s->standardIcon(QStyle::SP_ArrowRight)),
         pageNavigation);
  addCat(tr("Suche & Preview"),
         QIcon::fromTheme("system-search", s->standardIcon(QStyle::SP_FileDialogContentsView)),
         pageSearch);
  addCat(tr("Audio & Sprache"),
         QIcon::fromTheme("audio-x-generic", s->standardIcon(QStyle::SP_MediaVolume)),
         pageAudio);
  addCat(tr("Encoder-Standards"),
         QIcon::fromTheme("applications-system", s->standardIcon(QStyle::SP_DriveCDIcon)),
         pageEncoder);
  addCat(tr("Multiplexen-Standards"),
         QIcon::fromTheme("applications-system", s->standardIcon(QStyle::SP_DriveHDIcon)),
         pageMuxer);
  addCat(tr("Pfade"),
         QIcon::fromTheme("folder", s->standardIcon(QStyle::SP_DirIcon)),
         pagePaths);
  addCat(tr("Logging"),
         QIcon::fromTheme("utilities-log-viewer", s->standardIcon(QStyle::SP_FileIcon)),
         pageLogging);

  connect(categoryList, &QListWidget::currentRowChanged,
          stackedPages, &QStackedWidget::setCurrentIndex);

  // Restore last-used category
  QSettings settings("TTCut-ng", "TTCut-ng");
  int lastRow = settings.value("SettingsDialog/lastCategory", 0).toInt();
  if (lastRow >= 0 && lastRow < categoryList->count())
    categoryList->setCurrentRow(lastRow);
  else
    categoryList->setCurrentRow(0);
}

TTSettingsDialog::~TTSettingsDialog()
{
  QSettings settings("TTCut-ng", "TTCut-ng");
  settings.setValue("SettingsDialog/lastCategory", categoryList->currentRow());
}

void TTSettingsDialog::accept()
{
  // Order: Encoder before Muxer (codec-dependent container logic)
  pageGeneral->saveTabData();
  pageNavigation->saveTabData();
  pageSearch->saveTabData();
  pageAudio->saveTabData();
  pageEncoder->saveTabData();
  pageMuxer->saveTabData();
  pagePaths->saveTabData();
  pageLogging->saveTabData();

  QDialog::accept();
}
