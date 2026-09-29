// PDFMark - application theme (stylesheets + palette).
#include "ui/Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>

namespace pdfmark::theme {

QString chip(const QString& label, const QString& value) {
    return QStringLiteral("<span style='color:%1'>%2</span>"
                          "&nbsp;<span style='color:%3;font-weight:600'>%4</span>")
        .arg(QLatin1String(kTextSecondary), label.toHtmlEscaped(),
             QLatin1String(kText), value.toHtmlEscaped());
}

QString muted(const QString& text) {
    return QStringLiteral("<span style='color:%1'>%2</span>")
        .arg(QLatin1String(kTextMuted), text.toHtmlEscaped());
}

QString separator() {
    return QStringLiteral("<span style='color:%1'>&nbsp;·&nbsp;</span>").arg(QLatin1String(kBorder));
}

QString styleSheet() {
    // NOTE: QSS has no variables, so the palette above is repeated here. Keep both
    // in sync when changing a colour.
    return QStringLiteral(R"(
/* ── base ─────────────────────────────────────────────────────────────── */
QWidget { color: %TEXT%; }
QMainWindow, QDialog { background: %PAGE%; }
QToolTip {
    background: %CARD%; color: %TEXT%;
    border: 1px solid %BORDER%; padding: 6px 8px;
}

/* ── cards & sections ─────────────────────────────────────────────────── */
QFrame#card {
    background: %CARD%;
    border: 1px solid %BORDER%;
    border-radius: 10px;
}
QLabel#sectionTitle { font-size: 13px; font-weight: 600; color: %TEXT%; }
QLabel#hint, QLabel#sectionSubtitle { font-size: 12px; color: %MUTED%; }
QLabel#chip { font-size: 12px; color: %SECONDARY%; }
QLabel#emptyState { font-size: 13px; color: %MUTED%; }
QLabel#emptyTitle { font-size: 14px; color: %SECONDARY%; }

/* ── buttons ──────────────────────────────────────────────────────────── */
QPushButton {
    background: %CARD%;
    color: %TEXT%;
    border: 1px solid #d8dde4;
    border-radius: 6px;
    padding: 5px 12px;
    font-size: 13px;
}
QPushButton:hover { background: #f4f7fb; border-color: #c6ccd6; }
QPushButton:pressed { background: #eaeff6; }
QPushButton:disabled { color: #a9b0ba; background: #f6f7f9; border-color: #e8ebef; }

QPushButton#primary {
    background: %ACCENT%; border-color: %ACCENT%; color: #ffffff; font-weight: 600;
    padding: 6px 18px;
}
QPushButton#primary:hover { background: #0a5fb4; border-color: #0a5fb4; }
QPushButton#primary:pressed { background: #08508f; border-color: #08508f; }
QPushButton#primary:disabled { background: #b8cbe0; border-color: #b8cbe0; color: #ffffff; }

QPushButton#ghost { background: transparent; border-color: transparent; color: %SECONDARY%; }
QPushButton#ghost:hover { background: #eef2f7; color: %TEXT%; }
QPushButton#ghost:pressed { background: #e4eaf2; }
QPushButton#ghost:disabled { background: transparent; color: #b3bac4; }

QPushButton#danger { background: transparent; border-color: transparent; color: %DANGER%; }
QPushButton#danger:hover { background: #fdecea; }
QPushButton#danger:pressed { background: #fbdcd8; }
QPushButton#danger:disabled { background: transparent; color: #d9b0ac; }

/* ── inputs ───────────────────────────────────────────────────────────── */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QFontComboBox {
    background: %CARD%;
    border: 1px solid #d8dde4;
    border-radius: 6px;
    padding: 5px 8px;
    selection-background-color: #cfe2f9;
    selection-color: %TEXT%;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QComboBox:focus, QFontComboBox:focus { border-color: %ACCENT%; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled,
QComboBox:disabled { background: #f6f7f9; color: #a9b0ba; }

/* ── lists & tables ───────────────────────────────────────────────────── */
QTableWidget, QListWidget { background: %CARD%; border: none; outline: none; }
QTableWidget::item { border: none; padding: 0 8px; }
QTableWidget::item:selected { background: #e8f1fb; color: %TEXT%; }
QTableWidget::item:hover { background: #f5f8fc; }
QHeaderView::section {
    background: %CARD%; color: %MUTED%; font-size: 12px; font-weight: normal;
    border: none; border-bottom: 1px solid %BORDER%; padding: 6px 8px;
}
QTableCornerButton::section { background: %CARD%; border: none; }

/* ── scrollbars (thin, quiet) ─────────────────────────────────────────── */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #cbd1d9; border-radius: 3px; min-height: 32px; }
QScrollBar::handle:vertical:hover { background: #b3bbc6; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #cbd1d9; border-radius: 3px; min-width: 32px; }
QScrollBar::handle:horizontal:hover { background: #b3bbc6; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ── progress ─────────────────────────────────────────────────────────── */
QProgressBar {
    background: #eceff3; border: none; border-radius: 7px;
    min-height: 14px; max-height: 14px;
    color: %SECONDARY%; font-size: 11px;
}
QProgressBar::chunk { background: %ACCENT%; border-radius: 7px; }

/* ── splitter / menus ─────────────────────────────────────────────────── */
QSplitter::handle { background: transparent; }
QSplitter::handle:hover { background: %BORDER%; }
QMenuBar { background: %PAGE%; }
QMenuBar::item { padding: 4px 10px; background: transparent; border-radius: 4px; }
QMenuBar::item:selected { background: %BORDER%; }
QMenu { background: %CARD%; border: 1px solid %BORDER%; padding: 4px; }
QMenu::item { padding: 6px 22px 6px 12px; border-radius: 4px; }
QMenu::item:selected { background: #eef2f7; }
QMenu::separator { height: 1px; background: %BORDER%; margin: 4px 8px; }

/* A row's × remove button: no padding, so the glyph fits the 26px box. */
QPushButton#rowRemove {
    background: transparent; border: none; color: %DANGER%;
    padding: 0; font-size: 15px; font-weight: 600;
}
QPushButton#rowRemove:hover { background: #fdecea; border-radius: 4px; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }

/* ── dialogs ──────────────────────────────────────────────────────────── */
QGroupBox QLabel { color: %SECONDARY%; }
QLabel#templateOutputHint { font-size: 12px; color: %MUTED%; }
QLabel#previewFrame {
    background: %CARD%; border: 1px solid %BORDER%; border-radius: 8px;
}

/* ── group boxes become cards ─────────────────────────────────────────── */
QGroupBox {
    background: %CARD%;
    border: 1px solid %BORDER%;
    border-radius: 10px;
    margin-top: 12px;
    padding: 14px 12px 12px 12px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 12px; padding: 0 4px;
    color: %TEXT%; font-weight: 600;
}

/* ── slider (颜色深浅) ─────────────────────────────────────────────────── */
QSlider::groove:horizontal { height: 4px; background: %BORDER%; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %ACCENT%; border-radius: 2px; }
QSlider::handle:horizontal {
    background: %CARD%; border: 1px solid %ACCENT%;
    width: 14px; height: 14px; margin: -6px 0; border-radius: 8px;
}
QSlider::handle:horizontal:hover { background: #eaf2fd; }
)")
        .replace("%PAGE%", QLatin1String(kPageBg))
        .replace("%CARD%", QLatin1String(kCardBg))
        .replace("%BORDER%", QLatin1String(kBorder))
        .replace("%TEXT%", QLatin1String(kText))
        .replace("%SECONDARY%", QLatin1String(kTextSecondary))
        .replace("%MUTED%", QLatin1String(kTextMuted))
        .replace("%ACCENT%", QLatin1String(kAccent))
        .replace("%DANGER%", QLatin1String(kDanger));
}

void apply(QApplication& app) {
    // Keep the platform font family, normalise the size so macOS/Windows match.
    QFont font = app.font();
    font.setPixelSize(13);
    app.setFont(font);

    // Pin the light palette: with the stylesheet above the app should look the
    // same in light and dark system themes, and native indicators (checkboxes)
    // must not end up dark-on-dark.
    QPalette pal = app.palette();
    pal.setColor(QPalette::Window, QColor(kPageBg));
    pal.setColor(QPalette::WindowText, QColor(kText));
    pal.setColor(QPalette::Base, QColor(kCardBg));
    pal.setColor(QPalette::AlternateBase, QColor(kCardBg));
    pal.setColor(QPalette::Text, QColor(kText));
    pal.setColor(QPalette::Button, QColor(kCardBg));
    pal.setColor(QPalette::ButtonText, QColor(kText));
    pal.setColor(QPalette::Highlight, QColor(0xE8, 0xF1, 0xFB));
    pal.setColor(QPalette::HighlightedText, QColor(kText));
    pal.setColor(QPalette::ToolTipBase, QColor(kCardBg));
    pal.setColor(QPalette::ToolTipText, QColor(kText));
    app.setPalette(pal);

    app.setStyleSheet(styleSheet());
}

} // namespace pdfmark::theme
