/*----------------------------------------------------------------------------*/
/* COPYRIGHT: TriTime (c) 2003/2008 / ttcut.tritime.org                       */
/*----------------------------------------------------------------------------*/
/* PROJEKT  : TTCUT 2005                                                      */
/* FILE     : ttcutsettingscommon.cpp                                         */
/*----------------------------------------------------------------------------*/
/* AUTHOR  : b. altendorf (E-Mail: b.altendorf@tritime.de)   DATE: 02/26/2006 */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// *** TTCUTSETTINGSCOMMON
// ----------------------------------------------------------------------------

/*----------------------------------------------------------------------------*/
/* This program is free software; you can redistribute it and/or modify it    */
/* under the terms of the GNU General Public License as published by the Free */
/* Software Foundation;                                                       */
/* either version 3 of the License, or (at your option) any later version.    */
/*                                                                            */
/* This program is distributed in the hope that it will be useful, but WITHOUT*/
/* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or      */
/* FITNESS FOR A PARTICULAR PURPOSE.                                          */
/* See the GNU General Public License for more details.                       */
/*                                                                            */
/* You should have received a copy of the GNU General Public License along    */
/* with this program; if not, write to the Free Software Foundation,          */
/* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.              */
/*----------------------------------------------------------------------------*/

#include "ttcutsettingscommon.h"

#include "../common/ttcut.h"
#include "../common/ttsettings.h"

#include <QFileDialog>
#include <QDir>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>

TTCutSettingsCommon::TTCutSettingsCommon(QWidget* parent)
:QWidget(parent)
{
  setupUi(this);

  // Audio burst threshold spinbox (programmatically added)
  sbBurstThreshold = new QSpinBox(this);
  sbBurstThreshold->setRange(-60, 0);
  sbBurstThreshold->setSuffix(" dB");
  QLabel* lblBurst = new QLabel(tr("Audio-Burst Threshold (dB, 0=off)"), this);
  QGridLayout* gl = qobject_cast<QGridLayout*>(layout());
  if (gl) {
    int row = gl->rowCount();
    gl->addWidget(lblBurst, row, 0);
    gl->addWidget(sbBurstThreshold, row, 1);
  }

  // Zeitsprung interval spinbox
  sbQuickJumpInterval = new QSpinBox(this);
  sbQuickJumpInterval->setRange(0, 600);
  sbQuickJumpInterval->setSuffix(" s");
  sbQuickJumpInterval->setSpecialValueText(tr("All keyframes"));
  QLabel* lblQuickJump = new QLabel(tr("Zeitsprung interval (0=all)"), this);
  if (gl) {
    int row3 = gl->rowCount();
    gl->addWidget(lblQuickJump, row3, 0);
    gl->addWidget(sbQuickJumpInterval, row3, 1);
  }

  // AC3 acmod normalization checkbox
  cbNormalizeAcmod = new QCheckBox(tr("Normalize AC3 channel format at cuts"), this);
  cbNormalizeAcmod->setToolTip(tr("Re-encode AC3 frames at cut boundaries when channel format changes (e.g. stereo/5.1)"));
  if (gl) {
    int row4 = gl->rowCount();
    gl->addWidget(cbNormalizeAcmod, row4, 0, 1, 2);
  }

  // Audio language preference
  leAudioLangPref = new QLineEdit(this);
  leAudioLangPref->setPlaceholderText(tr("e.g. deu,eng,fra"));
  leAudioLangPref->setToolTip(tr(
      "Comma-separated audio language codes (2- or 3-letter). "
      "Empty = use system locale. Unknown codes are silently dropped."));
  QLabel* lblAudioLangPref = new QLabel(tr("Audio language preference"), this);
  if (gl) {
    int rowLang = gl->rowCount();
    gl->addWidget(lblAudioLangPref, rowLang, 0);
    gl->addWidget(leAudioLangPref, rowLang, 1, 1, 2);
  }

  // Defective frame grouping
  QGroupBox* gbCluster = new QGroupBox(tr("Defective Frame Grouping"), this);
  QGridLayout* clusterLayout = new QGridLayout(gbCluster);

  sbClusterGap = new QSpinBox(gbCluster);
  sbClusterGap->setRange(1, 30);
  sbClusterGap->setSuffix(tr(" seconds"));
  QLabel* lblClusterGap = new QLabel(tr("Group size"), gbCluster);
  clusterLayout->addWidget(lblClusterGap, 0, 0);
  clusterLayout->addWidget(sbClusterGap, 0, 1);

  sbClusterOffset = new QSpinBox(gbCluster);
  sbClusterOffset->setRange(0, 10);
  sbClusterOffset->setSuffix(tr(" seconds"));
  QLabel* lblClusterOffset = new QLabel(tr("Start offset"), gbCluster);
  clusterLayout->addWidget(lblClusterOffset, 1, 0);
  clusterLayout->addWidget(sbClusterOffset, 1, 1);

  if (gl) {
    int rowCluster = gl->rowCount();
    gl->addWidget(gbCluster, rowCluster, 0, 1, 3);
  }

  // Preview I-frame hint
  QLabel* lblPreviewHint = new QLabel(
      tr("Preview starts at an I-frame for each cut."), this);
  lblPreviewHint->setStyleSheet("QLabel { color: #666; font-style: italic; }");
  lblPreviewHint->setWordWrap(true);
  if (gl) {
    int row2 = gl->rowCount();
    gl->addWidget(lblPreviewHint, row2, 0, 1, 3);
  }

  connect(btnDirOpen, &QPushButton::clicked, this, &TTCutSettingsCommon::onOpenDir);
}

