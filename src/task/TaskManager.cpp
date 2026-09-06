// PDFMark - TaskManager implementation.
#include "task/TaskManager.h"
#include "task/WorkerPool.h"
#include "pdf/PdfDocument.h"
#include "pdf/PdfRenderer.h"
#include "pdf/PdfWriter.h"
#include "watermark/WatermarkRenderer.h"
#include "diagnostics/Diagnostics.h"
#include "diagnostics/CrashReporter.h"
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <iostream>
#include <atomic>
#include <chrono>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace pdfmark {

static uint64_t getUniqueTempId() {
#ifdef _WIN32
    uint64_t pid = static_cast<uint64_t>(GetCurrentProcessId());
#else
    uint64_t pid = static_cast<uint64_t>(getpid());
#endif
    static std::atomic<uint64_t> counter{0};
    uint64_t cnt = counter.fetch_add(1, std::memory_order_relaxed);
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return (pid << 48) ^ (static_cast<uint64_t>(now) << 16) ^ cnt;
}

TaskManager::TaskManager(QObject* parent)
    : QObject(parent),
      pool_(QThreadPool::globalInstance()) {
    qRegisterMetaType<FileResult>("FileResult");
    qRegisterMetaType<std::vector<FileResult>>("std::vector<FileResult>");
}

TaskManager::~TaskManager() {
    cancel();
}

void TaskManager::setWatermarkConfig(const WatermarkConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    configs_ = { config };
}

void TaskManager::setWatermarkConfigs(const std::vector<WatermarkConfig>& configs) {
    std::lock_guard<std::mutex> lock(mutex_);
    configs_ = configs;
    if (!configs_.empty()) {
        config_ = configs_.front();
    }
}

void TaskManager::setSubtasks(const std::vector<FileSubtask>& subtasks) {
    std::lock_guard<std::mutex> lock(mutex_);
    subtasks_ = subtasks;
    if (!subtasks_.empty()) {
        config_ = subtasks_.front().config;
    }
}

void TaskManager::setPerformanceMode(PerformanceMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    perfMode_ = mode;
    pool_->setMaxThreadCount(WorkerPool::idealWorkerCount(mode));
}

void TaskManager::setOutputDirectory(const fs::path& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    outputDir_ = dir;
}

void TaskManager::setPasswords(const std::unordered_map<std::string, std::string>& passwords) {
    std::lock_guard<std::mutex> lock(mutex_);
    passwords_ = passwords;
}

void TaskManager::addFile(const fs::path& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        if (fs::exists(path) && fs::is_regular_file(path)) {
            queue_.push_back(path);
        }
    } catch (const std::exception& e) {
        std::cerr << "TaskManager::addFile failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "TaskManager::addFile failed: unknown error" << std::endl;
    }
}

void TaskManager::addFiles(const std::vector<fs::path>& paths) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& p : paths) {
        try {
            if (fs::exists(p) && fs::is_regular_file(p)) {
                queue_.push_back(p);
            }
        } catch (...) {
            // Skip unreadable path
        }
    }
}

void TaskManager::addFolder(const fs::path& folder) {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        if (!fs::exists(folder) || !fs::is_directory(folder)) return;
        for (const auto& entry : fs::recursive_directory_iterator(folder)) {
            if (entry.is_regular_file() && entry.path().extension() == ".pdf") {
                queue_.push_back(entry.path());
            }
        }
    } catch (...) {
        // Directory traversal failed; skip
    }
}

void TaskManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load()) return;
    queue_.clear();
    results_.clear();
    subtasks_.clear();
}

int TaskManager::fileCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(queue_.size());
}

std::vector<fs::path> TaskManager::files() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_;
}

bool TaskManager::isRunning() const {
    return running_.load();
}

const std::vector<FileResult>& TaskManager::results() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return results_;
}

void TaskManager::cancel() {
    if (running_.load()) {
        cancelRequested_.store(true);
        emit cancelled();
    }
}

fs::path TaskManager::outputPathFor(const fs::path& input,
                                   const std::string& watermarkText,
                                   int duplicateIndex) const {
    std::string stem = sanitizeFilename(pathToString(input.stem()));
    std::string cleanTag = watermarkText.empty() ? std::string("watermarked")
                                                 : sanitizeFilename(watermarkText);
    std::string filename = cleanTag;
    if (duplicateIndex > 0) {
        filename += "_" + std::to_string(duplicateIndex);
    }
    filename += ".pdf";
    std::string subdir = stem.empty() ? "output" : stem;

    if (!outputDir_.empty()) {
        return outputDir_ / stringToPath(subdir) / stringToPath(filename);
    }
    return input.parent_path() / stringToPath(subdir) / stringToPath(filename);
}

