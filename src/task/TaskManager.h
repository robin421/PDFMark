// PDFMark - TaskManager: streaming pipeline, batch queue, cancellation.
#pragma once

#include "common/Common.h"
#include "watermark/WatermarkConfig.h"
#include "task/WorkloadEstimate.h"
#include "watermark/WatermarkTemplate.h"
#include <QObject>
#include <QString>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <filesystem>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace pdfmark {

class TaskManager : public QObject {
    Q_OBJECT
public:
    // One subtask == one (source PDF, template) pair == exactly one output PDF.
    // A template's lines all share one style and are overlaid together, so a
    // multi-line template still produces a single output document.
    struct FileSubtask {
        fs::path input;
        WatermarkTemplate tpl;
    };

    explicit TaskManager(QObject* parent = nullptr);
    ~TaskManager() override;

    // Configuration
    void setWatermarkConfig(const WatermarkConfig& config);
    void setWatermarkConfigs(const std::vector<WatermarkConfig>& configs);
    void setSubtasks(const std::vector<FileSubtask>& subtasks);
    // Legacy shim kept for tests / older callers. The UI no longer sets a
    // performance mode; it passes a fully derived ConcurrencyPolicy instead.
    void setPerformanceMode(PerformanceMode mode);

    // Fully automatic concurrency: window, thread count, throttle and the RAM
    // budget used for runtime adaptation. See planConcurrency().
    void setConcurrencyPolicy(const ConcurrencyPolicy& policy);

    // Hard cap on how many source documents are processed concurrently.
    // 0 (default) => whatever the performance mode allows. The UI derives this
    // from the memory budget so a huge batch cannot exhaust RAM.
    void setMaxConcurrentDocuments(int count);

    // Optional pause between two job submissions (ms). Only used for very large
    // batches so the machine stays responsive ("process progressively").
    void setThrottleMs(int ms);

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

    // Stop the supervisor thread and drain the worker pool. Idempotent and safe
    // to call from the owner's destructor: guarantees no runnable outlives this
    // object (previously the supervisor thread was never joined, so runnables
    // captured a dangling `this` and crashed on exit).
    void shutdown();

    // Diagnostics: effective concurrency of the last/current run and the
    // highest number of documents that were actually in flight at once.
    int effectiveConcurrency() const { return effectiveConcurrency_.load(); }
    int peakConcurrentDocuments() const { return peakInFlightDocs_.load(); }
    int throttleMs() const { return throttleMs_.load(); }
    // Live window, which may shrink/grow while running to respect the RAM budget.
    int currentWindow() const { return currentWindow_.load(); }
    long long observedRss() const { return observedRss_.load(); }

signals:
    void fileStarted(const QString& fileName, int index, int total);
    void pageProgress(const QString& fileName, int currentPage, int totalPages);
    void fileProgress(int completedFiles, int totalFiles);
    void fileFinished(const FileResult& result);
    void allFinished(const std::vector<FileResult>& results);
    void errorOccurred(const QString& message);
    void cancelled();
    void passwordRequired(const QString& filePath);
    // Emitted when the adaptive window changes mid-run (e.g. 2 -> 1).
    void concurrencyChanged(int window);

private:
    struct FileTaskItem {
        fs::path input;
        fs::path output;
        WatermarkTemplate tpl;
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

    // Output path = <output dir | source dir>/<sanitized folder>/<name>.pdf.
    //
    // `folder` is the template's effective folder (WatermarkTemplate::folderName()),
    // `variantSuffix` an optional file-name suffix that keeps several styles of
    // the same watermark text apart inside one folder; a blank suffix keeps the
    // plain "<source>.pdf" name. `duplicateIndex` > 0 appends "_2"/"_3" when the
    // result still collides (blank or identical suffixes).
    fs::path outputPathFor(const fs::path& input,
                           const std::string& folder,
                           const std::string& variantSuffix = std::string(),
                           int duplicateIndex = 0) const;

    // Sliding-window synchronisation between the supervisor thread (which
    // submits jobs) and the pool workers (which complete them).
    struct BatchSyncState {
        std::mutex mtx;
        std::condition_variable cv;
        int inFlight = 0;
    };

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
    std::atomic<bool> shuttingDown_{false};

    std::atomic<int> maxConcurrentDocs_{0};
    std::atomic<int> poolThreads_{0};
    std::atomic<int> throttleMs_{0};
    std::atomic<int64_t> ramBudgetBytes_{0};
    std::atomic<int> maxWindow_{1};
    std::atomic<int> currentWindow_{1};
    std::atomic<long long> observedRss_{0};
    std::atomic<int> effectiveConcurrency_{0};
    std::atomic<int> inFlightDocs_{0};
    std::atomic<int> peakInFlightDocs_{0};

    // Owned pool (NOT QThreadPool::globalInstance(): the global pool is shared
    // with the rest of Qt and cannot be safely drained on shutdown).
    QThreadPool* pool_ = nullptr;
    QThread* supervisorThread_ = nullptr;
    std::shared_ptr<BatchSyncState> sync_;
};

} // namespace pdfmark