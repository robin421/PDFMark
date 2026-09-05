// PDFMark - WatermarkPreviewWidget implementation.
#include "ui/WatermarkPreviewWidget.h"
#include "watermark/WatermarkRenderer.h"
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>

namespace pdfmark {

WatermarkPreviewWidget::WatermarkPreviewWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(240, 340);
    setStyleSheet(
        "background: white; "
        "border: 1px solid #cccccc; "
        "border-radius: 4px;"
    );
}

void WatermarkPreviewWidget::setConfig(const WatermarkConfig& config) {
    config_ = config;
    updatePreview();
    update();
}

void WatermarkPreviewWidget::updatePreview() {
    // Use A4 portrait aspect ratio (~1.414) for preview
    int w = std::max(100, width() - 20);
    int h = static_cast<int>(w * 1.414);
    h = std::min(h, height() - 20);
    cachedPreview_ = WatermarkRenderer::renderPreview(w, h, config_);
}

void WatermarkPreviewWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Fill background with light gray margin
    painter.fillRect(rect(), QColor(0xF0, 0xF0, 0xF0));

    // Draw A4 sheet
    int margin = 10;
    int sheetW = std::max(40, width() - 2 * margin);
    int sheetH = std::max(40, static_cast<int>(sheetW * 1.414));
    if (sheetH > height() - 2 * margin) {
        sheetH = height() - 2 * margin;
        sheetW = static_cast<int>(sheetH / 1.414);
    }
    int x = (width() - sheetW) / 2;
    int y = (height() - sheetH) / 2;

    painter.fillRect(x, y, sheetW, sheetH, Qt::white);

    if (!cachedPreview_.isNull()) {
        // Scale cached preview to fit inside the A4 sheet
        painter.drawImage(QRect(x, y, sheetW, sheetH), cachedPreview_);
    }

    painter.setPen(QColor(0x99, 0x99, 0x99));
    painter.drawText(rect(), Qt::AlignBottom | Qt::AlignHCenter, "预览：水印平铺效果");
}

void WatermarkPreviewWidget::resizeEvent(QResizeEvent* /*event*/) {
    updatePreview();
}

} // namespace pdfmark