void TTCutSettingsCommon::setTitle(__attribute__((unused))const QString& title)
{
}

// select a temp directory path
void TTCutSettingsCommon::onOpenDir()
{
  QString str_dir = QFileDialog::getExistingDirectory( this,
						       "Select temporary directory",
						       TTSettings::instance()->tempDirPath(),
						       (QFileDialog::DontResolveSymlinks |
							QFileDialog::ShowDirsOnly) );

  if ( !str_dir.isEmpty() )
  {
    TTSettings::instance()->setTempDirPath(str_dir);

    leTempDirectory->setText( TTSettings::instance()->tempDirPath() );
  }
}

// set the tab data from the global parameter
void TTCutSettingsCommon::setTabData()
{
  // --------------------------------------------------------------
  // common settings
  // --------------------------------------------------------------

  // Navigation
  sbSliderClickPlacement->setValue( TTSettings::instance()->stepSliderClick() );
  sbPgUpDown->setValue( TTSettings::instance()->stepPgUpDown() );
  sbArrowKeyPlacement->setValue( TTSettings::instance()->stepArrowKeys() );
  sbAltDistance->setValue( TTSettings::instance()->stepPlusAlt() );
  sbCtrlDistance->setValue( TTSettings::instance()->stepPlusCtrl() );
  sbMouseWheel->setValue( TTSettings::instance()->stepMouseWheel() );

  // Preview
  spPreviewLength->setValue( TTSettings::instance()->cutPreviewSeconds() );

  // Frame search
  sbSearchIntervall->setValue( TTSettings::instance()->searchLength() );
  sbSkipFrames->setValue( TTSettings::instance()->playSkipFrames() );

  // Options, directories
  cbQuickSearch->setChecked(TTSettings::instance()->fastSlider());
  leTempDirectory->setText( TTSettings::instance()->tempDirPath() );

  // Audio burst detection
  sbBurstThreshold->setValue(TTSettings::instance()->burstThresholdDb());

  // AC3 acmod normalization
  cbNormalizeAcmod->setChecked(TTSettings::instance()->normalizeAcmod());

  // Zeitsprung
  sbQuickJumpInterval->setValue(TTSettings::instance()->quickJumpIntervalSec());

  // Gruppierung defekter Frames
  sbClusterGap->setValue(TTCut::extraFrameClusterGapSec);
  sbClusterOffset->setValue(TTCut::extraFrameClusterOffsetSec);

  // Audio language preference
  leAudioLangPref->setText(TTSettings::instance()->audioLanguagePreference().join(","));
}

// get the tab data and fill the global parameter
void TTCutSettingsCommon::getTabData()
{
  // Navigation
  TTSettings::instance()->setStepSliderClick(sbSliderClickPlacement->value( ));
  TTSettings::instance()->setStepPgUpDown(   sbPgUpDown->value( ));
  TTSettings::instance()->setStepArrowKeys(  sbArrowKeyPlacement->value( ));
  TTSettings::instance()->setStepPlusAlt(    sbAltDistance->value( ));
  TTSettings::instance()->setStepPlusCtrl(   sbCtrlDistance->value( ));
  TTSettings::instance()->setStepMouseWheel( sbMouseWheel->value( ));

  // Preview
  TTSettings::instance()->setCutPreviewSeconds(spPreviewLength->value( ));

  // Frame search
  TTSettings::instance()->setSearchLength(sbSearchIntervall->value( ));
  TTSettings::instance()->setPlaySkipFrames(sbSkipFrames->value( ));

  // Options, directories
  TTSettings::instance()->setFastSlider(cbQuickSearch->isChecked());
  TTSettings::instance()->setTempDirPath(leTempDirectory->text( ));

  if ( !QDir(TTSettings::instance()->tempDirPath()).exists() )
    TTSettings::instance()->setTempDirPath(QDir::tempPath());

  // Audio burst detection
  TTSettings::instance()->setBurstThresholdDb(sbBurstThreshold->value());

  // AC3 acmod normalization
  TTSettings::instance()->setNormalizeAcmod(cbNormalizeAcmod->isChecked());

  // Zeitsprung
  TTSettings::instance()->setQuickJumpIntervalSec(sbQuickJumpInterval->value());

  // Gruppierung defekter Frames
  TTCut::extraFrameClusterGapSec    = sbClusterGap->value();
  TTCut::extraFrameClusterOffsetSec = sbClusterOffset->value();

  // Audio language preference — parse, normalize, drop empties/unknowns
  // Read-modify-write through the setter so audioLanguagePreferenceChanged
  // fires once per dialog apply and the legacy mirror stays consistent.
  QStringList newPrefs;
  QStringList rawEntries = leAudioLangPref->text().split(',', Qt::SkipEmptyParts);
  for (const QString& raw : rawEntries) {
    QString normalized = TTCut::normalizeLangCode(raw);
    if (!normalized.isEmpty()) {
      newPrefs.append(normalized);
    }
  }
  TTSettings::instance()->setAudioLanguagePreference(newPrefs);
}



