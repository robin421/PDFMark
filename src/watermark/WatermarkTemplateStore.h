// PDFMark - JSON persistence for reusable watermark templates.
#pragma once

#include "watermark/WatermarkTemplate.h"
#include <QString>
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
};

} // namespace pdfmark
