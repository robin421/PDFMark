// PDFMark - two-line rendering for the watermark template list.
//
// The default one-line item packed everything into one string
// ("机密 (2)   ·   1 条 · 26pt · -45° · 22%   ·   变体 斜体 → 机密/"), which forced a
// horizontal scrollbar as soon as a template name got long. This delegate draws a
// name line plus a muted meta line, a folder chip on the right, and its own check
// indicator, so rows stay readable at any name length.
#pragma once

#include <QStyledItemDelegate>

namespace pdfmark {

class TemplateListDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    // Extra item data roles filled by MainWindow::refreshTemplateList().
    enum Roles {
        MetaRole = Qt::UserRole + 10,     // "1 行 · 24pt · 12% · -35° · 变体 红色"
        FolderRole = Qt::UserRole + 11,   // "机密/（合并）"
    };

    explicit TemplateListDelegate(QObject* parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option, const QModelIndex& index) override;

private:
    // Hit area of the check indicator (shared by paint() and editorEvent()).
    static QRect indicatorRect(const QRect& itemRect);
};

} // namespace pdfmark
