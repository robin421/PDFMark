// PDFMark - WatermarkRenderer implementation.
#include "watermark/WatermarkRenderer.h"
#include "watermark/WatermarkTileLayout.h"
#include <QPainter>
#include <QTransform>
#include <QFontMetricsF>
#include <QColor>
#include <QString>
#include <cmath>
#include <numbers>
#include <algorithm>

namespace pdfmark {

WatermarkRenderer::Stamp WatermarkRenderer::createStamp(const WatermarkConfig& config, int maxTextWidthPx) {
    Stamp stamp;
    if (!config.isValid()) {
        return stamp;
    }

    int pixelSize = WatermarkTileLayout::calculatePixelFontSize(config.fontSizePt, config.dpi);
    QString family = config.fontFamily.empty() ? QString("Arial") : QString::fromStdString(config.fontFamily);
    QFont font(family);
    font.setPixelSize(pixelSize);
    font.setStyleHint(QFont::SansSerif);
    font.setBold(config.fontBold);
    font.setItalic(config.fontItalic);

    stamp.font = font;
    stamp.rotationDeg = config.rotationDegrees;

    QFontMetricsF fm(font);
    QString qtext = QString::fromStdString(config.text);
    QRectF rawRect = fm.boundingRect(qtext);

    double textW = std::max(10.0, rawRect.width());
    double textH = std::max(8.0, rawRect.height());

    // Prepare color with opacity
    QColor color(QString::fromStdString(config.colorHex));
    if (!color.isValid()) {
        color = QColor(0xBE, 0xBE, 0xBE);
    }
    double opacity = std::clamp(config.opacity, 0.01, 1.0);
    color.setAlphaF(opacity);

    // Add padding to ensure anti-aliased font edges are never clipped
    const double pad = 12.0;
    const double paddedW = textW + pad * 2.0;
    const double paddedH = textH + pad * 2.0;

    // Draw the text UPRIGHT and rotate the PIXELS afterwards instead of drawing
    // through a rotated QPainter.
    //
    // Why: rotated text rendering is not reliable on every platform. On Windows
    // the +90 degree stamp came out COMPLETELY blank (measured: 0 non-transparent
    // pixels, while -90 degree was fine), so a +90 degree watermark painted nothing
    // at all. Rotating the raster gives the same result everywhere ("measure the
    // stamp, not the painter") and still keeps the per-tile work down to a single
    // pre-rotated blit.
    const int flatW = static_cast<int>(std::ceil(paddedW));
    const int flatH = static_cast<int>(std::ceil(paddedH));
    QImage flat(flatW, flatH, QImage::Format_ARGB32_Premultiplied);
    flat.fill(Qt::transparent);
    {
        QPainter fp(&flat);
        fp.setRenderHint(QPainter::Antialiasing, true);
        fp.setRenderHint(QPainter::TextAntialiasing, true);
        fp.setFont(font);
        fp.setPen(color);
        fp.drawText(QRectF(pad, pad, textW, textH), Qt::AlignCenter, qtext);
        fp.end();
    }

    // ── Bound the raster before rotating it ────────────────────────────────
    // QImage::transformed() allocates the ROTATED BOUNDING BOX, whose area grows
    // with the square of the text length, and every tile of every page then
    // blits that image. A tile can never display more text than the page's
    // rotated width — only the text inside that window can reach the page (see
    // WatermarkTileLayout::rotatedExtentU) — so the flat text is cropped to that
    // width first.
    //
    // The crop is centred on the text centre, and its width is kept on the same
    // pixel parity as the source, so the removed margin is EXACTLY symmetric
    // (an odd difference would truncate the left inset by half a pixel and shift
    // the rotated stamp by that much). The rotated image therefore stays centred
    // on the text centre and the per-tile blit alignment is untouched.
    //
    // Only the two layouts that cannot fit the text in one page-wide row (single
    // centred tile, or a touching pair) are ever cropped, and both keep the whole
    // window that is visible on the page, so ordinary watermarks render exactly
    // as before.
    if (maxTextWidthPx > 0 && flat.width() > maxTextWidthPx) {
        int cropW = maxTextWidthPx;
        if (((flat.width() - cropW) & 1) != 0) --cropW;
        const int x = (flat.width() - cropW) / 2;
        flat = flat.copy(x, 0, cropW, flat.height());
    }

    if (std::abs(config.rotationDegrees) < 1e-9) {
        stamp.image = std::move(flat);
    } else {
        stamp.image = flat.transformed(QTransform().rotate(config.rotationDegrees),
                                       Qt::SmoothTransformation);
    }
    if (stamp.image.format() != QImage::Format_ARGB32_Premultiplied) {
        stamp.image.convertTo(QImage::Format_ARGB32_Premultiplied);
    }

    return stamp;
}

bool WatermarkRenderer::applyWatermark(QImage& image, const WatermarkConfig& config) {
    if (image.isNull() || !config.isValid()) {
        return false;
    }
    // Bound the rotated stamp to what this page can actually show (see
    // createStamp): any longer text never becomes visible on this page, it only
    // inflates the rotated raster.
    const int maxTextWidth = static_cast<int>(std::ceil(
        WatermarkTileLayout::rotatedExtentU(image.width(), image.height(),
                                            config.rotationDegrees))) + 2;
    Stamp stamp = createStamp(config, maxTextWidth);
    return applyWatermark(image, config, stamp);
}

bool WatermarkRenderer::applyWatermark(QImage& image, const WatermarkConfig& config, const Stamp& stamp) {
    if (image.isNull() || !stamp.isValid()) {
        return false;
    }

    // Calculate tile centers using the font cached in the stamp
    auto tiles = WatermarkTileLayout::calculateLayout(image.width(), image.height(), config, stamp.font);
    if (tiles.empty()) {
        return false;
    }

    const double halfW = stamp.image.width() / 2.0;
    const double halfH = stamp.image.height() / 2.0;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Directly blit pre-rendered and pre-rotated stamp.
    // Avoids repeated font rasterization and costly painter save/restore state stacks.
    for (const auto& tile : tiles) {
        painter.drawImage(QPointF(tile.center.x() - halfW, tile.center.y() - halfH), stamp.image);
    }

    painter.end();
    return true;
}

bool WatermarkRenderer::fillBuffer(QImage& scratch, const QImage& source) {
    if (source.isNull()) return false;
    if (scratch.size() != source.size() || scratch.format() != QImage::Format_RGB32) {
        scratch = QImage(source.size(), QImage::Format_RGB32);
        if (scratch.isNull()) return false;
    }
    QPainter blit(&scratch);
    blit.drawImage(0, 0, source);
    blit.end();
    return true;
}

void WatermarkRenderer::blitTiles(QImage& target, const Stamp& stamp,
                                  const std::vector<TileItem>& tiles) {
    if (target.isNull() || !stamp.isValid() || tiles.empty()) return;
    const double halfW = stamp.image.width() / 2.0;
    const double halfH = stamp.image.height() / 2.0;

    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const auto& tile : tiles) {
        painter.drawImage(QPointF(tile.center.x() - halfW, tile.center.y() - halfH), stamp.image);
    }
    painter.end();
}

