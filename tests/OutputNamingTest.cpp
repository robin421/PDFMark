// PDFMark - Regression test for output grouping / file naming.
//
// The user-facing requirement this guards:
//
//   "水印的文字一样，但是水印的样式不同，输出要到同一个文件夹下"
//   (same watermark text, different styles -> outputs must share ONE folder)
//
// Until 1.5.0 the output sub-directory was always the template name, so two
// styles of the same text could only coexist if the user typed different
// template names — which then scattered the outputs into different folders.
// Now `WatermarkTemplate::outputFolder` decides the folder and
// `WatermarkTemplate::variantSuffix` keeps the file names tellable apart.
#include "common/Common.h"
#include "pdf/PdfDocument.h"
#include "pdf/PdfWriter.h"
#include "task/TaskManager.h"
#include "watermark/WatermarkConfig.h"
#include "watermark/WatermarkTemplate.h"

#include <QEventLoop>
#include <QImage>
#include <QObject>
#include <QTimer>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace pdfmark {

namespace {

// A tiny single-page PDF: tests stay fast and the raster never dominates runtime.
fs::path createSinglePagePdf(const fs::path& outPath) {
    constexpr int kWidthPt = 200;
    constexpr int kHeightPt = 280;
    constexpr int kDpi = 72;
    constexpr int kWidthPx = (kWidthPt * kDpi) / 72;
    constexpr int kHeightPx = (kHeightPt * kDpi) / 72;

    QImage image(kWidthPx, kHeightPx, QImage::Format_RGB32);
    image.fill(Qt::white);

    auto doc = PdfDocument::create();
    PdfWriter::appendRasterPage(doc.get(), image, kWidthPt, kHeightPt, 60);
    PdfDocument::save(doc.get(), outPath);
    return outPath;
}

// One template = one watermark text + its own style. `variant` is the file-name
// suffix, `folder` the (optional) shared output folder.
WatermarkTemplate makeTemplate(const std::string& name,
                               const std::string& text,
                               const std::string& variant,
                               const std::string& folder) {
    WatermarkTemplate tpl;
    WatermarkConfig cfg;
    cfg.text = text;
    cfg.dpi = 72;
    cfg.fontSizePt = 18;
    tpl.watermarks.push_back(cfg);
    tpl.name = name;
    tpl.variantSuffix = variant;
    tpl.outputFolder = folder;
    return tpl;
}

std::vector<FileResult> runBatch(const fs::path& input,
                                 const std::vector<WatermarkTemplate>& tpls,
                                 const fs::path& outDir) {
    std::vector<TaskManager::FileSubtask> subtasks;
    subtasks.reserve(tpls.size());
    for (const auto& tpl : tpls) subtasks.push_back({input, tpl});

    TaskManager tm;
    tm.setSubtasks(subtasks);
    tm.setOutputDirectory(outDir);
    tm.setConcurrencyPolicy(ConcurrencyPolicy{});

    std::vector<FileResult> got;
    bool finished = false;
    QEventLoop loop;
    QObject::connect(&tm, &TaskManager::allFinished, &loop,
                     [&](const std::vector<FileResult>& r) { got = r; finished = true; loop.quit(); });
    QTimer::singleShot(60000, &loop, [&]() { loop.quit(); });
    tm.start();
    loop.exec();
    tm.shutdown();

    assert(finished);
    return got;
}

// Names of the files actually written into `dir` (empty when it does not exist).
// macOS junk and half-written temp files are ignored: on foreign filesystems
// (exFAT/NTFS, e.g. an external drive) the OS drops AppleDouble "­._name"
// sidecars next to every file, and the pipeline stages "~name.<id>.tmp" in the
// destination folder, so neither must be counted as an output.
std::set<std::string> filesIn(const fs::path& dir) {
    std::set<std::string> names;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return names;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = pathToString(entry.path().filename());
        if (name.rfind("._", 0) == 0 || name == ".DS_Store" || name.rfind("~", 0) == 0) continue;
        names.insert(name);
    }
    return names;
}

std::set<std::string> parentNamesOf(const std::vector<FileResult>& results) {
    std::set<std::string> names;
    for (const auto& r : results) names.insert(pathToString(r.outputPath.parent_path().filename()));
    return names;
}

std::set<std::string> fileNamesOf(const std::vector<FileResult>& results) {
    std::set<std::string> names;
    for (const auto& r : results) names.insert(pathToString(r.outputPath.filename()));
    return names;
}

void expectAllSucceeded(const std::vector<FileResult>& results) {
    for (const auto& r : results) {
        if (!r.success) std::cerr << "  failure: " << r.errorMessage << "\n";
        assert(r.success);
        assert(fs::exists(r.outputPath));
        assert(fs::file_size(r.outputPath) > 0);
    }
}

