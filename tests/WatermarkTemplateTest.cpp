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

    // ── output naming fields: variantSuffix / outputFolder ────────────────
    {
        // Empty outputFolder keeps the historical rule: folder == template name.
        WatermarkTemplate named = tpl;
        assert(named.variantSuffix.empty());
        assert(named.outputFolder.empty());
        assert(named.folderName() == "机密模板");
        assert(named.displayLabel() == "机密模板");   // no variant => plain name

        // displayLabel() keeps several styles of one text apart in the UI.
        named.variantSuffix = "红色";
        assert(named.displayLabel() == "机密模板·红色");

        // An explicit folder wins over the name (this is the "merge" switch
        // that puts several styles of one text into ONE folder).
        named.outputFolder = "机密";
        assert(named.folderName() == "机密");
        assert(named.name == "机密模板");          // label untouched
        assert(named.displayLabel() == "机密模板·红色");

        // folderName() must be a reference into the template, not a copy, so
        // later edits are visible without re-reading it.
        named.outputFolder.clear();
        assert(named.folderName() == named.name);
        named.variantSuffix.clear();
        assert(named.displayLabel() == named.name);
    }

    // ── trimCopy ─────────────────────────────────────────────────────────
    assert(trimCopy("  机密  ") == "机密");
    assert(trimCopy("\t\r\n 外部 \n") == "外部");
    assert(trimCopy("   ").empty());
    assert(trimCopy("").empty());

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

    // ── schema 2: variantSuffix + outputFolder round-trip ──────────────────
    {
        WatermarkTemplate variant = makeTemplate("机密-红色");
        variant.variantSuffix = "红色";
        variant.outputFolder = "机密";

        WatermarkTemplateStore store(jsonPath);
        assert(store.load());
        store.addOrReplace(variant);
        assert(store.save());
    }
    {
        WatermarkTemplateStore store(jsonPath);
        assert(store.load());
        const WatermarkTemplate* v = store.find(QString::fromUtf8("机密-红色"));
        assert(v != nullptr);
        assert(v->variantSuffix == "红色");
        assert(v->outputFolder == "机密");
        assert(v->folderName() == "机密");        // explicit folder wins
        assert(v->displayLabel() == "机密-红色·红色");

        const WatermarkTemplate* p = store.find(QString::fromUtf8("对外模板"));
        assert(p != nullptr);
        assert(p->variantSuffix.empty());          // never written => still empty
        assert(p->outputFolder.empty());
        assert(p->folderName() == "对外模板");      // ...and falls back to the name
    }

    // ── v1 files (no naming keys at all) keep loading ──────────────────────
    {
        const fs::path v1Path = dir / "v1_templates.json";
        std::ofstream out(v1Path.string());
        out << R"({"version":1,"templates":[{"name":"旧模板","watermarks":[{"text":"旧水印"}]}]})";
        out.close();

        WatermarkTemplateStore store(QString::fromStdString(pathToString(v1Path)));
        assert(store.load());
        assert(store.templates().size() == 1);
        assert(store.templates()[0].variantSuffix.empty());
        assert(store.templates()[0].outputFolder.empty());
        assert(store.templates()[0].folderName() == "旧模板");
    }

    // ── duplicate names in a hand-edited file: first wins, no ghost rows ───
    {
        const fs::path dupPath = dir / "dup_templates.json";
        std::ofstream out(dupPath.string());
        out << R"({"version":2,"templates":[
            {"name":"重复","watermarks":[{"text":"A"}]},
            {"name":"重复","watermarks":[{"text":"B"}]}]})";
        out.close();

        WatermarkTemplateStore store(QString::fromStdString(pathToString(dupPath)));
        assert(store.load());
        assert(store.templates().size() == 1);   // second entry dropped
        assert(store.templates()[0].watermarks[0].text == "A");
    }

    // ── names() / folderNames(): what the dialog is built from ─────────────
    {
        WatermarkTemplateStore store(jsonPath);
        assert(store.load());

        // A second style of "机密" that merges into the same folder.
        WatermarkTemplate blue = makeTemplate("机密-蓝色");
        blue.variantSuffix = "蓝色";
        blue.outputFolder = "机密";
        store.addOrReplace(blue);

        const QStringList names = store.names();
        assert(names.contains(QString::fromUtf8("机密-红色")));
        assert(names.contains(QString::fromUtf8("对外模板")));
        assert(names.contains(QString::fromUtf8("机密-蓝色")));

        // Effective folders: "机密" appears once even though two templates share
        // it, and a template without an explicit folder contributes its name.
        const QStringList folders = store.folderNames();
        assert(folders.count(QString::fromUtf8("机密")) == 1);
        assert(folders.contains(QString::fromUtf8("机密")));
        assert(folders.contains(QString::fromUtf8("对外模板")));
        assert(!folders.contains(QString::fromUtf8("机密-红色")));   // merged away
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

        // The two optional naming fields are trimmed too; a blank value means
        // "not set" and must not survive as whitespace (that would create a
        // folder literally called "   " / a file named report_  .pdf).
        WatermarkTemplate spaced;
        WatermarkConfig c; c.text = "机密";
        spaced.watermarks.push_back(c);
        spaced.name = "  变体一  ";
        spaced.variantSuffix = "  红色  ";
        spaced.outputFolder = "\t 机密 \n";
        assert(normalizeTemplate(spaced));
        assert(spaced.name == "变体一");
        assert(spaced.variantSuffix == "红色");
        assert(spaced.outputFolder == "机密");
        assert(spaced.folderName() == "机密");

        // Blank values collapse to empty (= fall back to name / no suffix).
        WatermarkTemplate blanks;
        blanks.watermarks.push_back(c);
        blanks.name = "变体二";
        blanks.variantSuffix = "   ";
        blanks.outputFolder = "\n";
        assert(normalizeTemplate(blanks));
        assert(blanks.variantSuffix.empty());
        assert(blanks.outputFolder.empty());
        assert(blanks.folderName() == "变体二");

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

        a.text = "一二三四五六";
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "一二三四五六");

        // No length limit: the whole first line becomes the name (it used to be
        // truncated to 6 characters + "...").
        a.text = "机密文件请勿外传";
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "机密文件请勿外传");

        a.text = "ABCDEFGH";
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "ABCDEFGH");

        a.text = "本文件仅供内部使用，未经许可不得外传（含附件与附表）";
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "本文件仅供内部使用，未经许可不得外传（含附件与附表）");
        assert(templateNameFromConfigs(one).size() > 6 * 3);   // UTF-8 bytes

        // Only the first LINE is used, even if a row somehow contains newlines.
        a.text = "第一行名称\n第二行";
        one.assign(1, a);
        assert(templateNameFromConfigs(one) == "第一行名称");

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

    // ── resolveTemplateNaming: the reported "same text, different styles" ──
    {
        std::vector<WatermarkConfig> lines;
        WatermarkConfig c;
        c.text = "机密";
        lines.push_back(c);

        const std::vector<std::string> taken = {"机密"};

        // First template: nothing taken yet, no explicit folder -> folder unset
        // (= template name), so v1-style layouts are preserved byte for byte.
        const TemplateNaming first = resolveTemplateNaming("", lines, {}, "");
        assert(first.name == "机密");
        assert(first.outputFolder.empty());

        // Second style of the SAME text: the name collides, so it becomes a
        // sibling AND the original name is pinned as the folder. Result: both
        // templates write into <out>/机密/.
        const TemplateNaming second = resolveTemplateNaming("", lines, taken, "");
        assert(second.name == "机密 (2)");
        assert(second.outputFolder == "机密");
        assert(second.name != first.name);

        // Third style: keeps counting.
        const std::vector<std::string> taken2 = {"机密", "机密 (2)"};   // NOLINT
        const TemplateNaming third = resolveTemplateNaming("", lines, taken2, "");
        assert(third.name == "机密 (3)");
        assert(third.outputFolder == "机密");

        // An explicit folder always wins over the collision rule...
        const TemplateNaming explicitFolder = resolveTemplateNaming("", lines, taken, "  对外  ");
        assert(explicitFolder.name == "机密 (2)");
        assert(explicitFolder.outputFolder == "对外");

        // ...and a name that does NOT collide keeps the folder unset.
        const TemplateNaming another = resolveTemplateNaming("机密-蓝色", lines, taken, "");
        assert(another.name == "机密-蓝色");
        assert(another.outputFolder.empty());

        // Typed name wins over the derived one, and is trimmed.
        const TemplateNaming typed = resolveTemplateNaming("  甲公司  ", lines, {}, "");
        assert(typed.name == "甲公司");

        // No text and no name: the name falls back to the generic placeholder,
        // and normalizeTemplate() then rejects the template (the dialog keeps
        // showing its "请至少填写一行水印文字" warning instead of saving it).
        WatermarkTemplate nothing;
        nothing.name = resolveTemplateNaming("", {}, {}, "").name;
        assert(nothing.name == "watermark");
        assert(!normalizeTemplate(nothing));
    }

    // ── uniqueNameAmong ───────────────────────────────────────────────────
    assert(uniqueNameAmong("A", {}) == "A");
    assert(uniqueNameAmong("A", {"A"}) == "A (2)");
    assert(uniqueNameAmong("A", {"A", "A (2)"}) == "A (3)");
    assert(uniqueNameAmong("A (2)", {"A"}) == "A (2)");
    assert(uniqueNameAmong("", {}) == "watermark");
    assert(uniqueNameAmong("  ", {}) == "watermark");
    assert(uniqueNameAmong("", {"watermark"}) == "watermark (2)");

    fs::remove_all(dir, ec);
    std::cout << "[PASS] testWatermarkTemplate\n";
}

} // namespace pdfmark
