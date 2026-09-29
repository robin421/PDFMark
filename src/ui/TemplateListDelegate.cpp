// PDFMark - two-line rendering for the watermark template list.
#include "ui/TemplateListDelegate.h"
#include "ui/Theme.h"

#include <QAbstractItemModel>
#include <QEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace pdfmark {

namespace {

constexpr int kRowHeight = 56;
constexpr int kIndicatorSize = 16;
constexpr int kLeftPadding = 14;
constexpr int kTextLeft = 42;
constexpr int kRightPadding = 14;
constexpr int kChipHeight = 20;
constexpr int kChipPadding = 8;

const QColor kText(theme::kText);
const QColor kMuted(theme::kTextMuted);
const QColor kSecondary(theme::kTextSecondary);
const QColor kAccent(theme::kAccent);
const QColor kBorder(theme::kBorder);
const QColor kSelectedBg(0xE8, 0xF1, 0xFB);
const QColor kHoverBg(0xF7, 0xF9, 0xFC);
const QColor kHairline(0xF2, 0xF4, 0xF7);
const QColor kChipBg(0xF1, 0xF4, 0xF8);

} // namespace

TemplateListDelegate::TemplateListDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

QRect TemplateListDelegate::indicatorRect(const QRect& itemRect) {
    return QRect(itemRect.left() + kLeftPadding,
                 itemRect.center().y() - kIndicatorSize / 2,
                 kIndicatorSize, kIndicatorSize);
}

QSize TemplateListDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const {
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(0, kRowHeight);
}

void TemplateListDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const QRect r = option.rect;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    // ── background ────────────────────────────────────────────────────────
    painter->fillRect(r, selected ? kSelectedBg : (hovered ? kHoverBg : QColor(theme::kCardBg)));
    const bool last = index.model() && index.row() == index.model()->rowCount() - 1;
    if (!last) {
        painter->setPen(QPen(kHairline, 1));
        painter->drawLine(r.left() + kLeftPadding, r.bottom(),
                          r.right() - kRightPadding, r.bottom());
    }

    // ── check indicator ───────────────────────────────────────────────────
    const QRect box = indicatorRect(r);
    const Qt::CheckState state =
        static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
    if (state == Qt::Checked) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(kAccent);
        painter->drawRoundedRect(box, 4, 4);
        QPainterPath check;
        check.moveTo(box.left() + 4.0, box.center().y() + 0.5);
        check.lineTo(box.left() + 6.8, box.bottom() - 4.5);
        check.lineTo(box.right() - 3.5, box.top() + 5.0);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(check);
    } else {
        painter->setPen(QPen(QColor(0xC9, 0xCE, 0xD6), 1));
        painter->setBrush(QColor(theme::kCardBg));
        painter->drawRoundedRect(box.adjusted(0, 0, -1, -1), 4, 4);
    }

    // ── right-aligned folder chip ─────────────────────────────────────────
    const QString folder = index.data(FolderRole).toString();
    int chipLeft = r.right() - kRightPadding;
    if (!folder.isEmpty()) {
        QFont chipFont = option.font;
        chipFont.setPixelSize(12);
        const QFontMetrics chipMetrics(chipFont);
        // Never let the folder chip eat the name: at most 45% of the row.
        const int chipBudget = static_cast<int>((r.width() - kTextLeft) * 0.45);
        const int textWidth = std::min(chipMetrics.horizontalAdvance(folder),
                                       std::max(40, chipBudget - kChipPadding * 2));
        const QString chipText = chipMetrics.elidedText(
            folder, Qt::ElideRight, std::max(40, chipBudget - kChipPadding * 2));
        const int chipWidth = textWidth + kChipPadding * 2;
        const QRect chip(chipLeft - chipWidth, r.center().y() - kChipHeight / 2,
                         chipWidth, kChipHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(kChipBg);
        painter->drawRoundedRect(chip, 5, 5);
        painter->setFont(chipFont);
        painter->setPen(kSecondary);
        painter->drawText(chip, Qt::AlignCenter, chipText);
        chipLeft = chip.left() - 12;   // keep the name away from the chip
    }

    // ── name (line 1) ─────────────────────────────────────────────────────
    const QString name = index.data(Qt::DisplayRole).toString();
    QFont nameFont = option.font;
    nameFont.setPixelSize(13);
    nameFont.setWeight(QFont::DemiBold);
    const QFontMetrics nameMetrics(nameFont);
    const int nameWidth = std::max(20, chipLeft - kTextLeft);
    const QRect nameRect(kTextLeft, r.top() + 9, nameWidth, 18);
    painter->setFont(nameFont);
    painter->setPen(kText);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      nameMetrics.elidedText(name, Qt::ElideRight, nameWidth));

    // ── meta (line 2) ─────────────────────────────────────────────────────
    const QString meta = index.data(MetaRole).toString();
    QFont metaFont = option.font;
    metaFont.setPixelSize(12);
    const QFontMetrics metaMetrics(metaFont);
    const QRect metaRect(kTextLeft, r.top() + 29, r.right() - kRightPadding - kTextLeft, 16);
    painter->setFont(metaFont);
    painter->setPen(kMuted);
    if (!meta.isEmpty()) {
        painter->drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter,
                          metaMetrics.elidedText(meta, Qt::ElideRight, metaRect.width()));
    }

    painter->restore();
}

bool TemplateListDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index) {
    const QRect box = indicatorRect(option.rect);
    QPoint pos;
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
        pos = static_cast<QMouseEvent*>(event)->pos();
    } else {
        return false;
    }
    if (!box.contains(pos)) return false;

    if (event->type() == QEvent::MouseButtonPress) {
        // Swallow the press so clicking the box does not also move the selection.
        return true;
    }
    const Qt::CheckState state =
        static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt());
    model->setData(index, state == Qt::Checked ? Qt::Unchecked : Qt::Checked,
                   Qt::CheckStateRole);
    return true;
}

} // namespace pdfmark
