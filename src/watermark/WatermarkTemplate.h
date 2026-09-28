// PDFMark - Reusable watermark template: multi-line text + one shared style.
#pragma once

#include "watermark/WatermarkConfig.h"
#include <algorithm>
#include <string>
#include <vector>

namespace pdfmark {

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
// the user: the first non-empty line, truncated to 6 characters with "...".
// Chinese characters count as one unit, so counting walks UTF-8 code points.
inline constexpr int kMaxTemplateNameChars = 6;

// Trims leading/trailing spaces, tabs and newlines. Returns an empty string when
// the input is blank, so callers can use it for "was anything actually typed?".
inline std::string trimCopy(const std::string& raw) {
    const size_t b = raw.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const size_t e = raw.find_last_not_of(" \t\r\n");
    return raw.substr(b, e - b + 1);
}

inline std::string templateNameFromConfigs(const std::vector<WatermarkConfig>& configs) {
    std::string text;
    for (const auto& cfg : configs) {
        if (cfg.text.empty()) continue;
        text = trimCopy(cfg.text);
        if (!text.empty()) break;
    }
    if (text.empty()) return std::string();

    size_t chars = 0;
    size_t cut = text.size();
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t len = (c < 0x80) ? 1u : (c >= 0xF0 ? 4u : (c >= 0xE0 ? 3u : 2u));
        if (i + len > text.size()) len = 1;
        if (chars == static_cast<size_t>(kMaxTemplateNameChars)) {
            cut = i;
            break;
        }
        ++chars;
        i += len;
    }
    if (cut < text.size()) return text.substr(0, cut) + "...";
    return text;
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
