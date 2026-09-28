#include "common/Common.h"
#include "watermark/WatermarkRenderer.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <QImage>
#include <QPainter>

namespace fs = std::filesystem;

namespace pdfmark {

void testWatermarkVisual(const fs::path& outputDir) {
    std::cout << "[RUN] testWatermarkVisual\n";

    if (!fs::exists(outputDir)) {
        fs::create_directories(outputDir);
    }

    struct Sample {
        const char* name;
        int widthPx;
        int heightPx;
        int fontSizePt;
        int dpi;
    };

    const Sample samples[] = {
        {"A4_portrait_200dpi.png",  1654, 2339, 24, 200},
        {"A4_landscape_200dpi.png", 2339, 1654, 24, 200},
        {"A3_portrait_150dpi.png",  1240, 1754, 36, 150},
        {"Letter_300dpi.png",       2550, 3300, 18, 300},
    };

    for (const auto& s : samples) {
        QImage page(s.widthPx, s.heightPx, QImage::Format_RGB32);
        page.fill(Qt::white);

        // Render simulated text lines for visual context
        QPainter bgPainter(&page);
        bgPainter.setPen(QColor(0xE0, 0xE0, 0xE0));
        int yPos = 60;
        while (yPos < s.heightPx - 50) {
            bgPainter.drawLine(50, yPos, s.widthPx - 50, yPos);
            yPos += 30;
        }
        bgPainter.end();

        WatermarkConfig cfg;
        cfg.text = "CONFIDENTIAL";
        cfg.fontSizePt = s.fontSizePt;
        cfg.opacity = 0.12;
        cfg.colorHex = "#BEBEBE";
        cfg.rotationDegrees = -35.0;
        cfg.dpi = s.dpi;
        cfg.jpegQuality = 85;

        bool ok = WatermarkRenderer::applyWatermark(page, cfg);
        assert(ok);
        assert(!page.isNull());

        fs::path outFile = outputDir / s.name;
        bool saved = page.save(QString::fromStdString(pathToString(outFile)));
        assert(saved);
        std::cout << "  Generated: " << outFile << "\n";

        // Verify pixel-level watermark presence: the watermark is drawn at 12%
        // opacity, so it BLENDS with the page instead of painting the raw colour
        // — an exact #BEBEBE match (what this test used to require) can never
        // happen. Count faint-gray pixels instead: lighter than the test's
        // #E0E0E0 guide lines, but not pure white.
        int watermarkPixels = 0;
        const int w = page.width();
        const int h = page.height();
        for (int y = h / 4; y < 3 * h / 4; y += 3) {
            for (int x = w / 4; x < 3 * w / 4; x += 3) {
                const QRgb px = page.pixel(x, y);
                const int r = qRed(px), g = qGreen(px), b = qBlue(px);
                const bool gray = (std::abs(r - g) <= 2) && (std::abs(g - b) <= 2);
                if (gray && r >= 240 && r < 255) ++watermarkPixels;
            }
        }
        std::cout << "  Sample '" << s.name << "' watermark pixels found in center: "
                  << watermarkPixels << "\n";
        assert(watermarkPixels > 50);
    }

    std::cout << "[PASS] testWatermarkVisual\n";
}

} // namespace pdfmark