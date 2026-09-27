// PDFMark - File list table selection helpers.
#pragma once

#include <QTableWidget>
#include <QItemSelectionModel>
#include <QList>
#include <QModelIndex>
#include <algorithm>
#include <functional>

namespace pdfmark {

// Returns the row indices of every selected row in `table`, de-duplicated and
// sorted in descending order so callers can safely removeRow() them
// bottom-to-top without index shifting.
//
// IMPORTANT: with QAbstractItemView::SelectRows, QTableWidget::selectedItems()
// returns *every cell* of a selected row (e.g. 4 items in a 4-column table).
// Feeding that list straight into a remove loop makes each duplicate row index
// re-target a freshly shifted row, so a single selected row silently deletes
// several extra files. Ask the selection model for rows (one index per row)
// instead of items.
inline QList<int> selectedRowsDescending(const QTableWidget* table) {
    QList<int> rows;
    if (!table || !table->selectionModel()) return rows;

    const QModelIndexList indexes = table->selectionModel()->selectedRows();
    rows.reserve(indexes.size());
    for (const QModelIndex& index : indexes) {
        if (index.isValid()) rows.append(index.row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

} // namespace pdfmark
