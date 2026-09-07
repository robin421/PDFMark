// End-to-end test for multi-watermark batch generation.
//
// Verifies that with one input PDF and N watermark configurations the
// TaskManager produces N independent output PDFs named with the
// sanitized watermark text appended to the source stem.

#include "common/Common.h"
#include "pdf/PdfDocument.h"
#include "pdf/PdfWriter.h"
#include "task/TaskManager.h"
#include "watermark/WatermarkConfig.h"

#include <QImage>
#include <QPainter>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <thread>
#include <unordered_set>

namespace fs = std::filesystem;

namespace pdfmark {

namespace {

// Generate a tiny single-page A4 PDF for testing.
fs::path createTestPdf(const fs::path& outPath) {
    constexpr int kWidthPt = 595;   // A4 width in points
    constexpr int kHeightPt = 842;  // A4 height in points
    constexpr int kDpi = 150;
    constexpr int kWidthPx = (kWidthPt * kDpi) / 72;
    constexpr int kHeightPx = (kHeightPt * kDpi) / 72;

    QImage image(kWidthPx, kHeightPx, QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter p(&image);
    p.setPen(QColor(0xAA, 0xAA, 0xAA));
    for (int y = 50; y < kHeightPx; y += 40) {
        p.drawLine(40, y, kWidthPx - 40, y);
    }
    p.end();

    auto doc = PdfDocument::create();
    PdfWriter::appendRasterPage(doc.get(), image, kWidthPt, kHeightPt, 80);
    PdfDocument::save(doc.get(), outPath);
    return outPath;
}

} // namespace

void testMultiWatermarkBatch() {
    std::cout << "[RUN] testMultiWatermarkBatch" << std::endl;

    fs::path workDir = "multi_wm_e2e";
    std::error_code ec;
    fs::remove_all(workDir, ec);
    fs::create_directories(workDir);
    std::cout << "[STEP 1] Directory created" << std::endl;

    fs::path inputPdf = workDir / "sample_input.pdf";
    createTestPdf(inputPdf);
    std::cout << "[STEP 2] Test PDF created: " << pathToString(inputPdf) << std::endl;
    assert(fs::exists(inputPdf));
    assert(fs::file_size(inputPdf) > 0);
    // illegal filename characters to exercise sanitization.
    std::vector<std::string> rawWatermarks = {
        "机密-张三",
        "内部文件-李四",
        "部门/审核*王五?",
    };

    std::vector<WatermarkConfig> configs;
    for (const auto& wm : rawWatermarks) {
        WatermarkConfig cfg;
        cfg.text = wm;
        cfg.fontSizePt = 36;
        cfg.opacity = 0.15;
        cfg.colorHex = "#B0B0B0";
        cfg.rotationDegrees = -35.0;
        cfg.dpi = 150;
        cfg.jpegQuality = 80;
        assert(cfg.isValid());
        configs.push_back(cfg);
    }

    // Sanitized names organized in per-watermark subdirectories, files keep original PDF name
    std::vector<fs::path> expectedOutputs = {
        workDir / "机密-张三" / "sample_input.pdf",
        workDir / "内部文件-李四" / "sample_input.pdf",
        workDir / "部门_审核_王五" / "sample_input.pdf",
    };

    // Drive the TaskManager synchronously.
    TaskManager mgr;
    mgr.setWatermarkConfigs(configs);
    mgr.setOutputDirectory(workDir);
    mgr.addFile(inputPdf);

    int allFinishedCount = 0;
    int fileStartedCount = 0;
    QObject::connect(&mgr, &TaskManager::allFinished,
                     [&allFinishedCount](const std::vector<FileResult>&) {
                         ++allFinishedCount;
                     });
    QObject::connect(&mgr, &TaskManager::fileStarted,
                     [&fileStartedCount](const QString&, int, int) {
                         ++fileStartedCount;
                     });
    std::cout << "[STEP 3] Starting TaskManager..." << std::endl;
    mgr.start();
    std::cout << "[STEP 4] Waiting for completion..." << std::endl;

    // Wait for completion (poll up to 60s).
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while ((mgr.isRunning() || allFinishedCount == 0) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cout << "[STEP 5] Finished waiting. isRunning=" << mgr.isRunning() 
              << ", allFinishedCount=" << allFinishedCount 
              << ", fileStartedCount=" << fileStartedCount << std::endl;
    assert(!mgr.isRunning());
    assert(allFinishedCount == 1);
    assert(fileStartedCount == static_cast<int>(configs.size()));

    const auto& results = mgr.results();
    std::cout << "[STEP 6] Results count=" << results.size() << std::endl;
    assert(results.size() == configs.size());
    for (const auto& r : results) {
        assert(r.success);
        assert(r.totalPages == 1);
        assert(!r.watermarkText.empty());
        assert(fs::exists(r.outputPath));
        assert(fs::file_size(r.outputPath) > 0);
        // Verify subdirectory structure from result path
        fs::path parent = r.outputPath.parent_path();
        std::string parentName = pathToString(parent.filename());
        std::string fileName = pathToString(r.outputPath.filename());
        assert(parentName == sanitizeFilename(r.watermarkText));
        assert(fileName == "sample_input.pdf");
    }

    // Verify directory structure: subdirectory per watermark
    for (const auto& wm : rawWatermarks) {
        fs::path subdir = workDir / stringToPath(sanitizeFilename(wm));
        assert(fs::is_directory(subdir));
        fs::path expectedPdf = subdir / "sample_input.pdf";
        assert(fs::exists(expectedPdf));
        assert(fs::file_size(expectedPdf) > 0);
    }
    std::cout << "[RUN] testMultiWatermarkBatch (multi-PDF)\n";

    fs::path workDir2 = workDir.parent_path() / "multi_wm_e2e_2pdf";
    fs::remove_all(workDir2, ec);
    fs::create_directories(workDir2);

    fs::path inputPdf2 = workDir2 / "doc_B.pdf";
    createTestPdf(inputPdf2);
    // inputPdf still exists from above, reuse it
    fs::copy(inputPdf, workDir2 / "doc_A.pdf", ec);

    std::vector<WatermarkConfig> configs2 = {
        [&]() {
            WatermarkConfig c;
            c.text = "测试水印-α";
            c.fontSizePt = 36; c.opacity = 0.15;
            c.colorHex = "#B0B0B0"; c.rotationDegrees = -35.0;
            c.dpi = 150; c.jpegQuality = 80;
            return c;
        }(),
        [&]() {
            WatermarkConfig c;
            c.text = "测试水印-β";
            c.fontSizePt = 36; c.opacity = 0.15;
            c.colorHex = "#B0B0B0"; c.rotationDegrees = -35.0;
            c.dpi = 150; c.jpegQuality = 80;
            return c;
        }(),
    };

    TaskManager mgr2;
    mgr2.setWatermarkConfigs(configs2);
    mgr2.setOutputDirectory(workDir2);
    mgr2.addFile(workDir2 / "doc_A.pdf");
    mgr2.addFile(workDir2 / "doc_B.pdf");
    mgr2.start();

    auto deadline2 = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (mgr2.isRunning() && std::chrono::steady_clock::now() < deadline2) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(!mgr2.isRunning());
    const auto& results2 = mgr2.results();
    assert(results2.size() == 4); // 2 files × 2 watermarks

    // Verify exactly 2 subdirectories (one per watermark)
    std::vector<fs::path> subdirs2;
    for (const auto& entry : fs::directory_iterator(workDir2)) {
        if (entry.is_directory()) subdirs2.push_back(entry.path());
    }
    assert(subdirs2.size() == 2);
    std::unordered_set<std::string> subdirNames2;
    for (const auto& d : subdirs2) subdirNames2.insert(pathToString(d.filename()));
    assert(subdirNames2.count(sanitizeFilename("测试水印-α")) == 1);
    assert(subdirNames2.count(sanitizeFilename("测试水印-β")) == 1);

    // Each subdirectory has exactly 2 files (doc_A.pdf, doc_B.pdf)
    for (const auto& d : subdirs2) {
        std::vector<fs::path> entries;
        for (const auto& e : fs::directory_iterator(d)) {
            if (e.is_regular_file() &&
                e.path().extension() == ".pdf" &&
                pathToString(e.path().filename()).rfind("._", 0) != 0) {
                entries.push_back(e.path());
            }
        }
        assert(entries.size() == 2);
        std::unordered_set<std::string> pdfNames;
        for (const auto& pdf : entries) {
            pdfNames.insert(pathToString(pdf.filename()));
            assert(fs::file_size(pdf) > 0);
            auto docRef = PdfDocument::open(pdf);
            assert(PdfDocument::pageCount(docRef.first.get()) == 1);
        }
        assert(pdfNames.count("doc_A.pdf") == 1);
        assert(pdfNames.count("doc_B.pdf") == 1);
    }
    // ── Advanced: Cancellation during multi-batch ──
    std::cout << "[RUN] testMultiWatermarkBatch (cancellation)\n";
    fs::path workDir3 = workDir.parent_path() / "multi_wm_e2e_cancel";
    fs::remove_all(workDir3, ec);
    fs::create_directories(workDir3);
    fs::path cancelInput = workDir3 / "cancel_doc.pdf";
    createTestPdf(cancelInput);

    TaskManager mgr3;
    mgr3.setWatermarkConfigs(configs2);
    mgr3.setOutputDirectory(workDir3);
    mgr3.addFile(cancelInput);

    bool cancelFinishedFired = false;
    QObject::connect(&mgr3, &TaskManager::allFinished,
                     [&cancelFinishedFired](const std::vector<FileResult>&) {
                         cancelFinishedFired = true;
                     });
    mgr3.start();
    mgr3.cancel(); // immediately cancel

    auto deadline3 = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((mgr3.isRunning() || !cancelFinishedFired) && std::chrono::steady_clock::now() < deadline3) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(!mgr3.isRunning());
    assert(cancelFinishedFired);
    fs::remove_all(workDir3, ec);

    fs::remove_all(workDir2, ec);
    fs::remove_all(workDir, ec);
    std::cout << "[PASS] testMultiWatermarkBatch\n";
}

} // namespace pdfmark
