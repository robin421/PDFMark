// PDFMark - Tests for reusable watermark templates and their JSON store.
#include "watermark/WatermarkTemplate.h"
#include "watermark/WatermarkTemplateStore.h"
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

    // ── template -> per-file configs ──────────────────────────────────────
    {
        // Line-level "selected" is no longer part of the model: every line of a
        // template is applied, and the checkbox lives on the template itself.
        auto configs = templateToConfigs(tpl);
        assert(configs.size() == 2);
        assert(configs[0].text == "机密-张三");
        assert(configs[0].dpi == 300);
        assert(configs[1].text == "请勿外传");
    }

    // ── normalizeTemplate: validation + automatic name ────────────────────
    {
        WatermarkTemplate t;
        WatermarkConfig a; a.text = "  甲公司机密  ";
        t.watermarks.push_back(a);
        WatermarkConfig blank; blank.text = "   ";
        t.watermarks.push_back(blank);
        assert(normalizeTemplate(t));
        assert(t.watermarks.size() == 1);              // blank line dropped
        assert(t.watermarks[0].text == "甲公司机密");   // trimmed
        assert(t.name == "甲公司机密");                 // name derived

        WatermarkTemplate named;
        WatermarkConfig b; b.text = "一二三四五六七八";
        named.watermarks.push_back(b);
        named.name = "  自定义名  ";
        assert(normalizeTemplate(named));
        assert(named.name == "自定义名");               // user name wins, trimmed

        WatermarkTemplate empty;
        empty.watermarks.push_back(blank);
        assert(!normalizeTemplate(empty));             // nothing usable
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

    // ── template name derived from the watermark text (no user input) ─────
    {
        std::vector<WatermarkConfig> one;
        WatermarkConfig a;
        a.text = "ABCV";
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "ABCV");

        a.text = "机密";
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "机密");

        a.text = "一二三四五六";          // exactly 6 -> unchanged
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "一二三四五六");

        a.text = "机密文件请勿外传";      // 8 chars -> first 6 + "..."
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "机密文件请勿...");

        a.text = "ABCDEFGH";              // 8 ASCII -> first 6 + "..."
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "ABCDEF...");

        a.text = "  机密  ";              // trimmed
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "机密");

        // first non-empty line wins; blank-only input yields no name
        std::vector<WatermarkConfig> many;
        WatermarkConfig blank;
        blank.text = "";
        many.push_back(blank);
        WatermarkConfig real;
        real.text = "内部资料";
        many.push_back(real);
        assert(templateNameFromConfigs(many) == "内部资料");

        std::vector<WatermarkConfig> spaces;
        WatermarkConfig onlySpace;
        onlySpace.text = "   ";
        spaces.push_back(onlySpace);
        assert(templateNameFromConfigs(spaces).empty());
        assert(templateNameFromConfigs({}).empty());
    }

    fs::remove_all(dir, ec);
    std::cout << "[PASS] testWatermarkTemplate\n";
}

} // namespace pdfmark
