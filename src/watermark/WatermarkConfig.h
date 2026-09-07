// PDFMark - Watermark configuration structures and validation.
#pragma once

#include "common/Common.h"
#include <string>

namespace pdfmark {

struct WatermarkConfig {
    std::string text = "Confidential";
    int fontSizePt = 24;              // Watermark font size in points
    std::string fontFamily = "Arial"; // Font family (Arial, Times New Roman, etc.)
    bool fontBold = true;             // Bold flag
    bool fontItalic = false;          // Italic flag
    double opacity = 0.12;             // 0.0 to 1.0 (default 12%)
    std::string colorHex = "#BEBEBE"; // Default light gray
    double rotationDegrees = -35.0;    // -35 deg tilts from bottom-left to top-right
    int dpi = 200;                    // Rasterization DPI (150, 200, 300)
    int jpegQuality = 85;              // Output JPEG quality (1-100)

    // Validate parameters to prevent zero division or rendering bugs
    bool isValid() const {
        if (text.empty()) return false;
        if (fontSizePt < 6 || fontSizePt > 200) return false;
        if (opacity <= 0.0 || opacity > 1.0) return false;
        if (dpi < 72 || dpi > 600) return false;
        if (jpegQuality < 10 || jpegQuality > 100) return false;
        return true;
    }
};

} // namespace pdfmark