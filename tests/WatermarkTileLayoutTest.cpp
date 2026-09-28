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

    std::cout << "[PASS] testTileLayout\n";
}

} // namespace pdfmark
