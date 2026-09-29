// PDFMark - Tile layout geometry and "no white margin" regression test.
//
// The watermark grid deliberately leaves gaps between tiles (2x font height
// vertically, 2x average char width horizontally). If the grid is anchored on
// the page diagonal, those gaps can fall exactly on a page border and leave a
// blank band running along it — measured before the fix, on the rendered page:
//
//   rotation    0°  -> 63 px blank at the bottom (A4 @200 DPI), 114 px (Letter @300)
//   rotation ±90°  -> 181 px blank on one side
//   rotation 180°  -> 63 px blank at the top
//
// The fix anchors the grid on the page's bounding box in the rotated frame and
// clamps the last row/column onto the far border. This test guards both forms of
// the property: structurally (some tile box must cross every border) and on the
// rendered pixels (the ink must come closer to every border than half a glyph
// height).
#include "watermark/WatermarkTileLayout.h"
#include "watermark/WatermarkRenderer.h"

#include <cassert>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QString>

namespace pdfmark {

namespace {

enum class Side { Top, Bottom, Left, Right };

const char* sideName(Side s) {
    switch (s) {
        case Side::Top: return "top";
        case Side::Bottom: return "bottom";
        case Side::Left: return "left";
        case Side::Right: return "right";
    }
    return "?";
}

QFont fontFor(const WatermarkConfig& cfg) {
    QFont font("Arial");
    font.setPixelSize(WatermarkTileLayout::calculatePixelFontSize(cfg.fontSizePt, cfg.dpi));
    return font;
}

// Corners of a tile's rotated text box, in page coordinates.
std::array<QPointF, 4> tileCorners(const TileItem& t) {
    const double rad = t.rotationDeg * (std::numbers::pi / 180.0);
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const double hw = t.textBounds.width() / 2.0;
    const double hh = t.textBounds.height() / 2.0;
    const QPointF local[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
    std::array<QPointF, 4> out;
    for (int i = 0; i < 4; ++i) {
        out[i] = QPointF(t.center.x() + local[i].x() * c - local[i].y() * s,
                         t.center.y() + local[i].x() * s + local[i].y() * c);
    }
    return out;
}

// Distance from the given page border to the nearest tile box: 0 as soon as some
// box touches or crosses it (the property that prevents a blank band), otherwise
// how far the closest box stops short of it. Measured on the rotated rectangle,
// not on its axis-aligned bounds — the bounds over-estimate coverage, which is
// exactly the kind of error that lets a real gap slip through.
double borderCoverageGap(const std::vector<TileItem>& tiles, int w, int h, Side side) {
    double best = 1e18;
    for (const auto& t : tiles) {
        const auto pts = tileCorners(t);
        double lo = 1e18, hi = -1e18;      // extent along the border
        double near_ = 1e18;               // closest approach to the border line
        for (const auto& p : pts) {
            switch (side) {
                case Side::Top:
                case Side::Bottom:
                    lo = std::min(lo, p.x()); hi = std::max(hi, p.x());
                    near_ = std::min(near_, (side == Side::Top) ? p.y() : h - p.y());
                    break;
                case Side::Left:
                case Side::Right:
                    lo = std::min(lo, p.y()); hi = std::max(hi, p.y());
                    near_ = std::min(near_, (side == Side::Left) ? p.x() : w - p.x());
                    break;
            }
        }
        // Only tiles that actually lie along this border can cover it.
        const bool overlapsBorder = (lo <= (side == Side::Top || side == Side::Bottom ? w : h)) && (hi >= 0.0);
        if (!overlapsBorder) continue;
        best = std::min(best, std::max(0.0, near_));
    }
    return best;
}

// Blank strip measured from each border to the outermost watermark pixel.
struct Gaps {
    int top = 0;
    int bottom = 0;
    int left = 0;
    int right = 0;
};

Gaps measureGaps(const QImage& page) {
    Gaps g;
    g.top = page.height();
    g.left = page.width();
    for (int y = 0; y < page.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(page.constScanLine(y));
        for (int x = 0; x < page.width(); ++x) {
            const QRgb p = row[x];
            if (qRed(p) == 255 && qGreen(p) == 255 && qBlue(p) == 255) continue;
            if (x < g.left) g.left = x;
            if (x > page.width() - 1 - g.right) g.right = page.width() - 1 - x;
            if (y < g.top) g.top = y;
            if (y > page.height() - 1 - g.bottom) g.bottom = page.height() - 1 - y;
        }
    }
    return g;
}

// A CJK string of exactly `n` characters. Wide enough that the old
// round()-based column count really did produce an overlapping layout.
QString cjkText(int n) {
    static const char* unit = "机密文件请勿外传";
    QString s = QString::fromUtf8(unit);
    while (s.size() < n) s += QString::fromUtf8(unit);
    return s.left(n);
}

// Exact tile-overlap test. Every tile of one layout shares the same rotation, so
// two tiles overlap iff, in that rotated frame, their centres are closer than one
// tile on BOTH axes. Comparing axis-aligned bounds instead would over-report
// overlaps near the diagonal — the exact kind of error that hides a real one.
bool tilesOverlap(const TileItem& a, const TileItem& b) {
    const double rad = a.rotationDeg * (std::numbers::pi / 180.0);
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    const double dx = b.center.x() - a.center.x();
    const double dy = b.center.y() - a.center.y();
    const double w = a.textBounds.width();
    const double h = a.textBounds.height();

    // Cheap rejection: a rotated box never reaches further than (w + h) / 2,
    // which keeps the O(n^2) sweep below effectively linear.
    if (std::abs(dx) >= w + h || std::abs(dy) >= w + h) return false;

    const double du = dx * c + dy * s;    // along the text
    const double dv = -dx * s + dy * c;   // across the text
    return std::abs(du) < w - 1e-6 && std::abs(dv) < h - 1e-6;
}

} // namespace

void testTileLayout() {
    std::cout << "[RUN] testTileLayout\n";

    WatermarkConfig cfg;
    cfg.text = "机密文件 请勿外传";
    cfg.fontSizePt = 24;
    cfg.dpi = 200;
    cfg.rotationDegrees = -35.0;
    cfg.opacity = 0.12;
    cfg.colorHex = "#BEBEBE";

    // ── basic geometry (A4 @200 DPI = 1654 x 2339 px) ─────────────────────
    const int w = 1654;
    const int h = 2339;

    auto tiles = WatermarkTileLayout::calculateLayout(w, h, cfg, fontFor(cfg));
    assert(!tiles.empty());
    assert(tiles.size() >= 10);            // staggered grid, not a single stamp
    for (const auto& t : tiles) {
        assert(t.rotationDeg == cfg.rotationDegrees);
        assert(t.textBounds.width() > 0.0 && t.textBounds.height() > 0.0);
    }
    std::cout << "  A4 @200 DPI, -35 deg: " << tiles.size() << " tiles\n";

    // ── no blank band along any page border ───────────────────────────────
    struct PageSize { const char* name; int w; int h; int pt; int dpi; };
    const PageSize pages[] = {
        {"A4@200", 1654, 2339, 24, 200},
        {"Letter@300", 2550, 3300, 18, 300},
    };
    // 0 / ±90 / 180 are the rotations that used to leave a band; -35 / 20 / -45
    // keep the ordinary tilted case honest.
    const double rotations[] = {0.0, -90.0, 90.0, 180.0, -35.0, 20.0, -45.0};

    for (const auto& p : pages) {
        for (double rot : rotations) {
            WatermarkConfig c = cfg;
            c.fontSizePt = p.pt;
            c.dpi = p.dpi;
            c.rotationDegrees = rot;

            const QFont font = fontFor(c);
            const auto layout = WatermarkTileLayout::calculateLayout(p.w, p.h, c, font);
            assert(!layout.empty());

            for (Side side : {Side::Top, Side::Bottom, Side::Left, Side::Right}) {
                if (borderCoverageGap(layout, p.w, p.h, side) > 0.0) {
                    std::cerr << "  [" << p.name << " " << rot << " deg] no tile box crosses the "
                              << sideName(side) << " border" << std::endl;
                    assert(false && "page border not covered by any tile box");
                }
            }

            QImage page(p.w, p.h, QImage::Format_RGB32);
            page.fill(Qt::white);
            assert(WatermarkRenderer::applyWatermark(page, c));

            // Stamp + tile diagnostics: a platform-specific renderer/stamp problem
            // (e.g. a blank +90 deg stamp on Windows) is otherwise invisible in CI.
            const WatermarkRenderer::Stamp stamp = WatermarkRenderer::createStamp(c);
            int stampInk = 0;
            for (int y = 0; y < stamp.image.height(); ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(stamp.image.constScanLine(y));
                for (int x = 0; x < stamp.image.width(); ++x) {
                    if (qAlpha(row[x]) != 0) ++stampInk;
                }
            }
            std::cerr << "    stamp " << stamp.image.width() << "x" << stamp.image.height()
                      << " inkPx=" << stampInk << " firstTile=("
                      << static_cast<int>(layout.front().center.x()) << ","
                      << static_cast<int>(layout.front().center.y()) << ") lastTile=("
                      << static_cast<int>(layout.back().center.x()) << ","
                      << static_cast<int>(layout.back().center.y()) << ")" << std::endl;

            const Gaps gaps = measureGaps(page);
            const double textH = QFontMetricsF(font).boundingRect(QString::fromStdString(c.text)).height();
            // A blank strip wider than half a glyph height reads as a margin; the
            // pre-fix bands (63 / 114 / 181 px) were far above that, the remaining
            // 11-13 px are the anti-aliasing tail of the outermost glyph.
            const int limit = static_cast<int>(textH / 2.0) + 2;
            std::cerr << "  " << p.name << " " << rot << " deg: tiles=" << layout.size()
                      << " textH=" << static_cast<int>(textH) << " ink gaps t/b/l/r = "
                      << gaps.top << "/" << gaps.bottom << "/" << gaps.left << "/" << gaps.right
                      << " (limit " << limit << ")" << std::endl;
            assert(gaps.top <= limit);
            assert(gaps.bottom <= limit);
            assert(gaps.left <= limit);
            assert(gaps.right <= limit);
        }
    }

    // ── INVARIANT: no two tiles may ever overlap ──────────────────────────
    // Regression set: 13-char and 19..21-char texts overlapped under the old
    // round()-based column count, and from ~33 chars on (text longer than the
    // page) the two border tiles sat almost on top of each other on every row.
    struct Canvas { const char* name; int w; int h; int dpi; };
    const Canvas canvases[] = {
        {"A4@200", 1654, 2339, 200},
        {"Letter@300", 2550, 3300, 300},
        {"preview@96", 800, 1131, 96},
    };
    const int lengths[] = {1, 3, 6, 8, 10, 13, 19, 20, 21, 24, 32, 33, 40, 60, 120, 300};
    const double allRotations[] = {0.0, 90.0, -90.0, 180.0, -35.0, 20.0, -45.0};
    const int allFontSizes[] = {12, 24, 72, 200};

    int layoutsChecked = 0;
    for (const auto& cv : canvases) {
        for (double rot : allRotations) {
            for (int pt : allFontSizes) {
                for (int n : lengths) {
                    WatermarkConfig c = cfg;
                    c.text = cjkText(n).toStdString();
                    c.fontSizePt = pt;
                    c.dpi = cv.dpi;
                    c.rotationDegrees = rot;

                    const QFont font = fontFor(c);
                    const auto layout = WatermarkTileLayout::calculateLayout(cv.w, cv.h, c, font);
                    assert(!layout.empty());
                    ++layoutsChecked;

                    for (size_t i = 0; i < layout.size(); ++i) {
                        for (size_t k = i + 1; k < layout.size(); ++k) {
                            if (tilesOverlap(layout[i], layout[k])) {
                                std::cerr << "  [" << cv.name << " " << rot << " deg, " << pt
                                          << " pt, " << n << " chars] tiles " << i << " and " << k
                                          << " overlap" << std::endl;
                                assert(false && "watermark tiles must never overlap");
                            }
                        }
                    }
                }
            }
        }
    }
    std::cout << "  no-overlap sweep: " << layoutsChecked << " layouts checked\n";

    // ── long texts must still cover every page border (no blank band) ─────
    // Past the page width the layout switches to a single centred tile (or a
    // touching pair), which must not reopen the blank-band bug those borders
    // were fixed for — and the stamp crop must not shave the border ink away.
    const int longLengths[] = {40, 300};
    const double longRotations[] = {0.0, -35.0, 90.0};
    for (const auto& p : pages) {
        for (double rot : longRotations) {
            for (int n : longLengths) {
                WatermarkConfig c = cfg;
                c.fontSizePt = p.pt;
                c.dpi = p.dpi;
                c.rotationDegrees = rot;
                c.text = cjkText(n).toStdString();

                const QFont font = fontFor(c);
                const auto layout = WatermarkTileLayout::calculateLayout(p.w, p.h, c, font);
                assert(!layout.empty());
                for (Side side : {Side::Top, Side::Bottom, Side::Left, Side::Right}) {
                    if (borderCoverageGap(layout, p.w, p.h, side) > 0.0) {
                        std::cerr << "  [" << p.name << " " << rot << " deg, " << n
                                  << " chars] no tile box crosses the " << sideName(side)
                                  << " border" << std::endl;
                        assert(false && "page border not covered by any tile box");
                    }
                }

                QImage page(p.w, p.h, QImage::Format_RGB32);
                page.fill(Qt::white);
                assert(WatermarkRenderer::applyWatermark(page, c));

                const Gaps gaps = measureGaps(page);
                const double textH =
                    QFontMetricsF(font).boundingRect(QString::fromStdString(c.text)).height();
                const int limit = static_cast<int>(textH / 2.0) + 2;
                std::cerr << "  " << p.name << " " << rot << " deg, " << n << " chars: tiles="
                          << layout.size() << " ink gaps t/b/l/r = " << gaps.top << "/"
                          << gaps.bottom << "/" << gaps.left << "/" << gaps.right << " (limit "
                          << limit << ")" << std::endl;
                assert(gaps.top <= limit);
                assert(gaps.bottom <= limit);
                assert(gaps.left <= limit);
                assert(gaps.right <= limit);
            }
        }
    }

    // ── a very long text must not blow the rotated stamp up ───────────────
    // QImage::transformed() sizes its output to the rotated bounding box, whose
    // area grows with the square of the text length: 200 chars @200 DPI used to
    // produce a ~10800 x 7600 px (~330 MB) stamp that every tile then blitted.
    {
        WatermarkConfig c = cfg;
        c.text = cjkText(200).toStdString();
        c.fontSizePt = 24;
        c.dpi = 200;
        c.rotationDegrees = -35.0;

        const QFont font = fontFor(c);
        const QRectF textRect = QFontMetricsF(font).boundingRect(QString::fromStdString(c.text));
        const double pad = 12.0;
        const double flatW = textRect.width() + 2 * pad;
        const double flatH = textRect.height() + 2 * pad;
        const double rad = c.rotationDegrees * (std::numbers::pi / 180.0);
        const double cosA = std::abs(std::cos(rad));
        const double sinA = std::abs(std::sin(rad));
        // What the un-cropped stamp would be (computed, NOT allocated: it is
        // ~330 MB, and allocating it in CI is exactly what we are preventing).
        const double uncroppedPx = (flatW * cosA + flatH * sinA) * (flatW * sinA + flatH * cosA);

        const double uExtent = WatermarkTileLayout::rotatedExtentU(1654, 2339, c.rotationDegrees);
        const int crop = static_cast<int>(std::ceil(uExtent)) + 2;
        const WatermarkRenderer::Stamp stamp = WatermarkRenderer::createStamp(c, crop);

        const double stampPx = double(stamp.image.width()) * stamp.image.height();
        std::cerr << "  200-char stamp: cropped " << stamp.image.width() << "x"
                  << stamp.image.height() << " (" << qint64(stampPx) / 1000000
                  << " Mpx) vs un-cropped " << qint64(uncroppedPx) / 1000000 << " Mpx"
                  << std::endl;

        assert(!stamp.image.isNull());
        assert(stamp.image.width() <= crop + flatH + 2);
        assert(stamp.image.height() <= crop + flatH + 2);
        assert(stampPx * 4.0 < uncroppedPx);
    }

    // ── cropping must not remove anything visible ────────────────────────
    // The cropped and the full stamp must paint the same page: the crop only
    // removes text that can never reach it. Compared as total ink darkness, which
    // is invariant under the sub-pixel resampling a different output raster size
    // introduces (a hard non-white pixel count flips on antialiased edges).
    {
        WatermarkConfig c = cfg;
        c.text = cjkText(80).toStdString();
        c.dpi = 200;
        c.rotationDegrees = -35.0;

        const int w = 1654;
        const int h = 2339;
        auto darkness = [](const QImage& img) {
            double sum = 0.0;
            for (int y = 0; y < img.height(); ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = 0; x < img.width(); ++x) sum += 255 - qGray(row[x]);
            }
            return sum;
        };

        QImage cropped(w, h, QImage::Format_RGB32);
        cropped.fill(Qt::white);
        assert(WatermarkRenderer::applyWatermark(cropped, c)); // crops internally

        QImage full(w, h, QImage::Format_RGB32);
        full.fill(Qt::white);
        const WatermarkRenderer::Stamp uncroppedStamp = WatermarkRenderer::createStamp(c);
        assert(WatermarkRenderer::applyWatermark(full, c, uncroppedStamp));

        const double dCropped = darkness(cropped);
        const double dFull = darkness(full);
        const double rel = std::abs(dCropped - dFull) / dFull;
        std::cerr << "  80-char crop equivalence: ink " << qint64(dCropped) << " vs "
                  << qint64(dFull) << " (" << rel * 100.0 << "% difference)" << std::endl;
        assert(dFull > 0.0);
        assert(rel < 0.01);
    }

    std::cout << "[PASS] testTileLayout\n";
}

} // namespace pdfmark
