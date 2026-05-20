/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-3.0-or-later                                  */
/*                                                                            */
/* TTCut-ng - frame-accurate video cutter                                     */
/* Copyright (c) 2024-2026 MINIXJR                                            */
/* Originally TTCut (c) 2003-2010 B. Altendorf / TriTime                      */
/*                                                                            */
/* Free software under the GNU GPL v3 or later - see the LICENSE file.        */
/*----------------------------------------------------------------------------*/

// ----------------------------------------------------------------------------
// TTVIDEOPLAYER
// ----------------------------------------------------------------------------

#include "ttvideoplayer.h"

#include <QLayout>

/* /////////////////////////////////////////////////////////////////////////////
 * Constructor for MovieWidget
 */
TTVideoPlayer::TTVideoPlayer(QWidget *parent)
    : QWidget(parent)
{
  currentMovie       = "";
  mIsPlaying         = false;
  areControlsVisible = false;
}

