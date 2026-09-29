// PDFMark - one place for the application look (colours, spacing, widget styles).
//
// The UI used to set a dozen inline `setStyleSheet("padding: ...")` calls, which
// is why buttons had four different heights and every panel drew its own border.
// Everything visual now comes from here: widgets only carry an objectName
// ("card" / "primary" / "ghost" / "danger" / "hint" / "chip" / "emptyState" /
// "sectionTitle"), and the stylesheet below decides how those look.
#pragma once

#include <QString>

class QApplication;

namespace pdfmark::theme {

// Palette. Kept in C++ so rich-text labels (which QSS cannot reach into) can use
// exactly the same values as the stylesheet.
inline constexpr const char* kPageBg = "#f4f6f8";
inline constexpr const char* kCardBg = "#ffffff";
inline constexpr const char* kBorder = "#e4e7ec";
inline constexpr const char* kText = "#1f2733";
inline constexpr const char* kTextSecondary = "#5b6673";
inline constexpr const char* kTextMuted = "#8b939e";
inline constexpr const char* kAccent = "#0b6bcb";
inline constexpr const char* kDanger = "#c9382f";

// Applies the base font size and the stylesheet to the whole application.
void apply(QApplication& app);

// The stylesheet on its own (tests / dialogs can apply it to a preview widget).
QString styleSheet();

// Small rich-text helpers so C++-built labels match the stylesheet.
// `chip("待处理", "3")` -> muted label + emphasized value.
QString chip(const QString& label, const QString& value);
// Muted inline hint text.
QString muted(const QString& text);
// "·" separator with the muted colour.
QString separator();

} // namespace pdfmark::theme
