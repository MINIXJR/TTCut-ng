/*----------------------------------------------------------------------------*/
/* COPYRIGHT: MINIXJR (c) 2024-2026 / TTCut-ng                               */
/*----------------------------------------------------------------------------*/

#ifndef TTLOGODETECTOR_H
#define TTLOGODETECTOR_H

#include <QRect>
#include <QImage>
#include <QSize>
#include <QVector>
#include <functional>

class TTLogoDetector
{
public:
  TTLogoDetector();

  // markad PGM import (primary path)
  // Video resolution is determined from the first decoded frame automatically.
  // progressFn is called after each decoded frame with (current, total).
  bool loadMarkadLogo(const QString& pgmPath,
                      std::function<QImage(int frameIndex)> decodeFrame,
                      std::function<int(int pos)> nextIFrame,
                      int startPos,
                      std::function<void(int current, int total)> progressFn = nullptr);

  // ROI management (manual fallback)
  void setROI(const QRect& roiInImageCoords);
  QRect roi() const;
  bool hasROI() const;

  // Profile management (manual fallback)
  void addEdgeSample(const QImage& fullFrame);
  void finalizeProfile();
  bool hasProfile() const;
  void clearProfile();
  int sampleCount() const;

  // Matching
  float matchScore(const QImage& fullFrame) const;

private:
  QImage extractGrayscaleROI(const QImage& fullFrame) const;
  QVector<float> sobelEdge(const QImage& gray) const;
  float computeNCC(const QVector<float>& a, const QVector<float>& b) const;

private:
  QRect           mROI;
  QVector<float>  mProfile;
  QVector<double> mProfileAccum;
  QVector<int>    mEdgeHitCount;   // per-pixel: in how many frames was a significant edge present
  int             mSampleCount;
  bool            mFinalized;
  int             mEdgeWidth;
  int             mEdgeHeight;
};

#endif // TTLOGODETECTOR_H