FileResult TaskManager::processSingleFile(const fs::path& input,
                                         const fs::path& output,
                                         const WatermarkConfig& config,
                                         std::function<void(int,int)> pageCallback) {
    FileResult result;
    result.inputPath = input;
    result.outputPath = output;
    result.watermarkText = config.text;
    double elapsed = 0.0;
    ScopedTimer timer(elapsed);

    // Unique temp file to avoid collisions with concurrent tasks or file locks
    uint64_t uid = getUniqueTempId();
    fs::path tempOutput = output.parent_path() / stringToPath(
        "~" + pathToString(output.filename()) + "." + std::to_string(uid) + ".tmp");

    std::string password;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = passwords_.find(pathToString(input));
        if (it != passwords_.end()) password = it->second;
    }

    try {
        // Ensure target subdirectory exists safely
        std::error_code ecMkdir;
        fs::create_directories(output.parent_path(), ecMkdir);
        if (ecMkdir && !fs::exists(output.parent_path())) {
            throw PdfError("Cannot create output directory: " + ecMkdir.message());
        }

        PdfDocumentRef srcDocRef = PdfDocument::open(input, password);
        FPDF_DOCUMENT srcDoc = srcDocRef.first.get();
        int totalPages = PdfDocument::pageCount(srcDoc);
        result.totalPages = totalPages;

        if (totalPages <= 0) {
            throw PdfError("PDF has no pages: " + pathToString(input));
        }
        PdfDocumentHandle dstDoc = PdfDocument::create();

        for (int i = 0; i < totalPages; ++i) {
            if (cancelRequested_.load()) {
                throw PdfError("Operation cancelled by user");
            }

            PdfPageHandle srcPage = PdfDocument::loadPage(srcDoc, i);
            double widthPt = PdfDocument::getPageWidth(srcPage.get());
            double heightPt = PdfDocument::getPageHeight(srcPage.get());

            QImage image = PdfRenderer::rasterizePage(srcPage.get(), config.dpi);
            WatermarkRenderer::applyWatermark(image, config);
            PdfWriter::appendRasterPage(dstDoc.get(), image, widthPt, heightPt, config.jpegQuality);

            if (pageCallback) {
                pageCallback(i + 1, totalPages);
            }
        }

        // Write to temp file first
        PdfDocument::save(dstDoc.get(), tempOutput);

        // Atomic rename to final destination
        std::error_code ecRename;
        fs::rename(tempOutput, output, ecRename);
        if (ecRename) {
            std::error_code ecCopy;
            fs::copy_file(tempOutput, output, fs::copy_options::overwrite_existing, ecCopy);
            if (ecCopy) {
                std::error_code ecRm;
                fs::remove(tempOutput, ecRm);
                throw PdfError("Failed to save output file: rename (" + ecRename.message() +
                               "), copy (" + ecCopy.message() + ")");
            }
            std::error_code ecRm;
            fs::remove(tempOutput, ecRm);
        }

        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = e.what();
        std::error_code ec;
        fs::remove(tempOutput, ec);

        // Report task error asynchronously to GlitchTip for remote monitoring
        QString extra = QString("Input: %1\nOutput: %2\nWatermark: %3")
            .arg(QString::fromUtf8(pathToString(input).c_str()))
            .arg(QString::fromUtf8(pathToString(output).c_str()))
            .arg(QString::fromUtf8(config.text.c_str()));
        CrashReporter::sendReportAsync("TaskError", e.what(), "TaskManager::processSingleFile", extra, "");
        qCritical() << "Task error on" << QString::fromUtf8(pathToString(input.filename()).c_str())
                    << ":" << e.what();
    } catch (...) {
        result.success = false;
        result.errorMessage = "Unknown critical error occurred during processing";
        std::error_code ec;
        fs::remove(tempOutput, ec);

        CrashReporter::sendReportAsync("TaskUnknownError", "Unknown exception", "TaskManager::processSingleFile", "", "");
        qCritical() << "Unknown task error on" << QString::fromUtf8(pathToString(input.filename()).c_str());
    }

    result.elapsedMs = elapsed;
    return result;
}

