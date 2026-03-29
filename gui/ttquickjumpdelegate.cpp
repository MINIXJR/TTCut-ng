/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#include "ttquickjumpdelegate.h"
#include "ttquickjumpmodel.h"

#include <QPainter>
#include <QApplication>

TTQuickJumpDelegate::TTQuickJumpDelegate(int thumbWidth, int thumbHeight, QObject* parent)
  : QStyledItemDelegate(parent),
    mThumbWidth(thumbWidth),
    mThumbHeight(thumbHeight),
    mLabelHeight(28),
    mPadding(3),
    mHighlightFrameIndex(-1)
{
}

QSize TTQuickJumpDelegate::sizeHint(const QStyleOptionViewItem& /*option*/,
                                     const QModelIndex& /*index*/) const
{
  return QSize(mThumbWidth + 2 * mPadding,
               mThumbHeight + mLabelHeight + 2 * mPadding);
}

QSize TTQuickJumpDelegate::thumbnailSize() const
{
  return QSize(mThumbWidth, mThumbHeight);
}

void TTQuickJumpDelegate::setHighlightFrameIndex(int frameIndex)
{
  mHighlightFrameIndex = frameIndex;
}

void TTQuickJumpDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
  painter->save();

  // Selection/hover background
  if (option.state & QStyle::State_Selected) {
    painter->fillRect(option.rect, option.palette.highlight());
  } else if (option.state & QStyle::State_MouseOver) {
    QColor hover = option.palette.highlight().color();
    hover.setAlpha(60);
    painter->fillRect(option.rect, hover);
  }

  // Thumbnail area -- centered horizontally
  QRect thumbRect(
    option.rect.left() + (option.rect.width() - mThumbWidth) / 2,
    option.rect.top() + mPadding,
    mThumbWidth,
    mThumbHeight
  );

  // Get thumbnail pixmap
  QVariant decoration = index.data(Qt::DecorationRole);
  int frameIndex = index.data(TTQuickJumpModel::FrameIndexRole).toInt();

  if (decoration.isValid() && decoration.canConvert<QPixmap>()) {
    QPixmap thumbnail = decoration.value<QPixmap>();
    // Scale with aspect ratio, center within thumbRect
    QPixmap scaled = thumbnail.scaled(mThumbWidth, mThumbHeight,
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
    int xOffset = (mThumbWidth - scaled.width()) / 2;
    int yOffset = (mThumbHeight - scaled.height()) / 2;
    painter->drawPixmap(thumbRect.left() + xOffset, thumbRect.top() + yOffset, scaled);
  } else {
    // Check if decode failed via model's isFailedFrame()
    const TTQuickJumpModel* model = qobject_cast<const TTQuickJumpModel*>(index.model());
    bool isFailed = model && model->isFailedFrame(frameIndex);
    QColor placeholderColor = isFailed
        ? QColor(100, 30, 30)   // dark red -- decode failed
        : QColor(69, 71, 90);   // #45475a -- loading/pending
    painter->fillRect(thumbRect, placeholderColor);

    // Show frame number as placeholder text
    painter->setPen(QColor(108, 112, 134));  // #6c7086
    QFont smallFont = painter->font();
    smallFont.setPointSize(8);
    painter->setFont(smallFont);
    painter->drawText(thumbRect, Qt::AlignCenter, QString("[%1]").arg(frameIndex));
  }

  // Highlight outline for current position
  if (frameIndex == mHighlightFrameIndex) {
    painter->setPen(QPen(QColor(0x44, 0x88, 0xff), 2));  // #4488ff -- I-Frame blue
    painter->drawRect(thumbRect.adjusted(-1, -1, 1, 1));
  }

  // Label below thumbnail
  QRect labelRect(
    option.rect.left(),
    thumbRect.bottom() + 2,
    option.rect.width(),
    mLabelHeight
  );

  QString displayText = index.data(Qt::DisplayRole).toString();
  QStringList parts = displayText.split('\n');

  painter->setPen(option.palette.text().color());
  QFont labelFont = QApplication::font();
  labelFont.setPointSize(8);
  labelFont.setFamily("monospace");
  painter->setFont(labelFont);

  if (parts.size() >= 1) {
    painter->drawText(labelRect, Qt::AlignTop | Qt::AlignHCenter, parts.at(0));
  }
  if (parts.size() >= 2) {
    QColor dimColor(108, 112, 134);  // #6c7086
    painter->setPen(dimColor);
    QFont dimFont = labelFont;
    dimFont.setPointSize(7);
    painter->setFont(dimFont);
    QRect frameNumRect = labelRect.adjusted(0, 14, 0, 0);
    painter->drawText(frameNumRect, Qt::AlignTop | Qt::AlignHCenter, parts.at(1));
  }

  painter->restore();
}
