#include "common/Common.h"
#include "diagnostics/Diagnostics.h"
#include <cassert>
#include <string>

namespace {
// True when `s` is well-formed UTF-8 (no truncated or stray sequences).
bool isValidUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if (c < 0x80) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else return false;
        if (i + len > s.size()) return false;
        for (size_t k = 1; k < len; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += len;
    }
    return true;
}
} // namespace
#include <iostream>
#include <thread>
#include <chrono>

namespace pdfmark {

void testCommonUtilities() {
    std::cout << "[RUN] testCommonUtilities\n";

    // 1. DPI points to pixels
    // 72 points at 72 DPI == 72 pixels
    assert(pointsToPixels(72.0, 72) == 72);
    // 72 points at 200 DPI == 200 pixels
    assert(pointsToPixels(72.0, 200) == 200);
    // Standard A4 width: 595.276 points -> ~1654 px at 200 DPI
    int a4w = pointsToPixels(595.276, 200);
    assert(a4w >= 1650 && a4w <= 1655);

    // 2. Enum resolution
    assert(dpiFromResolution(RasterResolution::R150) == 150);
    assert(dpiFromResolution(RasterResolution::R200) == 200);
    assert(dpiFromResolution(RasterResolution::R300) == 300);

    // 3. ScopedTimer
    double elapsedMs = 0.0;
    {
        ScopedTimer timer(elapsedMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(elapsedMs >= 15.0);

    // 4. Memory probe
    long long peakBytes = MemoryProbe::peakPhysicalBytes();
    assert(peakBytes >= 0);
    // 5. sanitizeFilename
    assert(sanitizeFilename("Confidential") == "Confidential");
    assert(sanitizeFilename("张三/人事*2026:机密") == "张三_人事_2026_机密");
    assert(sanitizeFilename("  test<file>?\"|  ") == "test_file");
    assert(sanitizeFilename("   ///:::***???   ") == "watermark");
    assert(sanitizeFilename("") == "watermark");

    // 6. sanitizeFilenameOrEmpty: same cleaning, but "nothing left" stays empty
    //    so optional path parts (file-name suffixes) can mean "not set" instead
    //    of silently turning into a folder/file called "watermark".
    assert(sanitizeFilenameOrEmpty("Confidential") == "Confidential");
    assert(sanitizeFilenameOrEmpty("红色/2026") == "红色_2026");
    assert(sanitizeFilenameOrEmpty("  ") == "");
    assert(sanitizeFilenameOrEmpty("") == "");
    assert(sanitizeFilenameOrEmpty("___") == "");
    assert(sanitizeFilenameOrEmpty("///") == "");

    // 7. Trailing dots/spaces are trimmed: Windows drops them when CREATING a
    //    name but not when the same name is reused as a path prefix, so a
    //    derived template name ending in "..." (any watermark text > 6 chars)
    //    used to make every output fail on Windows.
    assert(sanitizeFilename("内部文件-李...") == "内部文件-李");
    assert(sanitizeFilenameOrEmpty("内部文件-李...") == "内部文件-李");
    assert(sanitizeFilename("报告...  ") == "报告");
    assert(sanitizeFilenameOrEmpty("报告..") == "报告");
    assert(sanitizeFilenameOrEmpty("...") == "");
    assert(sanitizeFilenameOrEmpty(" . ") == "");
    assert(sanitizeFilename("...") == "watermark");    // unusable -> fallback
    assert(sanitizeFilename(".hidden") == ".hidden");  // leading dot is kept

    // 8. boundPathComponent: short values pass through untouched, long ones are
    //    cut on a UTF-8 boundary and tagged with a hash of the FULL value so two
    //    different long names never collapse onto one folder/file name.
    assert(boundPathComponent("机密") == "机密");
    assert(boundPathComponent("") == "");
    assert(boundPathComponent(std::string(200, 'a')) == std::string(200, 'a'));
    assert(boundPathComponent(std::string(201, 'a')).size() <= kMaxPathComponentBytes);

    const std::string longA = std::string("前缀") + std::string(200, 'a');
    const std::string longB = std::string("前缀") + std::string(199, 'a') + "b";
    const std::string boundedA = boundPathComponent(longA);
    const std::string boundedB = boundPathComponent(longB);
    assert(boundedA.size() <= kMaxPathComponentBytes);
    assert(boundedB.size() <= kMaxPathComponentBytes);
    assert(boundedA != boundedB);                       // hash distinguishes them
    assert(boundedA.compare(0, 6, "前缀") == 0);        // prefix stays readable
    assert(boundedA == boundPathComponent(longA));      // deterministic
    std::string manyCjk;
    for (int i = 0; i < 300; ++i) manyCjk += "中";
    assert(boundPathComponent(manyCjk).size() <= kMaxPathComponentBytes);
    // Never split a multi-byte character: the bounded value stays valid UTF-8.
    const std::string chewed = boundPathComponent(manyCjk);
    assert(chewed.size() >= 3);
    assert(isValidUtf8(chewed));

    std::cout << "[PASS] testCommonUtilities\n";
}

} // namespace pdfmark