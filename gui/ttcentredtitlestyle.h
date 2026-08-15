/*----------------------------------------------------------------------------
 * COPYRIGHT: (c) 2005/2006 Tim Straubinger / (c) 2007/2008 b. altendorf
 *----------------------------------------------------------------------------
 * PROJEKT  : TTCUT 2008
 *----------------------------------------------------------------------------
 * FILE     : ttcentredtitlestyle.h
 *----------------------------------------------------------------------------
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *----------------------------------------------------------------------------
 */

#ifndef TTCENTREDTITLESTYLE_H
#define TTCENTREDTITLESTYLE_H

#include <QProxyStyle>

class QStyle;

/**
 * Centres QGroupBox titles application-wide without a stylesheet.
 *
 * TTCut-ng used to do this with a one-line application stylesheet
 * ("QGroupBox::title { subcontrol-position: top center; }"). That works, but
 * setting ANY stylesheet on the QApplication wraps the platform style in
 * QStyleSheetStyle for EVERY widget - and with the KDE styles that stops the
 * animation of an indeterminate QProgressBar, which is exactly what
 * TTProgressBar's stall pulse relies on. Measured with
 * tools/diag/test_pulse_stylesheet (paints per second on the bar itself):
 *
 *      style     no sheet      app stylesheet     this proxy
 *      Breeze    63.5          1.0  (static)      63.7
 *      Oxygen    63.8          1.0  (static)      63.8
 *      Fusion    60.4         60.7                60.8
 *      Windows   31.2         31.2                31.2
 *
 * The alignment itself lives in QStyleOptionGroupBox::textAlignment, so a
 * proxy can rewrite it on the way to the real style and leave everything else
 * - including the animation - to that style.
 *
 * Use install() rather than constructing this directly; it takes care of
 * rebuilding the currently active style as the proxy's base.
 */
class TTCentredTitleStyle : public QProxyStyle
{
  Q_OBJECT

  public:
    explicit TTCentredTitleStyle(QStyle* baseStyle);

    /**
     * Install the proxy on top of whatever style is active. Call after the
     * QApplication exists. Does nothing if the active style cannot be
     * recreated by name - see the .cpp for why that case must not centre.
     */
    static void install();

    void  drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                             QPainter* painter, const QWidget* widget) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option,
                         SubControl subControl, const QWidget* widget) const override;
};

#endif // TTCENTREDTITLESTYLE_H
