// PDFMark - Main GUI Window.
#pragma once

#include "common/Common.h"
#include "watermark/WatermarkConfig.h"
#include "watermark/WatermarkTemplate.h"
#include "watermark/WatermarkTemplateStore.h"
#include "task/TaskManager.h"
#include "task/WorkloadEstimate.h"
#include <QMainWindow>
#include <QTableWidget>
#include <QScrollArea>
#include <QProgressBar>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QSlider>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QDesktopServices>
#include <QUrl>
#include <QFontComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QHash>
#include <QStackedWidget>
#include <QRadioButton>
#include <QButtonGroup>
#include <QShortcut>

namespace pdfmark {

class WatermarkRow : public QWidget {
    Q_OBJECT
public:
    explicit WatermarkRow(const QString& initialText, int index, QWidget* parent = nullptr);
    ~WatermarkRow() override = default;

    QString text() const;
    void setText(const QString& t);
    bool isSelected() const;
    void setSelected(bool on);
    int index() const { return index_; }

signals:
    void textChanged();
    void selectionChanged();
    void removeRequested(int index);

private:
    QLineEdit* lineEdit_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QCheckBox* checkBox_ = nullptr;
    int index_ = -1;
};

class AutoUpdater;
struct UpdateInfo;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void onAddFiles();
    void onAddFolder();
    void onClearFiles();
    void onRemoveSelectedFile();
    void onSelectOutputDir();
    // Which PDFs a batch covers: the checked ones, or every file in the list.
    enum class BatchScope { Checked, All };

    void onStartAllClicked();
    void onStartCheckedClicked();
    void onSelectAllFiles();
    void onSelectNoFiles();
    void onCancelClicked();
    void onOpenOutputFolder();
    void onFileSelectionChanged();
    // TaskManager signals
    void onFileStarted(const QString& fileName, int index, int total);
    void onFileProgress(int completed, int total);
    void onFileFinished(const FileResult& result);
    void onAllFinished(const std::vector<FileResult>& results);
    void onCancelled();
    void onPasswordRequired(const QString& filePath);

    // Watermark row management
    void addWatermarkRow();
    void removeWatermarkRow(int index);
    void onWatermarkTextChanged();
    void onWatermarkSelectionChanged();
    void onPreviewWatermark();

    // Watermark mode (template mode = global, custom mode = per-PDF)
    void onWatermarkModeChanged();

    // Watermark templates
    void refreshTemplateCombo(const QString& select = QString());
    void refreshTemplatePreview();
    void loadTemplateByName(const QString& name);  // custom mode: fill editor
    void onSaveAsTemplate();
    void onManageTemplates();
    WatermarkTemplate currentUiAsTemplate(const QString& name) const;
    void applyTemplateToFile(const WatermarkTemplate& tpl, const QString& filePath);
    void applyTemplateToCurrentUi(const WatermarkTemplate& tpl);
    // Applies a template to every PDF in the list; returns how many were set.
    int applyTemplateToAllFiles(const WatermarkTemplate& tpl);
    WatermarkTemplate selectedTemplate() const;  // invalid when none is picked

    // Auto-update
    void onCheckForUpdates();
    void onSilentUpdateAvailable(const UpdateInfo& info);

private:
    void setupUi();
    void setupConnections();
    WatermarkConfig currentConfig() const;
    // Centralised style <-> UI mapping so templates, per-file configs and the
    // live controls can never drift apart.
    WatermarkConfig styleFromUi() const;
    void applyStyleToUi(const WatermarkConfig& style);
    void updateUiState(bool running);

    // Per-PDF watermark management
    void saveCurrentWatermarks();
    void loadWatermarksForSelectedFile();

    std::vector<TaskManager::FileSubtask> buildAllConfigs(BatchScope scope);
    void runBatch(BatchScope scope);

    // Single confirmation gate before a batch starts: shows what will be
    // produced (files, outputs, output directory) and, for large/memory-tight
    // batches, the memory recommendation. Returns false when the user cancels.
    bool confirmBatchRun(const std::vector<TaskManager::FileSubtask>& subtasks,
                         BatchScope scope);

