/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#include "ttlogodetector.h"
#include <QFile>
#include <QtMath>
#include <QDebug>

TTLogoDetector::TTLogoDetector()
  : mSampleCount(0),
    mFinalized(false),
    mEdgeWidth(0),
    mEdgeHeight(0)
{
}

bool TTLogoDetector::loadMarkadLogo(const QString& pgmPath,
                                     std::function<QImage(int)> decodeFrame,
                                     std::function<int(int)> nextIFrame,
                                     int startPos,
                                     std::function<void(int, int)> progressFn)
{
  QFile file(pgmPath);
  if (!file.open(QIODevice::ReadOnly)) return false;

  QByteArray magic = file.readLine().trimmed();
  if (magic != "P5") { file.close(); return false; }

  int corner = -1;
  int logoW = 0, logoH = 0;

  while (!file.atEnd()) {
    QByteArray line = file.readLine().trimmed();
    if (line.startsWith('#')) {
      if (line.startsWith("#C") && line.size() >= 3)
        corner = line.mid(2).toInt();
      continue;
    }
    QList<QByteArray> parts = line.split(' ');
    if (parts.size() >= 2) {
      logoW = parts[0].toInt();
      logoH = parts[1].toInt();
    }
    break;
  }
  file.readLine();  // maxval

  if (corner < 0 || corner > 3 || logoW < 4 || logoH < 4) {
    file.close();
    return false;
  }

  QByteArray pixelData = file.readAll();
  file.close();
  if (pixelData.size() < logoW * logoH) return false;

  QVector<float> logoTemplate(logoW * logoH);
  for (int i = 0; i < logoW * logoH; ++i)
    logoTemplate[i] = (float)(255 - (uchar)pixelData[i]);

  // Determine video resolution from first decoded frame
  if (progressFn) progressFn(1, 1);

  int firstPos = nextIFrame(startPos);
  if (firstPos < 0) return false;

  QImage firstFrame = decodeFrame(firstPos);
  if (firstFrame.isNull()) return false;

  int videoWidth = firstFrame.width();
  int videoHeight = firstFrame.height();

  // Place logo directly at the corner — markad trims the logo to its minimal
  // bounding box flush with the corner edge, so offset is (0,0).
  int roiX = 0, roiY = 0;
  switch (corner) {
    case 0: roiX = 0;                  roiY = 0; break;                   // TOP_LEFT
    case 1: roiX = videoWidth - logoW;  roiY = 0; break;                  // TOP_RIGHT
    case 2: roiX = 0;                  roiY = videoHeight - logoH; break;  // BOTTOM_LEFT
    case 3: roiX = videoWidth - logoW;  roiY = videoHeight - logoH; break; // BOTTOM_RIGHT
  }

  mROI = QRect(roiX, roiY, logoW, logoH);

  int ow = logoW - 2;
  int oh = logoH - 2;
  mProfile.resize(ow * oh);
  for (int y = 1; y < logoH - 1; ++y)
    for (int x = 1; x < logoW - 1; ++x)
      mProfile[(y - 1) * ow + (x - 1)] = logoTemplate[y * logoW + x];

  mEdgeWidth = ow;
  mEdgeHeight = oh;
  mFinalized = true;
  mSampleCount = 1;

  qDebug() << "Loaded markad logo:" << pgmPath
           << logoW << "x" << logoH << "corner" << corner
           << "ROI" << mROI;

  return true;
}

void TTLogoDetector::setROI(const QRect& roiInImageCoords)
{
  mROI = roiInImageCoords;
  clearProfile();
}

QRect TTLogoDetector::roi() const
{
  return mROI;
}

bool TTLogoDetector::hasROI() const
{
  return mROI.isValid() && mROI.width() > 4 && mROI.height() > 4;
}

void TTLogoDetector::addEdgeSample(const QImage& fullFrame)
{
  if (!hasROI()) return;

  QImage gray = extractGrayscaleROI(fullFrame);
  if (gray.isNull()) return;

  QVector<float> edge = sobelEdge(gray);
  if (edge.isEmpty()) return;

  if (mProfileAccum.isEmpty()) {
    mEdgeWidth = gray.width() - 2;
    mEdgeHeight = gray.height() - 2;
    mProfileAccum.resize(edge.size());
    mProfileAccum.fill(0.0);
    mEdgeHitCount.resize(edge.size());
    mEdgeHitCount.fill(0);
  }

  if (edge.size() != mProfileAccum.size()) return;

  // Edge threshold: pixel is "significant edge" if magnitude > 30
  const float edgeThreshold = 30.0f;

  for (int i = 0; i < edge.size(); ++i) {
    mProfileAccum[i] += (double)edge[i];
    if (edge[i] > edgeThreshold)
      mEdgeHitCount[i]++;
  }

  mSampleCount++;
}

