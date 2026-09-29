// PDFMark - Interaction test for the template editor's output naming.
//
// The user-facing requirement this guards:
//
//   "水印的文字一样，但是水印的样式不同，输出要到同一个文件夹下"
//
// resolveTemplateNaming() owns that decision and is unit tested in
// WatermarkTemplateTest; this test drives the ACTUAL dialog (offscreen) and
// checks what a user typing the same text twice ends up saving — including the
// rename-to-sibling and the auto-merged output folder, plus the live hint that
// tells the user where the files will land.
#include "ui/TemplateEditDialog.h"

#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QMetaObject>

#include <cassert>
#include <iostream>

namespace pdfmark {

namespace {

const char* kNameEdit = "templateNameEdit";
const char* kVariantEdit = "templateVariantEdit";
const char* kFolderCombo = "templateFolderCombo";
const char* kOutputHint = "templateOutputHint";

// Types the watermark text and returns the accepted result. `name`/`variant`/
// `folder` are only set when non-null, so "leave it as the initial value" and
// "leave it blank" are both expressible.
WatermarkTemplate driveDialog(const WatermarkTemplate& initial,
                              bool creating,
                              const QStringList& taken,
                              const QStringList& folders,
                              const char* text,
                              const char* variant = nullptr,
                              const char* folder = nullptr,
                              const char* name = nullptr) {
    TemplateEditDialog dlg(initial, creating, taken, folders, nullptr);
    auto* nameEdit = dlg.findChild<QLineEdit*>(kNameEdit);
    auto* variantEdit = dlg.findChild<QLineEdit*>(kVariantEdit);
    auto* folderCombo = dlg.findChild<QComboBox*>(kFolderCombo);
    auto* textEdit = dlg.findChild<QLineEdit*>("watermarkTextEdit");
    assert(nameEdit && variantEdit && folderCombo && textEdit);

    textEdit->setText(QString::fromUtf8(text));
    if (name) nameEdit->setText(QString::fromUtf8(name));
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
        assert(t.name == "机密");
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
        TemplateEditDialog dlg(WatermarkTemplate{}, true, {"机密"}, {"机密"}, nullptr);
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

        // A long typed name is kept too, and does not collide with anything.
        const WatermarkTemplate typed =
            driveDialog(WatermarkTemplate{}, true, {}, {}, "机密", nullptr, nullptr, longText);
        assert(typed.name == longText);
        assert(typed.watermarks[0].text == "机密");
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

    // ── Editing in place: no rename, folder stays unset (so it follows the
    //    name later, i.e. the historical behaviour is preserved) ───────────
    {
        const WatermarkTemplate cur = existingTemplate("机密-红色", "机密", "红色");
        // MainWindow excludes the edited template's own name from `taken`.
        const WatermarkTemplate t = driveDialog(cur, false, {"其它模板"}, {}, "机密", "红色");
        assert(t.name == "机密-红色");
        assert(t.outputFolder.empty());
        assert(t.variantSuffix == "红色");
        assert(t.folderName() == "机密-红色");
    }

    // ── Renaming onto an occupied name also becomes a sibling + merge ────
    {
        const WatermarkTemplate cur = existingTemplate("草稿", "机密");
        // The user renames 草稿 to 机密 while another 机密 exists and leaves the
        // output folder alone: de-duplicated name + pinned folder, same as a new
        // template would get.
        const WatermarkTemplate t =
            driveDialog(cur, false, {"机密"}, {"机密"}, "机密", nullptr, nullptr, "机密");
        assert(t.name == "机密 (2)");
        assert(t.outputFolder == "机密");
    }

    std::cout << "[PASS] testTemplateDialogNaming\n";
}

} // namespace pdfmark
