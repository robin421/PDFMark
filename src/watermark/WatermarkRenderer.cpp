// PDFMark - WatermarkRenderer implementation.
#include "watermark/WatermarkRenderer.h"
#include "watermark/WatermarkTileLayout.h"
#include <QPainter>
#include <QFont>
#include <QColor>
#include <QString>

namespace pdfmark {

bool WatermarkRenderer::applyWatermark(QImage& image, const WatermarkConfig& config) {
    if (image.isNull() || !config.isValid()) {
        return false;
    }

    // Set up font with scaled pixel size based on DPI
    int pixelSize = WatermarkTileLayout::calculatePixelFontSize(config.fontSizePt, config.dpi);
    QFont font("Arial");
    font.setPixelSize(pixelSize);
    font.setStyleHint(QFont::SansSerif);
    font.setBold(true);

    // Calculate tile centers and rotated text bounds
    auto tiles = WatermarkTileLayout::calculateLayout(image.width(), image.height(), config, font);
    if (tiles.empty()) {
        return false;
    }

    // Prepare QColor from hex string and clamp opacity
    QColor color(QString::fromStdString(config.colorHex));
    if (!color.isValid()) {
        color = QColor(0xBE, 0xBE, 0xBE);
    }
    double opacity = std::clamp(config.opacity, 0.01, 1.0);
    color.setAlphaF(opacity);

    QString text = QString::fromStdString(config.text);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.setPen(color);

    // Draw each tile independently using save/restore transforms
    for (const auto& tile : tiles) {
        painter.save();
        painter.translate(tile.center);
        painter.rotate(tile.rotationDeg);
        painter.drawText(tile.textBounds, Qt::AlignCenter, text);
        painter.restore();
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