void TTLogoDetector::finalizeProfile()
{
  if (mSampleCount == 0 || mProfileAccum.isEmpty()) return;

  // Only keep edges that appeared in >= 70% of frames (persistent = logo).
  // Transient edges (background content) are zeroed out.
  int minHits = qMax(1, (int)(mSampleCount * 0.7f));

  mProfile.resize(mProfileAccum.size());
  for (int i = 0; i < mProfileAccum.size(); ++i) {
    if (mEdgeHitCount[i] >= minHits)
      mProfile[i] = (float)(mProfileAccum[i] / mSampleCount);
    else
      mProfile[i] = 0.0f;
  }

  mFinalized = true;
}

bool TTLogoDetector::hasProfile() const
{
  return mFinalized && !mProfile.isEmpty();
}

void TTLogoDetector::clearProfile()
{
  mProfile.clear();
  mProfileAccum.clear();
  mEdgeHitCount.clear();
  mSampleCount = 0;
  mFinalized = false;
  mEdgeWidth = 0;
  mEdgeHeight = 0;
}

int TTLogoDetector::sampleCount() const
{
  return mSampleCount;
}

float TTLogoDetector::matchScore(const QImage& fullFrame) const
{
  if (!hasProfile() || !hasROI()) return 0.0f;

  QImage gray = extractGrayscaleROI(fullFrame);
  if (gray.isNull()) return 0.0f;

  QVector<float> edge = sobelEdge(gray);
  if (edge.size() != mProfile.size()) return 0.0f;

  return computeNCC(mProfile, edge);
}

QImage TTLogoDetector::extractGrayscaleROI(const QImage& fullFrame) const
{
  if (fullFrame.isNull()) return QImage();

  QRect clipped = mROI.intersected(fullFrame.rect());
  if (clipped.width() < 4 || clipped.height() < 4) return QImage();

  QImage roi = fullFrame.copy(clipped);
  return roi.convertToFormat(QImage::Format_Grayscale8);
}

QVector<float> TTLogoDetector::sobelEdge(const QImage& gray) const
{
  int w = gray.width();
  int h = gray.height();
  if (w < 3 || h < 3) return QVector<float>();

  int ow = w - 2;
  int oh = h - 2;
  QVector<float> result(ow * oh);

  for (int y = 1; y < h - 1; ++y) {
    const uchar* row0 = gray.constScanLine(y - 1);
    const uchar* row1 = gray.constScanLine(y);
    const uchar* row2 = gray.constScanLine(y + 1);

    for (int x = 1; x < w - 1; ++x) {
      int gx = -row0[x-1] + row0[x+1]
               -2*row1[x-1] + 2*row1[x+1]
               -row2[x-1] + row2[x+1];

      int gy = -row0[x-1] - 2*row0[x] - row0[x+1]
               +row2[x-1] + 2*row2[x] + row2[x+1];

      result[(y-1) * ow + (x-1)] = (float)(qAbs(gx) + qAbs(gy));
    }
  }

  return result;
}

float TTLogoDetector::computeNCC(const QVector<float>& a, const QVector<float>& b) const
{
  int n = a.size();
  if (n == 0 || n != b.size()) return 0.0f;

  double sumA = 0.0, sumB = 0.0;
  for (int i = 0; i < n; ++i) {
    sumA += a[i];
    sumB += b[i];
  }
  double meanA = sumA / n;
  double meanB = sumB / n;

  double num = 0.0, denomA = 0.0, denomB = 0.0;
  for (int i = 0; i < n; ++i) {
    double da = a[i] - meanA;
    double db = b[i] - meanB;
    num += da * db;
    denomA += da * da;
    denomB += db * db;
  }

  double denom = qSqrt(denomA * denomB);
  if (denom < 1e-10) return 0.0f;

  return (float)(num / denom);
}
