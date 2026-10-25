// Unit test for ttAppIcon(): the window and About-dialog icon come from the
// PNGs bundled in mainwindow.qrc, one per size, so small sizes use the
// simplified drawing instead of a scaled-down large logo - and a stale
// ttcut-ng.png elsewhere on the system cannot replace them.
#include <QApplication>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QTemporaryDir>
#include <cstdio>

#include "../../gui/ttthemedicon.h"

static int failures = 0;
static void expect(const char* name, bool ok) {
    if (!ok) { failures++; printf("FAIL  %s\n", name); }
    else     printf("PASS  %s\n", name);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QIcon::setThemeSearchPaths({});         // no theme can answer for "ttcut-ng"
    QIcon::setFallbackSearchPaths({});

    const QIcon icon = ttAppIcon();
    const QList<QSize> sizes = icon.availableSizes();
    for (int n : {16, 22, 24, 32, 48, 64, 128, 256}) {
        char name[48];
        std::snprintf(name, sizeof name, "size_%d_bundled", n);
        expect(name, sizes.contains(QSize(n, n)));
    }
    const QPixmap p16 = icon.pixmap(QSize(16, 16));
    expect("pixmap_16_not_null", !p16.isNull() && p16.size() == QSize(16, 16));
    // the 16 px pixmap is the simplified 16 px drawing, not the 256 px logo
    // scaled down. Compared premultiplied: QPixmap stores premultiplied
    // colours, so half-transparent edge pixels do not survive a round trip
    // to straight alpha exactly.
    const auto premul = [](const QImage& i) { return i.convertToFormat(QImage::Format_ARGB32_Premultiplied); };
    const QImage want(":/pixmaps/pixmaps/icons/16/ttcut-ng.png");
    const QImage large(":/pixmaps/pixmaps/icons/256/ttcut-ng.png");
    expect("pixmap_16_is_small_drawing", !want.isNull() && premul(p16.toImage()) == premul(want));
    expect("small_drawing_differs_from_scaled_large",
           premul(want) != premul(large.scaled(16, 16, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
    const QPixmap p128 = icon.pixmap(QSize(128, 128));
    expect("pixmap_128_not_null", !p128.isNull() && p128.size() == QSize(128, 128));

    // A stale ttcut-ng.png in Qt's fallback search path (an older package put
    // the previous logo into /usr/share/pixmaps) must not replace the bundled
    // icon - measured 2026-10-25: QIcon::fromTheme found exactly that file.
    QTemporaryDir stale;
    QImage red(16, 16, QImage::Format_ARGB32);
    red.fill(Qt::red);
    red.save(stale.filePath("ttcut-ng.png"));
    QIcon::setFallbackSearchPaths({stale.path()});
    expect("stale_fallback_ignored",
           stale.isValid() && premul(ttAppIcon().pixmap(QSize(16, 16)).toImage()) == premul(want));

    printf("%s (%d failed)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
