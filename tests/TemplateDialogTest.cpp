// PDFMark - Interaction test for the template editor.
//
// User-facing requirements guarded here:
//
//   * "水印的文字一样，但是水印的样式不同，输出要到同一个文件夹下"
//   * "模板名称不需要填写，默认跟水印内容作为模板名"
//   * "水印样式可以选择样式1、样式2 ... 样式更改后自动保存配置"
//
// resolveTemplateNaming() owns the folder decision and is unit tested in
// WatermarkTemplateTest; this test drives the ACTUAL dialog (offscreen) and
// checks what a user typing the same text twice ends up saving — including the
// rename-to-sibling and the auto-merged output folder, plus the live hint that
// tells the user where the files will land.
#include "ui/TemplateEditDialog.h"
#include "watermark/WatermarkTemplateStore.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QSlider>

#include <cassert>
#include <cmath>
#include <iostream>

namespace pdfmark {

namespace {

const char* kVariantEdit = "templateVariantEdit";
const char* kFolderCombo = "templateFolderCombo";
const char* kOutputHint = "templateOutputHint";
const char* kStyleCombo = "templateStyleCombo";

bool nearly(double a, double b) { return std::fabs(a - b) < 1e-9; }

// Types the watermark text and returns the accepted result. The template name is
// never a parameter: it is always derived from the text now.
WatermarkTemplate driveDialog(const WatermarkTemplate& initial,
                              bool creating,
                              const QStringList& taken,
                              const QStringList& folders,
                              const char* text,
                              const char* variant = nullptr,
                              const char* folder = nullptr) {
    TemplateEditDialog dlg(initial, creating, taken, folders, nullptr, nullptr);
    auto* variantEdit = dlg.findChild<QLineEdit*>(kVariantEdit);
    auto* folderCombo = dlg.findChild<QComboBox*>(kFolderCombo);
    auto* textEdit = dlg.findChild<QLineEdit*>("watermarkTextEdit");
    assert(variantEdit && folderCombo && textEdit);

    textEdit->setText(QString::fromUtf8(text));
    if (variant) variantEdit->setText(QString::fromUtf8(variant));
    if (folder) folderCombo->setCurrentText(QString::fromUtf8(folder));

    // accept() is a private override of a public slot; invoke it by name.
    assert(QMetaObject::invokeMethod(&dlg, "accept", Qt::DirectConnection));
    return dlg.result();
}

WatermarkTemplate existingTemplate(const std::string& name,
                                   const std::string& text,
                                   const std::string& variant = std::string()) {
    WatermarkTemplate tpl;
    tpl.name = name;
    tpl.variantSuffix = variant;
    WatermarkConfig cfg;
    cfg.text = text;
    tpl.watermarks.push_back(cfg);
    return tpl;
}

} // namespace

void testTemplateDialogNaming() {
    std::cout << "[RUN] testTemplateDialogNaming\n";

    // ── First style: nothing taken, blank folder => folder stays unset ─────
    {
        const WatermarkTemplate t = driveDialog(WatermarkTemplate{}, true, {}, {}, "机密");
        assert(t.name == "机密");                 // derived, never typed
        assert(t.outputFolder.empty());          // == "use the template name"
        assert(t.variantSuffix.empty());
        assert(t.folderName() == "机密");
    }

    // ── Second style of the SAME text: sibling template, SAME folder ──────
    {
        const WatermarkTemplate t = driveDialog(WatermarkTemplate{}, true, {"机密"}, {"机密"}, "机密");
        assert(t.name == "机密 (2)");             // not an overwrite
        assert(t.outputFolder == "机密");         // merged with the first style
        assert(t.folderName() == "机密");
    }

    // ── ...and the live hint says so before saving ───────────────────────
    {
        TemplateEditDialog dlg(WatermarkTemplate{}, true, {"机密"}, {"机密"}, nullptr, nullptr);
        dlg.findChild<QLineEdit*>("watermarkTextEdit")->setText(QString::fromUtf8("机密"));
        const QString hint = dlg.findChild<QLabel*>(kOutputHint)->text();
        assert(hint.contains(QString::fromUtf8("机密 (2)")));
        assert(hint.contains(QString::fromUtf8("机密/")));
    }

    // ── Long names are kept verbatim (the 6-character truncation is gone) ──
    {
        const char* longText = "本文件仅供内部使用，未经许可不得外传";
        const WatermarkTemplate t = driveDialog(WatermarkTemplate{}, true, {}, {}, longText);
        assert(t.name == longText);
        assert(t.folderName() == longText);      // becomes the folder as-is
        assert(t.outputFolder.empty());          // ...bounded only when written
    }

    // ── Variant suffix is stored and shown ───────────────────────────────
    {
        const WatermarkTemplate t =
            driveDialog(WatermarkTemplate{}, true, {"机密"}, {"机密"}, "机密", "红色");
        assert(t.name == "机密 (2)");
        assert(t.variantSuffix == "红色");
        assert(t.outputFolder == "机密");
        assert(t.displayLabel() == "机密 (2)·红色");
    }

    // ── An explicitly picked folder wins over the auto-merge ─────────────
    {
        const WatermarkTemplate t =
            driveDialog(WatermarkTemplate{}, true, {"机密"}, {"机密", "对外"}, "机密", nullptr, "对外");
        assert(t.name == "机密 (2)");
        assert(t.outputFolder == "对外");
    }

    // ── Editing in place: the name follows the text, so an old "机密-红色"
    //    whose text is just "机密" becomes "机密" again (no rename field). ──
    {
        const WatermarkTemplate cur = existingTemplate("机密-红色", "机密", "红色");
        // MainWindow excludes the edited template's own name from `taken`.
        const WatermarkTemplate t = driveDialog(cur, false, {"其它模板"}, {}, "机密", "红色");
        assert(t.name == "机密");
        assert(t.outputFolder.empty());
        assert(t.variantSuffix == "红色");
        assert(t.folderName() == "机密");
    }

    // ── Renaming onto an occupied name also becomes a sibling + merge ────
    {
        const WatermarkTemplate cur = existingTemplate("草稿", "机密");
        // The derived name "机密" collides with another template: the result is
        // de-duplicated and the output folder is pinned to the original name.
        const WatermarkTemplate t =
            driveDialog(cur, false, {"机密"}, {"机密"}, "机密", nullptr, nullptr);
        assert(t.name == "机密 (2)");
        assert(t.outputFolder == "机密");
    }

    std::cout << "[PASS] testTemplateDialogNaming\n";
}

// "样式更改后自动保存配置": picking/tweaking 样式1/样式2 persists the preset to
// the store's JSON file, without pressing 保存.
void testTemplateDialogStylePresets() {
    std::cout << "[RUN] testTemplateDialogStylePresets\n";

    const QString path = QDir::tempPath() + "/pdfmark_dialog_style_test.json";
    QFile::remove(path);

    double editedRotation = 0.0;
    {
        WatermarkTemplateStore store(path);
        assert(!store.load());   // no file yet => built-in presets

        TemplateEditDialog dlg(WatermarkTemplate{}, true, {}, {}, &store, nullptr);
        auto* styleCombo = dlg.findChild<QComboBox*>(kStyleCombo);
        auto* rotation = dlg.findChild<QDoubleSpinBox*>();
        assert(styleCombo && rotation);
        assert(styleCombo->count() == kStylePresetCount);
        assert(styleCombo->currentIndex() == 0);
        // New template starts from the 样式1 values.
        assert(nearly(rotation->value(), store.stylePreset(0).rotationDegrees));

        // Switching to 样式2 loads ITS parameters into the controls...
        styleCombo->setCurrentIndex(1);
        assert(nearly(rotation->value(), store.stylePreset(1).rotationDegrees));
        // ...without marking anything dirty (a pure load must not write back).
        assert(!QFile::exists(path));

        // Editing a style widget updates 样式2 and auto-saves; the flush makes
        // the test independent of the debounce timer.
        rotation->setValue(rotation->value() - 7.0);
        editedRotation = rotation->value();
        dlg.flushPendingStyleSave();
        assert(QFile::exists(path));
    }

    WatermarkTemplateStore reloaded(path);
    assert(reloaded.load());
    assert(nearly(reloaded.stylePreset(1).rotationDegrees, editedRotation));
    // 样式1 was left alone.
    assert(nearly(reloaded.stylePreset(0).rotationDegrees, builtinStylePresets()[0].rotationDegrees));
    QFile::remove(path);

    std::cout << "[PASS] testTemplateDialogStylePresets\n";
}

} // namespace pdfmark
