// PDFMark - TaskManager: streaming pipeline, batch queue, cancellation.
#pragma once

#include "common/Common.h"
#include "watermark/WatermarkConfig.h"
#include <QObject>
#include <QString>
#include <QThreadPool>
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace pdfmark {

class TaskManager : public QObject {
    Q_OBJECT
public:
    struct FileSubtask {
        fs::path input;
        WatermarkConfig config;
    };

    explicit TaskManager(QObject* parent = nullptr);
    ~TaskManager() override;

    // Configuration
    void setWatermarkConfig(const WatermarkConfig& config);
    void setWatermarkConfigs(const std::vector<WatermarkConfig>& configs);
    void setSubtasks(const std::vector<FileSubtask>& subtasks);
    void setPerformanceMode(PerformanceMode mode);
    void setOutputDirectory(const fs::path& dir);
    void setPasswords(const std::unordered_map<std::string, std::string>& passwords);
    // Queue management (call before start, or after previous run finished)
    void addFile(const fs::path& path);
    void addFiles(const std::vector<fs::path>& paths);
    void addFolder(const fs::path& folder);
    void clear();
    int fileCount() const;
    std::vector<fs::path> files() const;

    // Execution
    void start();
    void cancel();
    bool isRunning() const;
    const std::vector<FileResult>& results() const;

signals:
    void fileStarted(const QString& fileName, int index, int total);
    void pageProgress(const QString& fileName, int currentPage, int totalPages);
    void fileProgress(int completedFiles, int totalFiles);
    void fileFinished(const FileResult& result);
    void allFinished(const std::vector<FileResult>& results);
    void errorOccurred(const QString& message);
    void cancelled();
    void passwordRequired(const QString& filePath);

private:
    struct FileTaskItem {
        fs::path input;
        fs::path output;
        WatermarkConfig config;
        int subtaskIndex = 0;
        QString displayName;
    };

    struct DocumentBatchJob {
        fs::path input;
        std::vector<FileTaskItem> tasks;
        int pageCount = 0;
    };

    // Internal per-document batch processing (processes 1 source PDF with N watermarks,
    // rasterizing each page only once and sharing base pixels across all target watermarks).
    void processDocumentBatch(const DocumentBatchJob& docJob,
                              int totalSubtasks,
                              std::shared_ptr<std::atomic<int>> completedUnits,
                              int totalUnits,
                              std::shared_ptr<std::atomic<int>> lastReportedPct);

    // Legacy single-file helper retained for backward compatibility.
    FileResult processSingleFile(const fs::path& input,
                                 const fs::path& output,
                                 const WatermarkConfig& config,
                                 std::function<void(int,int)> pageCallback);

    fs::path outputPathFor(const fs::path& input, const std::string& watermarkText = "", int duplicateIndex = 0) const;
    mutable std::mutex mutex_;
    WatermarkConfig config_;
    std::vector<WatermarkConfig> configs_;
    std::vector<FileSubtask> subtasks_;
    std::vector<fs::path> queue_;
    PerformanceMode perfMode_ = PerformanceMode::Normal;
    fs::path outputDir_;
    std::unordered_map<std::string, std::string> passwords_;
    std::vector<FileResult> results_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};

    QThreadPool* pool_ = nullptr;
};

} // namespace pdfmark