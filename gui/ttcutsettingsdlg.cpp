/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

#include "ttcutsettingsdlg.h"
#include "ttthemedicon.h"
#include "ttcutsettingsnavigation.h"
#include "ttcutsettingssearch.h"
#include "ttcutsettingsaudio.h"
#include "ttcutsettingsencoderdefaults.h"
#include "ttcutsettingsmuxer.h"
#include "ttcutsettingspaths.h"
#include "ttcutsettingslogging.h"
#include "ttcutsettingsstreampoints.h"

#include <QListWidgetItem>
#include <QPushButton>
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
  pageStreamPoints = new TTCutSettingsStreamPoints(this);

  // Load data into all pages
  pageNavigation->setTabData();
  pageSearch->setTabData();
  pageAudio->setTabData();
  pageEncoder->setTabData();
  pageMuxer->setTabData();
  pagePaths->setTabData();
  pageLogging->setTabData();
  pageStreamPoints->setTabData();

  // Populate sidebar list and stacked pages — order: UI interaction →
  // processing → output → system. Bedienung first, Logging last.
  auto addCat = [this](const QString& title, const QIcon& icon, QWidget* page) {
    new QListWidgetItem(icon, title, categoryList);
    stackedPages->addWidget(page);
  };

  addCat(tr("Navigation"),
         ttThemedIcon("go-jump", QStyle::SP_ArrowRight),
         pageNavigation);
  addCat(tr("Search & Preview"),
         ttThemedIcon("system-search", QStyle::SP_FileDialogContentsView),
         pageSearch);
  addCat(tr("Audio & Language"),
         ttThemedIcon("audio-x-generic", QStyle::SP_MediaVolume),
         pageAudio);
  addCat(tr("Encoder"),
         ttThemedIcon("applications-system", QStyle::SP_DriveCDIcon),
         pageEncoder);
  addCat(tr("Multiplexing"),
         ttThemedIcon("applications-system", QStyle::SP_DriveHDIcon),
         pageMuxer);
  addCat(tr("Paths"),
         ttThemedIcon("folder", QStyle::SP_DirIcon),
         pagePaths);
  addCat(tr("Logging"),
         ttThemedIcon("utilities-log-viewer", QStyle::SP_FileIcon),
         pageLogging);
  addCat(tr("Stream Points"),
         ttThemedIcon("edit-find", QStyle::SP_FileDialogContentsView),
         pageStreamPoints);

  connect(categoryList, &QListWidget::currentRowChanged,
          stackedPages, &QStackedWidget::setCurrentIndex);

  // Restore last-used category
  QSettings settings("TTCut-ng", "TTCut-ng");
  int lastRow = settings.value("SettingsDialog/lastCategory", 0).toInt();
  if (lastRow >= 0 && lastRow < categoryList->count())
    categoryList->setCurrentRow(lastRow);
  else
    categoryList->setCurrentRow(0);

  // No Enter default anywhere in this dialog. Save persists immediately (there
  // is no explicit apply step), so Enter must not trigger it by accident when
  // the user only means to confirm a field value. With no explicit default Qt
  // would promote the first autoDefault button it finds — a per-tab "Reset to
  // defaults" button — to the dialog default, which is worse. Clearing
  // autoDefault on every button leaves Enter inert (Escape still rejects via
  // the QDialog default) and Save reachable only by an explicit click.
  const auto buttons = findChildren<QPushButton*>();
  for (QPushButton* button : buttons)
    button->setAutoDefault(false);
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
  pageStreamPoints->saveTabData();

  QDialog::accept();
}
