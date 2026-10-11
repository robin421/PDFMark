// PDFMark - Reusable watermark template: multi-line text + one shared style.
#pragma once

#include "watermark/WatermarkConfig.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace pdfmark {

// The app ships exactly two GLOBAL style presets, "样式1" and "样式2". They are
// the only place a watermark style is authored: the editor lets the user pick
// one and tweak it, and every change is written to disk immediately. A template
// stores a plain COPY of the resulting style fields, so later preset edits never
// rewrite an already-saved template (see README "样式预设").
inline constexpr int kStylePresetCount = 2;

// "样式1" for index 0, "样式2" for index 1.
inline std::string stylePresetName(int index) {
    return "样式" + std::to_string(index + 1);
}

// Built-in defaults, used on first run and whenever the store has no "styles"
// entry yet (v1/v2 files). 样式1 keeps the historical look, 样式2 is deliberately
// different so the two choices are tellable apart out of the box.
inline std::array<WatermarkConfig, kStylePresetCount> builtinStylePresets() {
    WatermarkConfig light;      // 样式1: existing defaults, nothing looks new
    light.text.clear();
    light.opacity = 0.12;
    light.colorHex = "#BEBEBE";
    light.rotationDegrees = -35.0;
    light.fontFamily = "Arial";
    light.fontBold = true;
    light.fontItalic = false;

    WatermarkConfig deep;       // 样式2: deeper, taller slant, serif + italic
    deep.text.clear();
    deep.opacity = 0.30;
    deep.colorHex = "#808080";
    deep.rotationDegrees = -45.0;
    deep.fontFamily = "Times New Roman";
    deep.fontBold = false;
    deep.fontItalic = true;

    return {light, deep};
}

// Trims leading/trailing spaces, tabs and newlines. Returns an empty string when
// the input is blank, so callers can use it for "was anything actually typed?".
inline std::string trimCopy(const std::string& raw) {
    const size_t b = raw.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = raw.find_last_not_of(" \t\r\n");
    return raw.substr(b, e - b + 1);
}

// A named, reusable bundle of watermark settings. Every entry carries its own
// text plus the shared style fields (font, size, rotation, opacity, DPI,
// JPEG quality) and the per-entry `selected` flag used by "生成所选".
struct WatermarkTemplate {
    std::string name;
    std::vector<WatermarkConfig> watermarks;

    // Output naming / grouping (see README "输出命名").
    //
    // `variantSuffix` is appended to the output file name, so several STYLES of
    // the SAME watermark text stay tellable apart inside one folder
    // (e.g. "红色" -> report_红色.pdf). Empty means "no suffix"; genuine
    // collisions then fall back to the _2 / _3 counter in TaskManager.
    //
    // `outputFolder` overrides the output sub-directory. Empty keeps the
    // historical rule "sub-directory == template name", so templates that were
    // created before this field existed keep writing to the same place.
    std::string variantSuffix;
    std::string outputFolder;

    // Which GLOBAL style preset (stylePresetName(i)) this template was saved
    // with. Descriptive only: the real style parameters live in `watermarks[]`,
    // so editing that preset afterwards never rewrites this template. Empty for
    // files written before schema v3.
    std::string styleName;

    // Effective output sub-directory: the explicit override when set, the
    // template name otherwise. Single source of truth for UI + pipeline.
    // (Callers sanitize it with sanitizeFilename() right before touching disk.)
    const std::string& folderName() const {
        return outputFolder.empty() ? name : outputFolder;
    }

    // A template is usable when it has a name and at least one non-empty text.
    bool isValid() const {
        if (name.empty()) return false;
        for (const auto& wm : watermarks) {
            if (!wm.text.empty()) return true;
        }
        return false;
    }

    // Style preview: first non-empty entry, or a default-constructed config.
    WatermarkConfig style() const {
        for (const auto& wm : watermarks) {
            if (!wm.text.empty()) return wm;
        }
        return WatermarkConfig{};
    }

    // Human-readable label for result rows / logs / progress text. The variant
    // suffix is appended when set so several styles of the same text stay
    // distinguishable ("机密·红色"). Identical to `name` when unset.
    std::string displayLabel() const {
        return variantSuffix.empty() ? name : name + "·" + variantSuffix;
    }
};

