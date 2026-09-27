// PDFMark - Regression test for "删除选中文件" over-deletion.
//
// Bug: MainWindow::onRemoveSelectedFile() built its delete list from
// QTableWidget::selectedItems(). With QAbstractItemView::SelectRows that list
// contains one item per *cell* (4 per row in the file table), so a single
// selected row produced four duplicate row indices. Removing bottom-to-top
// then deleted 3 unrelated, freshly shifted rows per selection.
#include "ui/FileTableSelection.h"

#include <QApplication>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTableWidgetSelectionRange>
#include <QAbstractItemView>

#include <cassert>
#include <iostream>

namespace pdfmark {

namespace {

void fillTable(QTableWidget& table, int rows) {
    table.setRowCount(rows);
    for (int r = 0; r < rows; ++r) {
        table.setItem(r, 0, new QTableWidgetItem(QString("file%1.pdf").arg(r)));
        table.setItem(r, 1, new QTableWidgetItem(QString::number(r + 1)));
        table.setItem(r, 2, new QTableWidgetItem("等待处理"));
        table.setItem(r, 3, new QTableWidgetItem(QString("/tmp/file%1.pdf").arg(r)));
    }
}

// Mirrors the deletion loop in MainWindow::onRemoveSelectedFile().
void removeRows(QTableWidget& table, const QList<int>& rowsDescending) {
    for (int row : rowsDescending) {
        if (row < 0 || row >= table.rowCount()) continue;
        if (!table.item(row, 3)) continue;
        table.removeRow(row);
    }
}

QList<QString> firstColumn(const QTableWidget& table) {
    QList<QString> names;
    for (int r = 0; r < table.rowCount(); ++r) {
        names.append(table.item(r, 0)->text());
    }
    return names;
}

} // namespace

void testFileRemoval() {
    std::cout << "[RUN] testFileRemoval\n";

    QTableWidget table(0, 4);
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.setSelectionMode(QAbstractItemView::ExtendedSelection);
    fillTable(table, 12);

    // Guard the assumption the bug relied on: SelectRows selection yields one
    // selected item per *cell*, i.e. 4 items for a single selected row.
    table.selectRow(0);
    assert(table.selectedItems().size() == 4);
    assert(selectedRowsDescending(&table) == QList<int>{0});

    // Negative control: the old naive path (raw selectedItems() rows, no
    // de-duplication) deletes 4 rows for a single selection — this is the bug.
    {
        QTableWidget naive(0, 4);
        naive.setSelectionBehavior(QAbstractItemView::SelectRows);
        fillTable(naive, 12);
        naive.selectRow(0);
        QList<int> raw;
        for (auto* item : naive.selectedItems()) raw.append(naive.row(item));
        std::sort(raw.begin(), raw.end(), std::greater<int>());
        for (int r : raw) {
            if (r < 0 || r >= naive.rowCount()) continue;
            naive.removeRow(r);
        }
        assert(naive.rowCount() == 8); // 4 deleted instead of 1
    }

    // 1) Selecting a single row must delete exactly that row.
    removeRows(table, selectedRowsDescending(&table));
    assert(table.rowCount() == 11);
    assert(firstColumn(table).first() == "file1.pdf");

    // 2) Non-contiguous multi-selection must delete exactly those rows.
    fillTable(table, 12);
    table.clearSelection();
    table.setRangeSelected(QTableWidgetSelectionRange(0, 0, 0, 3), true);
    table.setRangeSelected(QTableWidgetSelectionRange(8, 0, 8, 3), true);
    QList<int> rows = selectedRowsDescending(&table);
    assert((rows == QList<int>{8, 0})); // de-duplicated, descending
    removeRows(table, rows);
    assert(table.rowCount() == 10);
    assert((firstColumn(table) == QList<QString>{
        "file1.pdf", "file2.pdf", "file3.pdf", "file4.pdf", "file5.pdf",
        "file6.pdf", "file7.pdf", "file9.pdf", "file10.pdf", "file11.pdf"}));

    // 3) Contiguous (Shift-style) multi-selection must delete only that range.
    fillTable(table, 12);
    table.clearSelection();
    table.setRangeSelected(QTableWidgetSelectionRange(2, 0, 5, 3), true);
    rows = selectedRowsDescending(&table);
    assert((rows == QList<int>{5, 4, 3, 2}));
    removeRows(table, rows);
    assert(table.rowCount() == 8);
    assert((firstColumn(table) == QList<QString>{
        "file0.pdf", "file1.pdf", "file6.pdf", "file7.pdf",
        "file8.pdf", "file9.pdf", "file10.pdf", "file11.pdf"}));

    // 4) Empty selection is a no-op.
    fillTable(table, 3);
    table.clearSelection();
    assert(selectedRowsDescending(&table).isEmpty());
    removeRows(table, selectedRowsDescending(&table));
    assert(table.rowCount() == 3);

    std::cout << "[PASS] testFileRemoval\n";
}

} // namespace pdfmark
