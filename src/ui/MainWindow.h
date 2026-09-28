// PDFMark - Main GUI Window.
//
// Watermarks exist ONLY as global templates: a template is a company/usage
// watermark file (name + one style + N text lines). Batches are the
// many-to-many product of the checked PDFs and the checked templates, one
// output PDF per (PDF, template) pair.
#pragma once

#include "common/Common.h"
#include "watermark/WatermarkTemplate.h"
#include "watermark/WatermarkTemplateStore.h"
#include "task/TaskManager.h"
#include "task/WorkloadEstimate.h"
#include <QMainWindow>
#include <QTableWidget>
#include <QProgressBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QListWidget>
#include <QHash>
#include <QShortcut>

namespace pdfmark {

class AutoUpdater;
struct UpdateInfo;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    // Which PDFs a batch covers: the checked ones, or every file in the list.
    enum class BatchScope { Checked, All };

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    // ── File list ───────────────────────────────────────────────────────
    void onAddFiles();
    void onAddFolder();
    void onClearFiles();
    void onRemoveSelectedFile();
    void onSelectAllFiles();
    void onSelectNoFiles();
    void onFileSelectionChanged();

    // ── Output / execution ──────────────────────────────────────────────
    void onSelectOutputDir();
    void onOpenOutputFolder();
    void onStartAllClicked();
    void onStartCheckedClicked();
    void onCancelClicked();

    // ── Templates ───────────────────────────────────────────────────────
    void onNewTemplate();
    void onEditTemplate();
    void onDeleteTemplate();
    void onPreviewTemplate();
    void onSelectAllTemplates();
    void onSelectNoneTemplates();
    void onTemplateItemChanged();

    // ── TaskManager signals ─────────────────────────────────────────────
    void onFileStarted(const QString& fileName, int index, int total);
    void onFileProgress(int completed, int total);
    void onFileFinished(const FileResult& result);
    void onAllFinished(const std::vector<FileResult>& results);
    void onCancelled();
    void onPasswordRequired(const QString& filePath);

    // ── Auto-update ─────────────────────────────────────────────────────
    void onCheckForUpdates();
    void onSilentUpdateAvailable(const UpdateInfo& info);

private:
    void setupUi();
    void setupConnections();
    void updateUiState(bool running);

    // ── Template list ───────────────────────────────────────────────────
    void refreshTemplateList(const QString& select = QString());
    WatermarkTemplate selectedTemplate() const;               // invalid when none
    std::vector<WatermarkTemplate> checkedTemplates() const;  // checked & usable
    int checkedTemplateCount() const;
    void setTemplateChecked(int row, bool on);
    bool isTemplateCheckedAt(int row) const;
    // Status-bar text after saving/updating a template (mentions a merged output
    // folder when the template writes into another template's folder).
    QString templateSavedMessage(const WatermarkTemplate& tpl, const QString& verb) const;

    // ── Batch ───────────────────────────────────────────────────────────
    std::vector<TaskManager::FileSubtask> buildSubtasks(BatchScope scope);
    void runBatch(BatchScope scope);
    // Single confirmation gate: shows the composition (PDFs x templates =
    // outputs), the output directory and, for tight memory, the estimate.
    // Returns false when the user cancels.
    bool confirmBatchRun(const std::vector<TaskManager::FileSubtask>& subtasks,
                         BatchScope scope);
    WorkloadEstimate estimateCurrentWorkload(
        const std::vector<TaskManager::FileSubtask>& subtasks) const;

    // Dynamic button copy, e.g. "生成（1 个 PDF × 5 个模板 = 5 个输出）".
    void refreshActionLabels();
    // Right-hand summary: how many templates are checked and the output count.
    void refreshSummary();

    // Rebuild the filename -> row lookup used by the progress handlers
    // (keeps them O(1) instead of scanning the whole table per signal).
    void rebuildFileIndex();

    // ── File list access ─────────────────────────────────────────────────
    // The table only *displays* a checkbox and a file name; the path, page
    // count and status live in hidden data roles on the name item so changing
    // the visible columns can never break the logic again.
    void appendFileRow(const QString& filePath);
    QString pathAt(int row) const;
    void setPathAt(int row, const QString& path);
    int pagesAt(int row) const;
    void setPagesAt(int row, int pages);
    void setStatus(int row, const QString& status, const QString& tooltip = QString());
    bool isCheckedAt(int row) const;
    void setCheckedAt(int row, bool on);
    std::vector<QString> checkedPaths() const;
    int checkedCount() const;

    // ── Widgets: file list ──────────────────────────────────────────────
    QTableWidget* fileTable_ = nullptr;
    QPushButton* selectAllBtn_ = nullptr;
    QPushButton* selectNoneBtn_ = nullptr;
    QHash<QString, int> fileNameToRow_;  // basename -> file table row
    QString currentSelectedFile_;        // row-highlight target (edit focus)

    // ── Widgets: templates ──────────────────────────────────────────────
    QListWidget* templateList_ = nullptr;
    QPushButton* newTemplateBtn_ = nullptr;
    QPushButton* editTemplateBtn_ = nullptr;
    QPushButton* deleteTemplateBtn_ = nullptr;
    QPushButton* previewTemplateBtn_ = nullptr;
    QPushButton* selectAllTemplatesBtn_ = nullptr;
    QPushButton* selectNoneTemplatesBtn_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    WatermarkTemplateStore templateStore_;

    // ── Widgets: output / status ────────────────────────────────────────
    QLineEdit* outputDirEdit_ = nullptr;
    QPushButton* openFolderBtn_ = nullptr;
    QString lastOutputDir_;
    QProgressBar* totalProgressBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* startCheckedBtn_ = nullptr;
    QPushButton* startAllBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;

    TaskManager taskManager_;
    std::unordered_map<std::string, std::string> knownPasswords_;

    bool suppressBatchConfirm_ = false;  // "don't confirm again this session"
    AutoUpdater* autoUpdater_ = nullptr;
};

} // namespace pdfmark