QImage WatermarkRenderer::renderPreview(int widthPx, int heightPx,
                                        const std::vector<WatermarkConfig>& configs) {
    const int canvasW = 800;
    const int canvasH = 1131;

    QImage preview(canvasW, canvasH, QImage::Format_RGB32);
    preview.fill(Qt::white);
    {
        QPainter p(&preview);
        p.setPen(QColor(0xDF, 0xDF, 0xDF));
        p.setBrush(QColor(0xF5, 0xF5, 0xF5));
        p.drawRect(40, 40, canvasW - 80, 40);
        int lineY = 120;
        while (lineY < canvasH - 50) {
            p.drawLine(50, lineY, canvasW - 50, lineY);
            lineY += 24;
        }
        p.end();
    }

    for (const auto& cfg : configs) {
        if (cfg.text.empty()) continue;
        WatermarkConfig at96 = cfg;
        at96.dpi = 96;
        applyWatermark(preview, at96);
    }

    if (widthPx > 0 && heightPx > 0 && (widthPx != canvasW || heightPx != canvasH)) {
        return preview.scaled(widthPx, heightPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return preview;
}

QImage WatermarkRenderer::renderPreview(int widthPx, int heightPx, const WatermarkConfig& config) {
    // Fixed high-fidelity reference canvas matching standard A4 aspect ratio (800 x 1131 px)
    // This guarantees that preview layout, spacing and font scaling are 100% identical to actual PDF output.
    const int canvasW = 800;
    const int canvasH = 1131; // 800 * 1.414

    QImage preview(canvasW, canvasH, QImage::Format_RGB32);
    preview.fill(Qt::white);

    // Draw simulated subtle document text lines to demonstrate contrast
    {
        QPainter p(&preview);
        p.setPen(QColor(0xDF, 0xDF, 0xDF));
        p.setBrush(QColor(0xF5, 0xF5, 0xF5));
        
        // Header block
        p.drawRect(40, 40, canvasW - 80, 40);
        
        // Sample dummy text paragraphs
        int lineY = 120;
        while (lineY < canvasH - 50) {
            p.drawLine(50, lineY, canvasW - 50, lineY);
            lineY += 24;
        }
        p.end();
    }

    // Use standard 96 DPI proportional to 800px A4 canvas
    WatermarkConfig previewConfig = config;
    previewConfig.dpi = 96;

    applyWatermark(preview, previewConfig);

    // Return smooth scaled image if specific preview dimensions are requested
    if (widthPx > 0 && heightPx > 0 && (widthPx != canvasW || heightPx != canvasH)) {
        return preview.scaled(widthPx, heightPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return preview;
}

} // namespace pdfmark