// PDFMark - Common type definitions and lightweight utilities.
//
// Common.h - Shared by all modules to avoid circular dependencies.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace pdfmark {

namespace fs = std::filesystem;
inline std::string pathToString(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}


// Performance modes available in the UI.
enum class PerformanceMode {
    Low,    // 1 worker
    Normal, // 2-4 workers (auto)
    High    // max workers capped by memory budget
};

// Resolution options for rasterization.
enum class RasterResolution {
    R150 = 150,
    R200 = 200,
    R300 = 300,
};

inline int dpiFromResolution(RasterResolution r) {
    return static_cast<int>(r);
}

// Convert PDF points (1pt = 1/72 inch) to pixels at given DPI.
inline int pointsToPixels(double points, int dpi) {
    return static_cast<int>((points * dpi) / 72.0);
}
inline std::string sanitizeFilename(const std::string& raw) {
    std::string clean;
    clean.reserve(raw.size());
    for (char ch : raw) {
        if (ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' ||
            ch == '"' || ch == '<' || ch == '>' || ch == '|' ||
            ch == '\n' || ch == '\r' || ch == '\t' || static_cast<unsigned char>(ch) < 32) {
            clean.push_back('_');
        } else {
            clean.push_back(ch);
        }
    }
    size_t start = clean.find_first_not_of(" _");
    if (start == std::string::npos) {
        return "watermark";
    }
    size_t end = clean.find_last_not_of(" _");
    std::string trimmed = clean.substr(start, end - start + 1);
    return trimmed.empty() ? "watermark" : trimmed;
}

// Generic PDF error with human-readable detail.
class PdfError : public std::runtime_error {
public:
    explicit PdfError(const std::string& msg) : std::runtime_error(msg) {}
    explicit PdfError(const char* msg) : std::runtime_error(msg) {}
};

// Result of a single file processing job.
struct FileResult {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    std::string watermarkText;
    bool success = false;
    int totalPages = 0;
    double elapsedMs = 0.0;
    std::string errorMessage;
};
} // namespace pdfmark