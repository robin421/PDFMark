// PDFMark - Common type definitions and lightweight utilities.
//
// Common.h - Shared by all modules to avoid circular dependencies.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <QString>
#include <QMetaType>
#define PDFMARK_VERSION "1.7.1"
#define PDFMARK_VERSION_MAJOR 1
#define PDFMARK_VERSION_MINOR 7
#define PDFMARK_VERSION_PATCH 1

namespace pdfmark {

namespace fs = std::filesystem;

inline std::string pathToString(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}
inline fs::path qstringToPath(const QString& str) {
#ifdef _WIN32
    return fs::path(str.toStdWString());
#else
    return fs::path(str.toUtf8().toStdString());
#endif
}
inline fs::path stringToPath(const std::string& u8str) {
#ifdef _WIN32
    return fs::u8path(u8str);
#else
    return fs::path(u8str);
#endif
}

#if defined(_WIN32) && defined(_MSC_VER)
    // SEH (Structured Exception Handling) guard for Windows.
    // Converts access violations / SEH exceptions into C++ exceptions
    // so they can be caught by try/catch(...).
    #define PDFMARK_SEH_GUARD_BEGIN \
        __try {
    #define PDFMARK_SEH_GUARD_END(expr) \
        } __except(EXCEPTION_EXECUTE_HANDLER) { \
            throw std::runtime_error("SEH exception (0x" + \
                std::to_string(GetExceptionCode()) + ") in " + (expr)); \
        }
#else
    #define PDFMARK_SEH_GUARD_BEGIN
    #define PDFMARK_SEH_GUARD_END(expr)
#endif

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
// Replaces characters that are illegal in a path component (or that would be
// interpreted as a separator) with '_', then trims surrounding spaces,
// underscores and trailing dots. Returns an EMPTY string when nothing usable is
// left, which lets callers distinguish "not set" from the "watermark" fallback
// below.
//
// Trailing dots are trimmed because Windows silently drops them when a file or
// directory is CREATED ("CONCUR..." -> "CONCUR") but does NOT drop them when
// the same name is later used as a path prefix: opening
// "CONCUR.../out.tmp" fails with ENOENT. Derived template names of long
// watermark texts always end with "...", so a trailing dot in this sanitizer
// made every such output fail on Windows.
inline std::string sanitizeFilenameOrEmpty(const std::string& raw) {
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
        return std::string();
    }
    size_t end = clean.find_last_not_of(" ._");
    if (end == std::string::npos || end < start) {
        return std::string();
    }
    return clean.substr(start, end - start + 1);
}

// Path component with a guaranteed non-empty result: blank or fully-sanitized
// input degrades to "watermark", so a folder/file name never becomes empty.
inline std::string sanitizeFilename(const std::string& raw) {
    std::string clean = sanitizeFilenameOrEmpty(raw);
    return clean.empty() ? std::string("watermark") : clean;
}

// Longest path component we hand to the filesystem, in UTF-8 bytes. APFS/ext4
// refuse a component beyond 255 bytes, and Windows caps the whole path at 260
// characters unless long paths are enabled. 200 leaves room for the
// "~<name>.<id>.tmp" staging file the writer creates next to the output.
inline constexpr size_t kMaxPathComponentBytes = 200;

// Bounds a path component WITHOUT losing the ability to tell inputs apart: the
// tail is replaced by "_<hash>", where the hash covers the whole original value,
// so two long names that differ only at the end still map to two different
// folders/files. Inputs that already fit are returned unchanged, and the cut is
// made on a UTF-8 boundary so the result never holds half a character.
inline std::string boundPathComponent(const std::string& part,
                                      size_t maxBytes = kMaxPathComponentBytes) {
    if (maxBytes == 0 || part.size() <= maxBytes) return part;

    uint32_t hash = 2166136261u;   // FNV-1a over the FULL value
    for (const unsigned char c : part) {
        hash ^= c;
        hash *= 16777619u;
    }
    char hex[9];
    std::snprintf(hex, sizeof(hex), "%08x", hash);
    const std::string suffix = std::string("_") + hex;

    const size_t budget = maxBytes > suffix.size() ? maxBytes - suffix.size() : 0;
    size_t cut = std::min(budget, part.size());
    while (cut > 0 && (static_cast<unsigned char>(part[cut]) & 0xC0) == 0x80) --cut;
    return part.substr(0, cut) + suffix;
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

Q_DECLARE_METATYPE(pdfmark::FileResult)
Q_DECLARE_METATYPE(std::vector<pdfmark::FileResult>)