void TaskManager::start() {
    if (running_.exchange(true)) {
        return; // Already running
    }

    cancelRequested_.store(false);

    // Collect subtasks under lock
    std::vector<FileSubtask> activeSubtasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!subtasks_.empty()) {
            activeSubtasks = subtasks_;
        } else {
            std::vector<WatermarkConfig> activeConfigs = configs_.empty()
                ? std::vector<WatermarkConfig>{config_} : configs_;
            for (const auto& f : queue_) {
                for (const auto& c : activeConfigs) {
                    activeSubtasks.push_back({f, c});
                }
            }
        }
        results_.clear();
    }

    int totalSubtasks = static_cast<int>(activeSubtasks.size());
    if (totalSubtasks == 0) {
        running_.store(false);
        emit allFinished({});
        return;
    }

    // Pre-assign collision-free output paths
    struct SubtaskJob {
        fs::path input;
        fs::path output;
        WatermarkConfig config;
        int index = 0;
    };

    std::vector<SubtaskJob> jobs;
    jobs.reserve(activeSubtasks.size());

    std::unordered_set<std::string> usedOutputPaths;
    for (size_t i = 0; i < activeSubtasks.size(); ++i) {
        const auto& st = activeSubtasks[i];
        int dupIdx = 0;
        fs::path out = outputPathFor(st.input, st.config.text, dupIdx);
        while (usedOutputPaths.find(pathToString(out)) != usedOutputPaths.end()) {
            dupIdx++;
            out = outputPathFor(st.input, st.config.text, dupIdx);
        }
        usedOutputPaths.insert(pathToString(out));
        jobs.push_back({st.input, out, st.config, static_cast<int>(i + 1)});
    }


    // Pre-count pages per job (on the caller thread, before worker starts).
    std::vector<int> jobPageCounts(jobs.size(), 0);
    for (size_t i = 0; i < jobs.size(); ++i) {
        try {
            auto srcDocRef = PdfDocument::open(jobs[i].input);
            jobPageCounts[i] = PdfDocument::pageCount(srcDocRef.first.get());
        } catch (...) {
            jobPageCounts[i] = 1; // Fallback to 1 page if unable to read count.
        }
    }
    int totalPageCount = 0;
    for (int pc : jobPageCounts) { totalPageCount += pc; }
    // Single-worker serial execution: tasks run one-by-one on a dedicated thread,
    // eliminating all cross-worker signal races (progress bars no longer jump).
    QThread* workerThread = QThread::create([this, jobs, jobPageCounts, totalPageCount]() {
        int totalSubtasks = static_cast<int>(jobs.size());
        for (int idx = 0; idx < totalSubtasks; ++idx) {
            if (cancelRequested_.load()) {
                // Emit cancelled results for all remaining tasks.
                for (int k = idx; k < totalSubtasks; ++k) {
                    FileResult res;
                    res.inputPath = jobs[k].input;
                    res.outputPath = jobs[k].output;
                    res.watermarkText = jobs[k].config.text;
                    res.success = false;
                    res.errorMessage = "Operation cancelled";
                    {
                        std::lock_guard<std::mutex> lk(mutex_);
                        results_.push_back(res);
                    }
                    emit fileFinished(res);
                    // Emit page-grain total progress so the bar stays monotonic.
                    int completedPages = 0;
                    for (int t = 0; t < idx; ++t) {
                        completedPages += jobPageCounts[t];
                    }
                    int pct = totalPageCount > 0 ? (completedPages * 100 / totalPageCount) : 0;
                    emit fileProgress(pct, 100);
                }
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    std::vector<FileResult> fin = results_;
                    running_.store(false);
                    emit allFinished(fin);
                }
                return;
            }

            const auto& job = jobs[idx];
            QString fileName = QString::fromUtf8(pathToString(job.input.filename()).c_str());
            QString displayName = QString("%1 [%2]")
                .arg(fileName)
                .arg(QString::fromUtf8(job.config.text.c_str()));
            emit fileStarted(displayName, idx + 1, totalSubtasks);

            // Page-grain progress: emits both page-level (for current file) and
            // page-grain total (for totalProgressBar_). total is strictly monotonic.
            auto pageCb = [this, displayName, idx, jobPageCounts, totalPageCount](int cur, int tot) {
                int completedPages = 0;
                for (int t = 0; t < idx; ++t) { completedPages += jobPageCounts[t]; }
                completedPages += cur;
                int pct = totalPageCount > 0 ? (completedPages * 100 / totalPageCount) : 0;
                emit pageProgress(displayName, cur, tot);
                emit fileProgress(pct, 100);
            };

            FileResult res;
            try {
                res = processSingleFile(job.input, job.output, job.config, pageCb);
            } catch (const std::exception& e) {
                res.inputPath = job.input;
                res.outputPath = job.output;
                res.watermarkText = job.config.text;
                res.success = false;
                res.errorMessage = e.what();
                qCritical() << "Error processing file" << displayName << ":" << e.what();
            } catch (...) {
                res.inputPath = job.input;
                res.outputPath = job.output;
                res.watermarkText = job.config.text;
                res.success = false;
                res.errorMessage = "Unknown error during processing";
                qCritical() << "Unknown error processing file" << displayName;
            }
            {
                std::lock_guard<std::mutex> lk(mutex_);
                results_.push_back(res);
            }
            emit fileFinished(res);
        }

        std::vector<FileResult> fin;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            fin = results_;
        }
        running_.store(false);
        emit allFinished(fin);
    });

    workerThread->start();
    connect(workerThread, &QThread::finished, workerThread, &QThread::deleteLater);
}

} // namespace pdfmark
