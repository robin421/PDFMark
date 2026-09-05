// PDFMark - Watermark preview widget displaying live tiled watermark on A4.
#pragma once

#include "watermark/WatermarkConfig.h"
#include <QWidget>
#include <QImage>

namespace pdfmark {

class WatermarkPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit WatermarkPreviewWidget(QWidget* parent = nullptr);

    void setConfig(const WatermarkConfig& config);
    const WatermarkConfig& config() const { return config_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updatePreview();

    WatermarkConfig config_;
    QImage cachedPreview_;
};

} // namespace pdfmark