// Template names are derived from the watermark text instead of being typed by
// the user: the first non-empty line, used VERBATIM — no length limit, so a
// longer notice ("本文件仅供内部使用，未经许可不得外传") stays readable in the
// template list instead of turning into "本文件仅供内...".
//
// The name is also the default output folder, and a path component cannot be
// arbitrarily long, so it is bounded where it actually reaches the disk (see
// boundPathComponent()): truncating it here would lose the user's text in the UI
// as well, which is what this function used to do.
inline std::string templateNameFromConfigs(const std::vector<WatermarkConfig>& configs) {
    for (const auto& cfg : configs) {
        if (cfg.text.empty()) continue;
        std::string text = trimCopy(cfg.text);
        // Only the first LINE becomes the name (a row cannot contain a newline in
        // the UI, but a hand-edited template file could).
        const size_t newline = text.find_first_of("\r\n");
        if (newline != std::string::npos) text = trimCopy(text.substr(0, newline));
        if (!text.empty()) return text;
    }
    return std::string();
}

// Trims every line and derives a name when none was given.
// Returns false when the template has no usable (non-empty) line.
inline bool normalizeTemplate(WatermarkTemplate& tpl) {
    std::vector<WatermarkConfig> kept;
    kept.reserve(tpl.watermarks.size());
    for (auto& wm : tpl.watermarks) {
        wm.text = trimCopy(wm.text);
        if (wm.text.empty()) continue;
        kept.push_back(wm);
    }
    tpl.watermarks = std::move(kept);
    if (tpl.watermarks.empty()) return false;

    const std::string name = trimCopy(tpl.name);
    tpl.name = name.empty() ? templateNameFromConfigs(tpl.watermarks) : name;

    // Naming fields are optional; a blank one means "not set" (fall back to the
    // template name / no file-name suffix) rather than a folder called "   ".
    tpl.variantSuffix = trimCopy(tpl.variantSuffix);
    tpl.outputFolder = trimCopy(tpl.outputFolder);
    return true;
}

// First free name of the form "base", "base (2)", "base (3)", ...
// A blank base degrades to "watermark" so the result is never empty.
inline std::string uniqueNameAmong(const std::string& base, const std::vector<std::string>& taken) {
    std::string stem = trimCopy(base);
    if (stem.empty()) stem = "watermark";
    auto isTaken = [&taken](const std::string& candidate) {
        return std::find(taken.begin(), taken.end(), candidate) != taken.end();
    };
    if (!isTaken(stem)) return stem;
    for (int i = 2; i < 100000; ++i) {
        const std::string candidate = stem + " (" + std::to_string(i) + ")";
        if (!isTaken(candidate)) return candidate;
    }
    return stem;   // unreachable in practice
}

// The naming outcome for one template.
//
// `outputFolder` empty means "use the template name" (WatermarkTemplate::folderName()
// falls back to `name`), which is what keeps pre-1.5.0 output layouts intact.
struct TemplateNaming {
    std::string name;
    std::string outputFolder;
};

// Resolve what the user typed into what actually gets saved:
//
//  * a blank `typedName` derives the name from the watermark text (first line,
//    6 chars);
//  * a name that is already `taken` becomes a SIBLING ("机密" -> "机密 (2)")
//    instead of overwriting the template that owns the name, and the ORIGINAL
//    name is then pinned as the output folder — that single rule is what makes
//    "same watermark text, different styles, ONE output folder" work;
//  * an explicit `typedFolder` always wins, a blank one stays blank (=> name).
inline TemplateNaming resolveTemplateNaming(const std::string& typedName,
                                            const std::vector<WatermarkConfig>& lines,
                                            const std::vector<std::string>& takenNames,
                                            const std::string& typedFolder) {
    TemplateNaming out;
    const std::string trimmedName = trimCopy(typedName);
    const std::string base = trimmedName.empty() ? templateNameFromConfigs(lines) : trimmedName;

    out.name = uniqueNameAmong(base, takenNames);

    const std::string folder = trimCopy(typedFolder);
    if (!folder.empty()) {
        out.outputFolder = folder;
    } else if (!base.empty() && base != out.name) {
        out.outputFolder = base;
    }
    return out;
}

// Expand a template into the per-file config list used by the batch pipeline.// Entries with empty text are dropped so a template never produces a blank
// watermark.
inline std::vector<WatermarkConfig> templateToConfigs(const WatermarkTemplate& tpl) {
    std::vector<WatermarkConfig> out;
    out.reserve(tpl.watermarks.size());
    for (const auto& wm : tpl.watermarks) {
        if (wm.text.empty()) continue;
        out.push_back(wm);
    }
    return out;
}

} // namespace pdfmark