    // Dynamic button copy, e.g. "生成勾选的 PDF（3 个）", depends on mode + checked files.
    void refreshActionLabels();
    // Scope banner: which PDFs / which template the right panel currently edits.
    void refreshScopeLabel();
    WorkloadEstimate estimateCurrentWorkload(
        const std::vector<TaskManager::FileSubtask>& subtasks) const;
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
    QString statusAt(int row) const;
    void setStatus(int row, const QString& status, const QString& tooltip = QString());
    bool isCheckedAt(int row) const;
    void setCheckedAt(int row, bool on);
    std::vector<QString> checkedPaths() const;
    int checkedCount() const;

    // True when the right panel is in "template mode" (global, all PDFs share
    // one template) instead of "custom mode" (edit the current PDF only).
    bool isTemplateMode() const;
    // True when the visible watermark rows contain at least one non-empty text.
    bool hasAnyWatermarkRow() const;

    // Widgets
    QTableWidget* fileTable_ = nullptr;
    QPushButton* openFolderBtn_ = nullptr;
    QString lastOutputDir_;
    // Watermark panel
    // Watermark mode switch (segmented control) + stacked right panel
    QPushButton* modeTemplateBtn_ = nullptr;
    QPushButton* modeCustomBtn_ = nullptr;
    QButtonGroup* modeButtonGroup_ = nullptr;
    QLabel* scopeLabel_ = nullptr;
    QStackedWidget* watermarkModeStack_ = nullptr;

    // Watermark template controls
    QComboBox* templateCombo_ = nullptr;
    QPushButton* saveTemplateBtn_ = nullptr;
    QPushButton* manageTemplateBtn_ = nullptr;
    QPushButton* loadTemplateBtn_ = nullptr;   // custom mode: fill editor from template
    QPushButton* gotoCustomBtn_ = nullptr;     // template mode empty state
    QLabel* tplPreviewLabel_ = nullptr;
    QLabel* customHintLabel_ = nullptr;        // custom mode empty state
    QScrollArea* watermarkScrollArea_ = nullptr;
    QWidget* watermarkContainer_ = nullptr;   // holds rows in a QVBoxLayout
    QVBoxLayout* watermarkLayout_ = nullptr; // owns WatermarkRow widgets
    QPushButton* addWatermarkBtn_ = nullptr;

    // Shared watermark params
    QSlider* depthSlider_ = nullptr;
    QLabel* depthValueLabel_ = nullptr;
    QSpinBox* depthSpin_ = nullptr;
    QPushButton* previewBtn_ = nullptr;
    QLineEdit* outputDirEdit_ = nullptr;
    // New font/rotation controls
    QDoubleSpinBox* rotationSpin_ = nullptr;
    QFontComboBox* fontCombo_ = nullptr;
    QCheckBox* boldCheck_ = nullptr;
    QCheckBox* italicCheck_ = nullptr;
    QProgressBar* totalProgressBar_ = nullptr;
    QDialog* previewDialog_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* startAllBtn_ = nullptr;
    QPushButton* startCheckedBtn_ = nullptr;
    QPushButton* selectAllBtn_ = nullptr;
    QPushButton* selectNoneBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;

    TaskManager taskManager_;
    std::unordered_map<std::string, std::string> knownPasswords_;

    // Per-file watermark map: filePath → list of full WatermarkConfig (text + style)
    std::unordered_map<QString, std::vector<WatermarkConfig>> fileWatermarkConfigs_;
    // Per-file style overrides: filePath → WatermarkConfig used as style template for new rows
    std::unordered_map<QString, WatermarkConfig> perPdfConfigMap_;
    QString currentSelectedFile_;  // currently displayed PDF path

    int nextWatermarkIndex_ = 0;  // monotonic index for WatermarkRow identity
    QHash<QString, int> fileNameToRow_;  // basename -> file table row
    bool preflightSuppressed_ = false;   // "don't warn again this session"
    bool suppressBatchConfirm_ = false;  // "don't confirm small batches again"
    WatermarkTemplateStore templateStore_;
    AutoUpdater* autoUpdater_ = nullptr;
};

} // namespace pdfmark