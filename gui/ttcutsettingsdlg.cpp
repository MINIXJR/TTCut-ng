#include "ttcutsettingsdlg.h"
#include "ttcutsettingsnavigation.h"
#include "ttcutsettingssearch.h"
#include "ttcutsettingsaudio.h"
#include "ttcutsettingsencoderdefaults.h"
#include "ttcutsettingsmuxer.h"
#include "ttcutsettingspaths.h"
#include "ttcutsettingslogging.h"

#include <QListWidgetItem>
#include <QIcon>
#include <QSettings>
#include <QStyle>
#include <QApplication>


TTSettingsDialog::TTSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
  setupUi(this);

  pageNavigation = new TTCutSettingsNavigation(this);
  pageSearch     = new TTCutSettingsSearch(this);
  pageAudio      = new TTCutSettingsAudio(this);
  pageEncoder    = new TTCutSettingsEncoderDefaults(this);
  pageMuxer      = new TTCutSettingsMuxer(this);
  pagePaths      = new TTCutSettingsPaths(this);
  pageLogging    = new TTCutSettingsLogging(this);

  // Load data into all pages
  pageNavigation->setTabData();
  pageSearch->setTabData();
  pageAudio->setTabData();
  pageEncoder->setTabData();
  pageMuxer->setTabData();
  pagePaths->setTabData();
  pageLogging->setTabData();

  // Populate sidebar list and stacked pages — order: UI interaction →
  // processing → output → system. Bedienung first, Logging last.
  QStyle* s = QApplication::style();
  auto addCat = [this](const QString& title, const QIcon& icon, QWidget* page) {
    new QListWidgetItem(icon, title, categoryList);
    stackedPages->addWidget(page);
  };

  addCat(tr("Navigation"),
         QIcon::fromTheme("go-jump", s->standardIcon(QStyle::SP_ArrowRight)),
         pageNavigation);
  addCat(tr("Search & Preview"),
         QIcon::fromTheme("system-search", s->standardIcon(QStyle::SP_FileDialogContentsView)),
         pageSearch);
  addCat(tr("Audio & Language"),
         QIcon::fromTheme("audio-x-generic", s->standardIcon(QStyle::SP_MediaVolume)),
         pageAudio);
  addCat(tr("Encoder"),
         QIcon::fromTheme("applications-system", s->standardIcon(QStyle::SP_DriveCDIcon)),
         pageEncoder);
  addCat(tr("Multiplexing"),
         QIcon::fromTheme("applications-system", s->standardIcon(QStyle::SP_DriveHDIcon)),
         pageMuxer);
  addCat(tr("Paths"),
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
  pageNavigation->saveTabData();
  pageSearch->saveTabData();
  pageAudio->saveTabData();
  pageEncoder->saveTabData();
  pageMuxer->saveTabData();
  pagePaths->saveTabData();
  pageLogging->saveTabData();

  QDialog::accept();
}