// Prints everything under `root` (path strings come from production helpers, so
// this stays readable even when a test-built path is wrong for the platform).
void dumpTree(const fs::path& root) {
    std::error_code ec;
    // std::cerr + std::endl: assert() aborts, which drops buffered stdout.
    std::cerr << "  [DIAG] tree of " << pathToString(root)
              << " (dir=" << static_cast<int>(fs::is_directory(root, ec)) << ")" << std::endl;
    if (!fs::is_directory(root, ec)) return;
    for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
        std::cerr << "         " << (e.is_directory() ? "[d] " : "    ")
                  << pathToString(e.path()) << std::endl;
    }
}

} // namespace

void testOutputNaming() {
    std::cout << "[RUN] testOutputNaming\n";

    const fs::path workDir = "output_naming_e2e";
    std::error_code ec;
    fs::remove_all(workDir, ec);
    fs::create_directories(workDir, ec);

    const fs::path inputPdf = createSinglePagePdf(workDir / "sample_input.pdf");

    // ── The reported scenario: same text, different styles, ONE folder ─────
    // Two templates whose watermark text is identical ("机密") but whose styles
    // differ. They explicitly share the folder "机密" and carry distinct variant
    // suffixes, so both outputs must land in <out>/机密/ as two separate files.
    {
        const fs::path outDir = workDir / "out_variants";
        const std::vector<WatermarkTemplate> tpls = {
            makeTemplate("机密-红色", "机密", "红色", "机密"),
            makeTemplate("机密-蓝色", "机密", "蓝色", "机密"),
        };
        const auto got = runBatch(inputPdf, tpls, outDir);
        assert(got.size() == 2);
        expectAllSucceeded(got);

        // One folder, reached through the explicit outputFolder (not the name).
        assert(parentNamesOf(got).size() == 1);
        assert(parentNamesOf(got).count("机密") == 1);
        assert(got[0].outputPath.parent_path().parent_path() == outDir);

        const std::set<std::string> names = fileNamesOf(got);
        assert(names.count("sample_input_红色.pdf") == 1);
        assert(names.count("sample_input_蓝色.pdf") == 1);

        const std::set<std::string> onDisk = filesIn(outDir / stringToPath("机密"));
        if (onDisk.size() != 2) dumpTree(outDir);
        assert(onDisk.size() == 2);
        assert(onDisk.count("sample_input_红色.pdf") == 1);
        assert(onDisk.count("sample_input_蓝色.pdf") == 1);

        // Result rows stay distinguishable in the UI/日志.
        std::set<std::string> labels;
        for (const auto& r : got) labels.insert(r.watermarkText);
        assert(labels.count("机密-红色·红色") == 1);
        assert(labels.count("机密-蓝色·蓝色") == 1);

        std::cout << "  same text x 2 styles -> folder " << *parentNamesOf(got).begin()
                  << "/ (" << onDisk.size() << " files)\n";
    }

    // ── Full dialog flow: same text typed twice, second one auto-merged ────
    // This mirrors what the UI does now: the user types the same watermark text
    // twice with different styles, leaves 输出文件夹 empty, and only sets a
    // variant suffix on the second one. resolveTemplateNaming() renames the
    // second template and pins its folder to the first one's name.
    {
        const fs::path outDir = workDir / "out_auto_merge";
        std::vector<WatermarkConfig> lines;
        WatermarkConfig cfg;
        cfg.text = "机密";
        cfg.dpi = 72;
        cfg.fontSizePt = 18;
        lines.push_back(cfg);

        const TemplateNaming first = resolveTemplateNaming("", lines, {}, "");
        const TemplateNaming second = resolveTemplateNaming("", lines, {"机密"}, "");
        assert(first.name == "机密" && first.outputFolder.empty());
        assert(second.name == "机密 (2)" && second.outputFolder == "机密");

        WatermarkTemplate a, b;
        a.name = first.name;   a.outputFolder = first.outputFolder;
        a.variantSuffix.clear();
        b.name = second.name;  b.outputFolder = second.outputFolder;
        b.variantSuffix = "红色";                  // the only style-specific input
        a.watermarks = b.watermarks = lines;

        const auto got = runBatch(inputPdf, {a, b}, outDir);
        assert(got.size() == 2);
        expectAllSucceeded(got);

        assert(parentNamesOf(got).size() == 1);
        assert(parentNamesOf(got).count("机密") == 1);
        const std::set<std::string> onDisk = filesIn(outDir / stringToPath("机密"));
        assert(onDisk.size() == 2);
        assert(onDisk.count("sample_input.pdf") == 1);        // untouched first style
        assert(onDisk.count("sample_input_红色.pdf") == 1);   // variant suffix
        std::cout << "  dialog flow (same text x2) -> 机密/ " << onDisk.size() << " files\n";
    }

    // ── No variant suffix: same folder, files fall back to _2 / _3 ─────────
    {
        const fs::path outDir = workDir / "out_nosuffix";
        const std::vector<WatermarkTemplate> tpls = {
            makeTemplate("机密", "机密", "", "机密"),
            makeTemplate("机密 (2)", "机密", "", "机密"),
        };
        const auto got = runBatch(inputPdf, tpls, outDir);
        assert(got.size() == 2);
        expectAllSucceeded(got);
        assert(parentNamesOf(got).size() == 1);
        assert(parentNamesOf(got).count("机密") == 1);

        const std::set<std::string> onDisk = filesIn(outDir / stringToPath("机密"));
        assert(onDisk.size() == 2);                            // collision resolved
        assert(onDisk.count("sample_input.pdf") == 1);
        // The counter has always started at 1 ("_1", not "_2"); the README used to
        // claim otherwise. Kept as-is so existing output layouts do not shift.
        assert(onDisk.count("sample_input_1.pdf") == 1);
        std::cout << "  blank suffix x 2 -> " << *parentNamesOf(got).begin() << "/ "
                  << onDisk.size() << " files (counter fallback)\n";
    }

    // ── Different texts merged into one shared folder ──────────────────────
    {
        const fs::path outDir = workDir / "out_shared";
        const std::vector<WatermarkTemplate> tpls = {
            makeTemplate("公司A", "公司A机密", "甲", "对外"),
            makeTemplate("公司B", "公司B机密", "乙", "对外"),
        };
        const auto got = runBatch(inputPdf, tpls, outDir);
        assert(got.size() == 2);
        expectAllSucceeded(got);
        assert(parentNamesOf(got).size() == 1);
        assert(parentNamesOf(got).count("对外") == 1);
        assert(filesIn(outDir / stringToPath("对外")).size() == 2);
    }

    // ── Same variant suffix twice: counter is appended after the suffix ────
    {
        const fs::path outDir = workDir / "out_same_suffix";
        const std::vector<WatermarkTemplate> tpls = {
            makeTemplate("机密-红1", "机密", "红色", "机密"),
            makeTemplate("机密-红2", "机密", "红色", "机密"),
        };
        const auto got = runBatch(inputPdf, tpls, outDir);
        assert(got.size() == 2);
        expectAllSucceeded(got);
        assert(parentNamesOf(got).size() == 1);

        const std::set<std::string> onDisk = filesIn(outDir / stringToPath("机密"));
        assert(onDisk.size() == 2);
        assert(onDisk.count("sample_input_红色.pdf") == 1);
        assert(onDisk.count("sample_input_红色_1.pdf") == 1);
    }

    // ── Windows: a folder name ending in dots is unusable ────────────────
    // Derived template names of watermark texts longer than 6 characters always
    // end with "..." (e.g. "内部文件-李..."). Win32 strips those dots when
    // CREATING the directory but not when the same name is reused as a path
    // prefix ("...\内部文件-李...\~x.tmp" -> ENOENT), so on Windows every such
    // output failed with "Failed to open output file". Regression guard.
    {
        const fs::path outDir = workDir / "out_trailing_dots";
        WatermarkTemplate tpl = makeTemplate("内部文件-李...", "内部文件-李四", "", "");
        assert(templateNameFromConfigs(tpl.watermarks) == "内部文件-李...");   // what the UI derives

        const auto got = runBatch(inputPdf, {tpl}, outDir);
        assert(got.size() == 1);
        expectAllSucceeded(got);
        assert(pathToString(got[0].outputPath.parent_path().filename()) == "内部文件-李");
        assert(filesIn(outDir / stringToPath("内部文件-李")).count("sample_input.pdf") == 1);
    }

    // ── Regression: no outputFolder => folder == template name (1.4.0 rule) ─
    {
        const fs::path outDir = workDir / "out_legacy";
        const std::vector<WatermarkTemplate> tpls = {
            makeTemplate("机密-张三", "机密-张三", "", ""),
        };
        const auto got = runBatch(inputPdf, tpls, outDir);
        assert(got.size() == 1);
        expectAllSucceeded(got);
        assert(pathToString(got[0].outputPath.parent_path().filename()) == "机密-张三");
        assert(pathToString(got[0].outputPath.filename()) == "sample_input.pdf");
        assert(got[0].watermarkText == "机密-张三");           // no «·» without a variant
    }

    // ── Illegal characters are sanitized; unusable parts degrade ───────────
    {
        const fs::path outDir = workDir / "out_illegal";
        const std::vector<WatermarkTemplate> tpls = {
            makeTemplate("非法", "机密", "a/b:c*", "A/B"),
            makeTemplate("空白", "机密", "___", "/"),
        };
        const auto got = runBatch(inputPdf, tpls, outDir);
        assert(got.size() == 2);
        expectAllSucceeded(got);

        // "A/B" -> folder "A_B"; "a/b:c*" -> suffix "a_b_c" (trailing _ trimmed).
        assert(filesIn(outDir / "A_B").count("sample_input_a_b_c.pdf") == 1);

        // A suffix of "___" sanitizes to nothing -> treated as unset (NOT
        // "watermark"); the folder "/" falls back to the "watermark" folder.
        const std::set<std::string> onDisk = filesIn(outDir / "watermark");
        assert(onDisk.size() == 1);
        assert(onDisk.count("sample_input.pdf") == 1);
    }

    fs::remove_all(workDir, ec);
    std::cout << "[PASS] testOutputNaming\n";
}

} // namespace pdfmark
