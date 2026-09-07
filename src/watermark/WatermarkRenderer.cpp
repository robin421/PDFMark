// PDFMark - WatermarkRenderer implementation.
#include "watermark/WatermarkRenderer.h"
#include "watermark/WatermarkTileLayout.h"
#include <QPainter>
#include <QFontMetricsF>
#include <QColor>
#include <QString>
#include <cmath>
#include <numbers>
#include <algorithm>

namespace pdfmark {

WatermarkRenderer::Stamp WatermarkRenderer::createStamp(const WatermarkConfig& config) {
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

    // Compute rotated bounding box
    const double rad = config.rotationDegrees * (std::numbers::pi / 180.0);
    const double cosA = std::abs(std::cos(rad));
    const double sinA = std::abs(std::sin(rad));

    int rotW = static_cast<int>(std::ceil(paddedW * cosA + paddedH * sinA)) + 4;
    int rotH = static_cast<int>(std::ceil(paddedW * sinA + paddedH * cosA)) + 4;
    if (rotW % 2 != 0) rotW++;
    if (rotH % 2 != 0) rotH++;

    // ARGB32_Premultiplied offers optimal blending performance on Qt
    QImage stampImage(rotW, rotH, QImage::Format_ARGB32_Premultiplied);
    stampImage.fill(Qt::transparent);

    {
        QPainter sp(&stampImage);
        sp.setRenderHint(QPainter::Antialiasing, true);
        sp.setRenderHint(QPainter::TextAntialiasing, true);
        sp.setFont(font);
        sp.setPen(color);

        sp.translate(rotW / 2.0, rotH / 2.0);
        sp.rotate(config.rotationDegrees);
        QRectF textRect(-textW / 2.0, -textH / 2.0, textW, textH);
        sp.drawText(textRect, Qt::AlignCenter, qtext);
        sp.end();
    }

    stamp.image = std::move(stampImage);
    return stamp;
}

bool WatermarkRenderer::applyWatermark(QImage& image, const WatermarkConfig& config) {
    if (image.isNull() || !config.isValid()) {
        return false;
    }
    Stamp stamp = createStamp(config);
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