// PDFMark - JSON persistence for reusable watermark templates.
#pragma once

#include "watermark/WatermarkTemplate.h"
#include <QString>
#include <QStringList>
#include <array>
#include <vector>

namespace pdfmark {

// Stores named watermark templates in a JSON file under the per-user app data
// directory (never the Windows registry, keeping the "green / portable" green
// promise). All operations are plain filesystem I/O and can be unit tested
// without any widget.
class WatermarkTemplateStore {
public:
    // Empty filePath => defaultFilePath().
    explicit WatermarkTemplateStore(QString filePath = QString());

    // Default location: <AppDataLocation>/watermark_templates.json
    static QString defaultFilePath();

    QString filePath() const { return filePath_; }
    void setFilePath(const QString& path) { filePath_ = path; }

    const std::vector<WatermarkTemplate>& templates() const { return templates_; }

    // ── Global style presets (样式1 / 样式2) ────────────────────────────
    // Index is bounded: out-of-range reads return 样式1, writes are rejected.
    const WatermarkConfig& stylePreset(int index) const;
    // Persist one preset and write the file immediately (the editor calls this
    // on every style edit, so it is the "auto-save" end point).
    bool saveStylePreset(int index, const WatermarkConfig& cfg);

    // All template names (names are the store's uniqueness key).
    QStringList names() const;

    // Effective output folders (WatermarkTemplate::folderName()) of every
    // template, de-duplicated. Feeds the "merge into an existing folder" picker.
    QStringList folderNames() const;

    // Load from disk. Returns false on missing/corrupt file (templates are then
    // cleared and a warning is logged) — never throws.
    bool load();

    // Write all templates to disk (creates the parent directory when needed).
    bool save() const;

    // Insert a new template or replace the one with the same name.
    // Returns true when an existing template was replaced.
    bool addOrReplace(const WatermarkTemplate& tpl);

    bool remove(const QString& name);
    bool rename(const QString& oldName, const QString& newName);

    // nullptr when not found.
    const WatermarkTemplate* find(const QString& name) const;

private:
    QString filePath_;
    std::vector<WatermarkTemplate> templates_;
    std::array<WatermarkConfig, kStylePresetCount> stylePresets_{builtinStylePresets()};
};

} // namespace pdfmark
