// PDFMark - Tests for reusable watermark templates and their JSON store.
#include "watermark/WatermarkTemplate.h"
#include "watermark/WatermarkTemplateStore.h"
#include "watermark/WatermarkSelection.h"
#include "common/Common.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace pdfmark {

namespace {

bool nearly(double a, double b) { return std::fabs(a - b) < 1e-9; }

WatermarkTemplate makeTemplate(const std::string& name, bool checked = true) {
    WatermarkTemplate tpl;
    tpl.name = name;

    WatermarkConfig a;
    a.text = "机密-张三";
    a.fontFamily = "Times New Roman";
    a.fontBold = false;
    a.fontItalic = true;
    a.fontSizePt = 32;
    a.opacity = 0.22;
    a.colorHex = "#123456";
    a.rotationDegrees = -45.0;
    a.dpi = 300;
    a.jpegQuality = 70;
    a.selected = checked;
    tpl.watermarks.push_back(a);

    WatermarkConfig b = a;
    b.text = "请勿外传";
    b.selected = false;
    tpl.watermarks.push_back(b);
    return tpl;
}

void expectEqual(const WatermarkConfig& a, const WatermarkConfig& b) {
    assert(a.text == b.text);
    assert(a.fontFamily == b.fontFamily);
    assert(a.fontBold == b.fontBold);
    assert(a.fontItalic == b.fontItalic);
    assert(a.fontSizePt == b.fontSizePt);
    assert(nearly(a.opacity, b.opacity));
    assert(a.colorHex == b.colorHex);
    assert(nearly(a.rotationDegrees, b.rotationDegrees));
    assert(a.dpi == b.dpi);
    assert(a.jpegQuality == b.jpegQuality);
    assert(a.selected == b.selected);
}

} // namespace

void testWatermarkTemplate() {
    std::cout << "[RUN] testWatermarkTemplate\n";

    // ── validity ──────────────────────────────────────────────────────────
    WatermarkTemplate empty;
    assert(!empty.isValid());                 // no name, no text
    empty.name = "x";
    assert(!empty.isValid());                 // name but no watermark
    WatermarkTemplate tpl = makeTemplate("机密模板");
    assert(tpl.isValid());
    assert(tpl.style().text == "机密-张三");
    assert(tpl.style().dpi == 300);

    // ── templateToConfigs drops empty text lines ──────────────────────────
    WatermarkTemplate withBlank = tpl;
    WatermarkConfig blank = tpl.watermarks[0];
    blank.text = "";
    withBlank.watermarks.push_back(blank);
    auto expanded = templateToConfigs(withBlank);
    assert(expanded.size() == 2);
    for (const auto& cfg : expanded) assert(!cfg.text.empty());

    // ── store round-trip (UTF-8 names + full style) ───────────────────────
    const fs::path dir = fs::temp_directory_path() / "pdfmark_template_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const QString jsonPath = QString::fromStdString(pathToString(dir / "templates.json"));

    {
        WatermarkTemplateStore store(jsonPath);
        assert(store.templates().empty());
        assert(store.addOrReplace(tpl) == false); // first insert
        assert(store.addOrReplace(tpl) == true);  // same name replaced
        assert(store.templates().size() == 1);
        assert(store.save());
    }

    {
        WatermarkTemplateStore store(jsonPath);
        assert(store.load());
        assert(store.templates().size() == 1);
        const WatermarkTemplate* loaded = store.find(QString::fromUtf8("机密模板"));
        assert(loaded != nullptr);
        assert(loaded->name == "机密模板");
        assert(loaded->watermarks.size() == 2);
        expectEqual(loaded->watermarks[0], tpl.watermarks[0]);
        expectEqual(loaded->watermarks[1], tpl.watermarks[1]);
    }

    // ── rename / remove ───────────────────────────────────────────────────
    {
        WatermarkTemplateStore store(jsonPath);
        store.load();
        assert(store.rename(QString::fromUtf8("机密模板"), QString::fromUtf8("对外模板")));
        assert(store.find(QString::fromUtf8("机密模板")) == nullptr);
        assert(store.find(QString::fromUtf8("对外模板")) != nullptr);
        // renaming onto an existing name must fail
        WatermarkTemplate other = makeTemplate("另一个");
        store.addOrReplace(other);
        assert(!store.rename(QString::fromUtf8("对外模板"), QString::fromUtf8("另一个")));
        assert(store.save());
    }
    {
        WatermarkTemplateStore store(jsonPath);
        store.load();
        assert(store.templates().size() == 2);
        assert(store.remove(QString::fromUtf8("另一个")));
        assert(!store.remove(QString::fromUtf8("不存在")));
        assert(store.templates().size() == 1);
        assert(store.save());
    }

    // ── template -> per-file configs + "生成所选" filtering ────────────────
    {
        auto configs = templateToConfigs(tpl);
        assert(configs.size() == 2);
        // "生成全部" style: every config is a candidate
        // "生成所选" style: only checked entries survive
        auto selectedOnly = filterSelectedWatermarks(configs);
        assert(selectedOnly.size() == 1);
        assert(selectedOnly[0].text == "机密-张三");
        assert(selectedOnly[0].dpi == 300);
    }

    // ── corrupt JSON must degrade to empty, never throw ───────────────────
    {
        const fs::path badPath = dir / "broken.json";
        std::ofstream out(badPath.string());
        out << "{ this is not json";
        out.close();

        WatermarkTemplateStore store(QString::fromStdString(pathToString(badPath)));
        assert(!store.load());
        assert(store.templates().empty());
    }

    // ── missing file is not an error, just empty ──────────────────────────
    {
        WatermarkTemplateStore store(QString::fromStdString(
            pathToString(dir / "does_not_exist.json")));
        assert(!store.load());
        assert(store.templates().empty());
    }

    fs::remove_all(dir, ec);
    std::cout << "[PASS] testWatermarkTemplate\n";
}

} // namespace pdfmark
