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
#include <QCoreApplication>
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
    std::cout << "[RUN] testMultiWatermarkBatch\n";

    fs::path workDir = "multi_wm_e2e";
    std::error_code ec;
    fs::remove_all(workDir, ec);
    fs::create_directories(workDir);

    fs::path inputPdf = workDir / "sample_input.pdf";
    createTestPdf(inputPdf);
    assert(fs::exists(inputPdf));
    assert(fs::file_size(inputPdf) > 0);

    // Watermark list: includes regular text and a string with
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

    // Sanitized names that the TaskManager should produce.
    // Sanitized names organized in per-PDF subdirectories
    std::vector<fs::path> expectedOutputs = {
        workDir / "sample_input" / "机密-张三.pdf",
        workDir / "sample_input" / "内部文件-李四.pdf",
        workDir / "sample_input" / "部门_审核_王五.pdf",
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

    mgr.start();

    // Wait for completion (poll up to 60s).
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while ((mgr.isRunning() || allFinishedCount == 0) && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    QCoreApplication::processEvents();
    assert(!mgr.isRunning());
    assert(allFinishedCount == 1);
    assert(fileStartedCount == static_cast<int>(configs.size()));

    const auto& results = mgr.results();
    assert(results.size() == configs.size());

    for (const auto& r : results) {
        assert(r.success);
        assert(r.totalPages == 1);
        assert(!r.watermarkText.empty());
        assert(fs::exists(r.outputPath));
        assert(fs::file_size(r.outputPath) > 0);
        // Verify subdirectory structure from result path
        fs::path parent = r.outputPath.parent_path();
        std::string parentName = parent.filename().string();
        std::string fileName = r.outputPath.filename().string();
        assert(parentName == "sample_input");
        assert(fileName == sanitizeFilename(r.watermarkText) + ".pdf");
    }

    // Verify directory structure: subdirectory per PDF
    fs::path subdir = workDir / "sample_input";
    assert(fs::is_directory(subdir));
    std::vector<fs::path> pdfsInSubdir;
    for (const auto& entry : fs::directory_iterator(subdir)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".pdf" &&
            entry.path().filename().string().rfind("._", 0) != 0) {
            pdfsInSubdir.push_back(entry.path());
        }
    }
    assert(static_cast<int>(pdfsInSubdir.size()) == static_cast<int>(configs.size()));
    // ── Advanced: 2 PDFs × 2 watermarks → 2 subdirs, 4 total files ──
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
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    QCoreApplication::processEvents();
    assert(!mgr2.isRunning());
    const auto& results2 = mgr2.results();
    assert(results2.size() == 4); // 2 files × 2 watermarks

    // Verify exactly 2 subdirectories
    std::vector<fs::path> subdirs2;
    for (const auto& entry : fs::directory_iterator(workDir2)) {
        if (entry.is_directory()) subdirs2.push_back(entry.path());
    }
    assert(subdirs2.size() == 2);
    std::unordered_set<std::string> subdirNames;
    for (const auto& d : subdirs2) subdirNames.insert(d.filename().string());
    assert(subdirNames.count("doc_A") == 1);
    assert(subdirNames.count("doc_B") == 1);

    // Each subdirectory has exactly 2 files
    for (const auto& d : subdirs2) {
        std::vector<fs::path> entries;
        for (const auto& e : fs::directory_iterator(d)) {
            if (e.is_regular_file() &&
                e.path().extension() == ".pdf" &&
                e.path().filename().string().rfind("._", 0) != 0) {
                entries.push_back(e.path());
            }
        }
        assert(entries.size() == 2);
        for (const auto& pdf : entries) {
            assert(fs::file_size(pdf) > 0);
            auto docRef = PdfDocument::open(pdf);
            assert(PdfDocument::pageCount(docRef.first.get()) == 1);
        }
    }
    fs::remove_all(workDir2, ec);
    fs::remove_all(workDir, ec);
    std::cout << "[PASS] testMultiWatermarkBatch\n";
}

} // namespace pdfmark
