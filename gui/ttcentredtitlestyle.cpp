/*----------------------------------------------------------------------------
 * COPYRIGHT: (c) 2005/2006 Tim Straubinger / (c) 2007/2008 b. altendorf
 *----------------------------------------------------------------------------
 * PROJEKT  : TTCUT 2008
 *----------------------------------------------------------------------------
 * FILE     : ttcentredtitlestyle.cpp
 *----------------------------------------------------------------------------
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *----------------------------------------------------------------------------
 */

#include "ttcentredtitlestyle.h"

#include "../common/ttmessagelogger.h"

#include <QApplication>
#include <QStyleFactory>
#include <QStyleOptionComplex>
#include <QStyleOptionGroupBox>

TTCentredTitleStyle::TTCentredTitleStyle(QStyle* baseStyle)
  : QProxyStyle(baseStyle)
{
}

/**
 * Rebuild the active style by name and put the proxy on top of that new
 * instance. Passing QApplication::style() itself is not an option: setStyle()
 * deletes the previous style, which the proxy would then hold as a dangling
 * base.
 *
 * If the active style's objectName is not a QStyleFactory key - possible for a
 * third-party style shipped by a distribution or desktop - create() returns
 * nullptr. A QProxyStyle built on nullptr silently falls back to the DEFAULT
 * style, i.e. it would replace the look the user chose. Centred titles are not
 * worth that, so in this case nothing is installed and the titles stay wherever
 * the user's style puts them.
 */
void TTCentredTitleStyle::install()
{
  const QString activeStyle = QApplication::style()->objectName();
  QStyle*       base        = QStyleFactory::create(activeStyle);

  if (base == nullptr) {
    TTMessageLogger::getInstance()->warningMsg(__FILE__, __LINE__,
        QString("Style '%1' is not a QStyleFactory key - group box titles are "
                "left as this style draws them.").arg(activeStyle));
    return;
  }

  QApplication::setStyle(new TTCentredTitleStyle(base));
}

void TTCentredTitleStyle::drawComplexControl(ComplexControl control,
                                             const QStyleOptionComplex* option,
                                             QPainter* painter,
                                             const QWidget* widget) const
{
  if (control == CC_GroupBox) {
    if (const auto* groupBox = qstyleoption_cast<const QStyleOptionGroupBox*>(option)) {
      QStyleOptionGroupBox centred(*groupBox);
      centred.textAlignment = Qt::AlignHCenter;
      QProxyStyle::drawComplexControl(control, &centred, painter, widget);
      return;
    }
  }

  QProxyStyle::drawComplexControl(control, option, painter, widget);
}

/**
 * The title rect is laid out from the same alignment. Without this override
 * the text would be painted centred but clipped to a left-aligned rectangle.
 */
QRect TTCentredTitleStyle::subControlRect(ComplexControl control,
                                          const QStyleOptionComplex* option,
                                          SubControl subControl,
                                          const QWidget* widget) const
{
  if (control == CC_GroupBox) {
    if (const auto* groupBox = qstyleoption_cast<const QStyleOptionGroupBox*>(option)) {
      QStyleOptionGroupBox centred(*groupBox);
      centred.textAlignment = Qt::AlignHCenter;
      return QProxyStyle::subControlRect(control, &centred, subControl, widget);
    }
  }

  return QProxyStyle::subControlRect(control, option, subControl, widget);
}
