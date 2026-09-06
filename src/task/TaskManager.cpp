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
#include <QRunnable>
#include <iostream>
#include <atomic>
#include <chrono>
#include <unordered_set>
#include <memory>
#include <condition_variable>
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

void TaskManager::processDocumentBatch(const DocumentBatchJob& docJob,
                                       int totalSubtasks,
                                       std::shared_ptr<std::atomic<int>> completedUnits,
                                       int totalUnits,
                                       std::shared_ptr<std::atomic<int>> lastReportedPct) {
    if (docJob.tasks.empty()) return;

    // Emit fileStarted for all tasks in this document batch
    for (const auto& task : docJob.tasks) {
        emit fileStarted(task.displayName, task.subtaskIndex + 1, totalSubtasks);
    }

    if (cancelRequested_.load()) {
        for (const auto& task : docJob.tasks) {
            FileResult res;
            res.inputPath = task.input;
            res.outputPath = task.output;
            res.watermarkText = task.config.text;
            res.success = false;
            res.errorMessage = "Operation cancelled";
            {
                std::lock_guard<std::mutex> lk(mutex_);
                results_[task.subtaskIndex] = res;
            }
            emit fileFinished(res);
        }
        return;
    }

    std::string password;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = passwords_.find(pathToString(docJob.input));
        if (it != passwords_.end()) password = it->second;
    }

    struct OutputContext {
        const FileTaskItem* task = nullptr;
        fs::path tempOutput;
        PdfDocumentHandle dstDoc;
        WatermarkRenderer::Stamp stamp;
        double elapsedMs = 0.0;
        std::unique_ptr<ScopedTimer> timer;
    };

    std::vector<OutputContext> contexts(docJob.tasks.size());
    for (size_t k = 0; k < docJob.tasks.size(); ++k) {
        contexts[k].task = &docJob.tasks[k];
        uint64_t uid = getUniqueTempId();
        contexts[k].tempOutput = docJob.tasks[k].output.parent_path() / stringToPath(
            "~" + pathToString(docJob.tasks[k].output.filename()) + "." + std::to_string(uid) + ".tmp");
        contexts[k].timer = std::make_unique<ScopedTimer>(contexts[k].elapsedMs);
        contexts[k].dstDoc = PdfDocument::create();
        contexts[k].stamp = WatermarkRenderer::createStamp(docJob.tasks[k].config);

        std::error_code ecMkdir;
        fs::create_directories(docJob.tasks[k].output.parent_path(), ecMkdir);
    }

    PdfDocumentRef srcDocRef;
    int totalPages = 0;
    try {
        srcDocRef = PdfDocument::open(docJob.input, password);
        totalPages = PdfDocument::pageCount(srcDocRef.first.get());
        if (totalPages <= 0) {
            throw PdfError("PDF has no pages: " + pathToString(docJob.input));
        }
    } catch (const std::exception& e) {
        for (auto& ctx : contexts) {
            ctx.timer.reset();
            FileResult res;
            res.inputPath = ctx.task->input;
            res.outputPath = ctx.task->output;
            res.watermarkText = ctx.task->config.text;
            res.success = false;
            res.errorMessage = e.what();
            res.elapsedMs = ctx.elapsedMs;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                results_[ctx.task->subtaskIndex] = res;
            }
            emit fileFinished(res);
        }
        return;
    }

    bool batchOk = true;
    std::string batchError;

    for (int pageIdx = 0; pageIdx < totalPages; ++pageIdx) {
        if (cancelRequested_.load()) {
            batchOk = false;
            batchError = "Operation cancelled by user";
            break;
        }

        try {
            PdfPageHandle srcPage = PdfDocument::loadPage(srcDocRef.first.get(), pageIdx);
            double widthPt = PdfDocument::getPageWidth(srcPage.get());
            double heightPt = PdfDocument::getPageHeight(srcPage.get());

            // Single rasterization per page shared across all watermarks
            int baseDpi = contexts[0].task->config.dpi;
            QImage baseImage = PdfRenderer::rasterizePage(srcPage.get(), baseDpi);

            for (size_t k = 0; k < contexts.size(); ++k) {
                QImage pageImg;
                if (contexts[k].task->config.dpi == baseDpi) {
                    if (k == contexts.size() - 1) {
                        pageImg = std::move(baseImage);
                    } else {
                        pageImg = baseImage.copy();
                    }
                } else {
                    pageImg = PdfRenderer::rasterizePage(srcPage.get(), contexts[k].task->config.dpi);
                }

                WatermarkRenderer::applyWatermark(pageImg, contexts[k].task->config, contexts[k].stamp);
                PdfWriter::appendRasterPage(contexts[k].dstDoc.get(), pageImg, widthPt, heightPt, contexts[k].task->config.jpegQuality);
            }
        } catch (const std::exception& e) {
            batchOk = false;
            batchError = e.what();
            break;
        } catch (...) {
            batchOk = false;
            batchError = "Unknown error during page processing";
            break;
        }

        // Atomically update progress units and smoothly throttle emission
        int unitsDone = completedUnits->fetch_add(static_cast<int>(contexts.size())) + static_cast<int>(contexts.size());
        int pct = totalUnits > 0 ? (unitsDone * 100 / totalUnits) : 0;
        if (pct > 100) pct = 100;
        int prev = lastReportedPct->load(std::memory_order_relaxed);
        while (pct > prev && !lastReportedPct->compare_exchange_weak(prev, pct)) {
            // monotonic lock-free CAS
        }
        if (pct > prev) {
            emit fileProgress(pct, 100);
        }
        emit pageProgress(contexts[0].task->displayName, pageIdx + 1, totalPages);
    }

    // Save outputs to temporary paths and atomically rename
    for (auto& ctx : contexts) {
        ctx.timer.reset();
        FileResult res;
        res.inputPath = ctx.task->input;
        res.outputPath = ctx.task->output;
        res.watermarkText = ctx.task->config.text;
        res.totalPages = totalPages;
        res.elapsedMs = ctx.elapsedMs;

        if (!batchOk) {
            res.success = false;
            res.errorMessage = batchError;
            std::error_code ec;
            fs::remove(ctx.tempOutput, ec);
        } else {
            try {
                PdfDocument::save(ctx.dstDoc.get(), ctx.tempOutput);
                std::error_code ecRename;
                fs::rename(ctx.tempOutput, ctx.task->output, ecRename);
                if (ecRename) {
                    std::error_code ecCopy;
                    fs::copy_file(ctx.tempOutput, ctx.task->output, fs::copy_options::overwrite_existing, ecCopy);
                    if (ecCopy) {
                        std::error_code ecRm;
                        fs::remove(ctx.tempOutput, ecRm);
                        throw PdfError("Failed to save output file: rename (" + ecRename.message() +
                                       "), copy (" + ecCopy.message() + ")");
                    }
                    std::error_code ecRm;
                    fs::remove(ctx.tempOutput, ecRm);
                }
                res.success = true;
            } catch (const std::exception& e) {
                res.success = false;
                res.errorMessage = e.what();
                std::error_code ec;
                fs::remove(ctx.tempOutput, ec);
            }
        }

        {
            std::lock_guard<std::mutex> lk(mutex_);
            results_[ctx.task->subtaskIndex] = res;
        }
        emit fileFinished(res);
    }
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
    std::vector<FileTaskItem> allTasks;
    allTasks.reserve(activeSubtasks.size());

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

        QString fileName = QString::fromUtf8(pathToString(st.input.filename()).c_str());
        QString displayName = QString("%1 [%2]")
            .arg(fileName)
            .arg(QString::fromUtf8(st.config.text.c_str()));

        allTasks.push_back({st.input, out, st.config, static_cast<int>(i), displayName});
    }

    // Group tasks by source document to batch rasterization
    std::vector<DocumentBatchJob> docJobs;
    std::unordered_map<std::string, size_t> inputToJobIdx;
    for (const auto& task : allTasks) {
        std::string key = pathToString(task.input);
        auto it = inputToJobIdx.find(key);
        if (it == inputToJobIdx.end()) {
            inputToJobIdx[key] = docJobs.size();
            DocumentBatchJob dj;
            dj.input = task.input;
            dj.tasks.push_back(task);
            docJobs.push_back(std::move(dj));
        } else {
            docJobs[it->second].tasks.push_back(task);
        }
    }

    // Pre-count total page units for strictly monotonic progress reporting
    int totalUnits = 0;
    for (auto& dj : docJobs) {
        try {
            auto srcDocRef = PdfDocument::open(dj.input);
            dj.pageCount = PdfDocument::pageCount(srcDocRef.first.get());
        } catch (...) {
            dj.pageCount = 1;
        }
        totalUnits += dj.pageCount * static_cast<int>(dj.tasks.size());
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        results_.assign(allTasks.size(), FileResult{});
    }

    auto completedUnits = std::make_shared<std::atomic<int>>(0);
    auto lastReportedPct = std::make_shared<std::atomic<int>>(0);

    int maxWorkers = WorkerPool::idealWorkerCount(perfMode_);
    pool_->setMaxThreadCount(maxWorkers);

    QThread* supervisorThread = QThread::create([this, docJobs, totalSubtasks, completedUnits, totalUnits, lastReportedPct]() {
        struct SyncState {
            std::mutex mtx;
            std::condition_variable cv;
            int remaining = 0;
        };
        auto sync = std::make_shared<SyncState>();
        sync->remaining = static_cast<int>(docJobs.size());

        for (const auto& dj : docJobs) {
            if (cancelRequested_.load()) {
                for (const auto& task : dj.tasks) {
                    FileResult res;
                    res.inputPath = task.input;
                    res.outputPath = task.output;
                    res.watermarkText = task.config.text;
                    res.success = false;
                    res.errorMessage = "Operation cancelled";
                    {
                        std::lock_guard<std::mutex> lk(mutex_);
                        results_[task.subtaskIndex] = res;
                    }
                    emit fileFinished(res);
                }
                {
                    std::lock_guard<std::mutex> lk(sync->mtx);
                    sync->remaining--;
                    if (sync->remaining == 0) {
                        sync->cv.notify_one();
                    }
                }
                continue;
            }

            pool_->start(QRunnable::create([this, dj, totalSubtasks, completedUnits, totalUnits, lastReportedPct, sync]() {
                processDocumentBatch(dj, totalSubtasks, completedUnits, totalUnits, lastReportedPct);
                {
                    std::lock_guard<std::mutex> lk(sync->mtx);
                    sync->remaining--;
                    if (sync->remaining == 0) {
                        sync->cv.notify_one();
                    }
                }
            }));
        }

        // Wait for all worker batches to finish
        {
            std::unique_lock<std::mutex> lk(sync->mtx);
            sync->cv.wait(lk, [&]() { return sync->remaining == 0; });
        }

        std::vector<FileResult> fin;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            fin = results_;
        }
        running_.store(false);
        emit allFinished(fin);
    });

    supervisorThread->start();
    connect(supervisorThread, &QThread::finished, supervisorThread, &QThread::deleteLater);
}

} // namespace pdfmark
