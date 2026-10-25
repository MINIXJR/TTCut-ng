// Render an SVG the way Qt draws theme icons: QIcon on the file, which goes
// through the svg icon engine plugin (libqsvgicon) - the path a Plasma panel,
// title bar or launcher takes for ttcut-ng.svg. tools/diag/gate_logo.sh
// compares the result with librsvg's render of the same file.
//
//   usage: test_logo_render <in.svg> <size> <out.png>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <cstdio>

int main(int argc, char** argv)
{
  QGuiApplication app(argc, argv);
  if (argc != 4) {
    std::fprintf(stderr, "usage: test_logo_render <in.svg> <size> <out.png>\n");
    return 2;
  }
  bool okSize = false;
  const int size = QString::fromLocal8Bit(argv[2]).toInt(&okSize);
  if (!okSize || size <= 0) {
    std::fprintf(stderr, "bad size: %s\n", argv[2]);
    return 2;
  }
  const QIcon icon(QString::fromLocal8Bit(argv[1]));
  if (icon.isNull()) {
    std::fprintf(stderr, "QIcon is null for %s (svg icon engine plugin missing?)\n", argv[1]);
    return 1;
  }
  const QImage img = icon.pixmap(QSize(size, size)).toImage();
  if (img.width() != size || img.height() != size) {
    std::fprintf(stderr, "got %dx%d, expected %dx%d\n", img.width(), img.height(), size, size);
    return 1;
  }
  if (!img.save(QString::fromLocal8Bit(argv[3]))) {
    std::fprintf(stderr, "cannot write %s\n", argv[3]);
    return 1;
  }
  return 0;
}
