#include "watermark/WatermarkConfig.h"
#include <cassert>
#include <iostream>

namespace pdfmark {

void testWatermarkConfig() {
    std::cout << "[RUN] testWatermarkConfig\n";

    // Default config should be valid
    WatermarkConfig cfg;
    assert(cfg.isValid());

    // Empty text invalid
    cfg.text = "";
    assert(!cfg.isValid());
    cfg.text = "Valid Text";
    assert(cfg.isValid());

    // Font size boundary
    cfg.fontSizePt = 5;
    assert(!cfg.isValid());
    cfg.fontSizePt = 201;
    assert(!cfg.isValid());
    cfg.fontSizePt = 24;
    assert(cfg.isValid());

    // Opacity boundary
    cfg.opacity = 0.0;
    assert(!cfg.isValid());
    cfg.opacity = -0.1;
    assert(!cfg.isValid());
    cfg.opacity = 1.01;
    assert(!cfg.isValid());
    cfg.opacity = 0.12;
    assert(cfg.isValid());

    // DPI boundary
    cfg.dpi = 50;
    assert(!cfg.isValid());
    cfg.dpi = 700;
    assert(!cfg.isValid());
    cfg.dpi = 200;
    assert(cfg.isValid());

    // JPEG quality boundary
    cfg.jpegQuality = 9;
    assert(!cfg.isValid());
    cfg.jpegQuality = 101;
    assert(!cfg.isValid());
    cfg.jpegQuality = 85;
    assert(cfg.isValid());

    std::cout << "[PASS] testWatermarkConfig\n";
}

} // namespace pdfmark