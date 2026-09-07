// PDFMark - Main GUI Window.
#pragma once

#include "common/Common.h"
#include "watermark/WatermarkConfig.h"
#include "task/TaskManager.h"
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

namespace pdfmark {

class WatermarkRow : public QWidget {
    Q_OBJECT
public:
    explicit WatermarkRow(const QString& initialText, int index, QWidget* parent = nullptr);
    ~WatermarkRow() override = default;

    QString text() const;
    void setText(const QString& t);
    int index() const { return index_; }

signals:
    void textChanged();
    void removeRequested(int index);

private:
    QLineEdit* lineEdit_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
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
    void onStartAllClicked();
    void onStartSelectedClicked();
    void onCancelClicked();
    void onOpenOutputFolder();
    void onFileSelectionChanged();
    // TaskManager signals
    void onFileStarted(const QString& fileName, int index, int total);
    void onPageProgress(const QString& fileName, int current, int total);
    void onFileProgress(int completed, int total);
    void onFileFinished(const FileResult& result);
    void onAllFinished(const std::vector<FileResult>& results);
    void onCancelled();
    void onPasswordRequired(const QString& filePath);

    // Watermark row management
    void addWatermarkRow();
    void removeWatermarkRow(int index);
    void onWatermarkTextChanged();
    void onPreviewWatermark();

    // Watermark row management

    // Auto-update
    void onCheckForUpdates();
    void onSilentUpdateAvailable(const UpdateInfo& info);

private:
    void setupUi();
    void setupConnections();
    WatermarkConfig currentConfig() const;
    void updateUiState(bool running);

    // Per-PDF watermark management
    void saveCurrentWatermarks();
    void loadWatermarksForSelectedFile();

    std::vector<TaskManager::FileSubtask> buildAllConfigs(bool onlySelected = false);
    void runBatch(bool onlySelected);

    // Widgets
    QTableWidget* fileTable_ = nullptr;
    QPushButton* openFolderBtn_ = nullptr;
    QString lastOutputDir_;
    // Watermark panel
    QScrollArea* watermarkScrollArea_ = nullptr;
    QWidget* watermarkContainer_ = nullptr;   // holds rows in a QVBoxLayout
    QVBoxLayout* watermarkLayout_ = nullptr; // owns WatermarkRow widgets
    QPushButton* addWatermarkBtn_ = nullptr;

    // Shared watermark params
    QSlider* depthSlider_ = nullptr;
    QLabel* depthValueLabel_ = nullptr;
    QComboBox* perfCombo_ = nullptr;
    QLineEdit* outputDirEdit_ = nullptr;
    // New font/rotation controls
    QDoubleSpinBox* rotationSpin_ = nullptr;
    QFontComboBox* fontCombo_ = nullptr;
    QCheckBox* boldCheck_ = nullptr;
    QCheckBox* italicCheck_ = nullptr;
    QProgressBar* pageProgressBar_ = nullptr;
    QProgressBar* totalProgressBar_ = nullptr;
    QDialog* previewDialog_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QPushButton* startAllBtn_ = nullptr;
    QPushButton* startSelectedBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;

    TaskManager taskManager_;
    std::unordered_map<std::string, std::string> knownPasswords_;

    // Per-file watermark map: filePath → list of full WatermarkConfig (text + style)
    std::unordered_map<QString, std::vector<WatermarkConfig>> fileWatermarkConfigs_;
    // Per-file style overrides: filePath → WatermarkConfig used as style template for new rows
    std::unordered_map<QString, WatermarkConfig> perPdfConfigMap_;
    QString currentSelectedFile_;  // currently displayed PDF path

    int nextWatermarkIndex_ = 0;  // monotonic index for WatermarkRow identity
    AutoUpdater* autoUpdater_ = nullptr;
};

} // namespace pdfmark