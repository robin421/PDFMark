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
#include <algorithm>
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

namespace {
// Legacy callers hand us bare WatermarkConfigs; wrap each into a one-line
// template so the pipeline only ever deals with templates.
WatermarkTemplate singleLineTemplate(const WatermarkConfig& cfg) {
    WatermarkTemplate tpl;
    tpl.watermarks.push_back(cfg);
    tpl.name = templateNameFromConfigs(tpl.watermarks);
    if (tpl.name.empty()) tpl.name = "watermark";
    return tpl;
}
} // namespace

TaskManager::TaskManager(QObject* parent)
    : QObject(parent) {
    // Own the pool: setting maxThreadCount on the global instance would affect
    // unrelated Qt users, and we must be able to drain it deterministically.
    pool_ = new QThreadPool(this);
    pool_->setMaxThreadCount(WorkerPool::idealWorkerCount(PerformanceMode::Normal));
    qRegisterMetaType<FileResult>("FileResult");
    qRegisterMetaType<std::vector<FileResult>>("std::vector<FileResult>");
    qRegisterMetaType<int>("int");
}

TaskManager::~TaskManager() {
    shutdown();
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
        config_ = subtasks_.front().tpl.style();
    }
}

void TaskManager::setPerformanceMode(PerformanceMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    perfMode_ = mode;
    pool_->setMaxThreadCount(WorkerPool::idealWorkerCount(mode));
}

void TaskManager::setConcurrencyPolicy(const ConcurrencyPolicy& policy) {
    maxConcurrentDocs_.store(policy.maxConcurrentDocs > 0 ? policy.maxConcurrentDocs : 1);
    poolThreads_.store(policy.poolThreads > 0 ? policy.poolThreads : 1);
    throttleMs_.store(policy.throttleMs > 0 ? policy.throttleMs : 0);
    ramBudgetBytes_.store(policy.ramBudgetBytes > 0 ? policy.ramBudgetBytes : 0);
}

void TaskManager::setMaxConcurrentDocuments(int count) {
    maxConcurrentDocs_.store(count > 0 ? count : 0);
}

void TaskManager::setThrottleMs(int ms) {
    throttleMs_.store(ms > 0 ? ms : 0);
}

void TaskManager::shutdown() {
    shuttingDown_.store(true);
    cancelRequested_.store(true);
    {
        auto sync = sync_;
        if (sync) {
            std::lock_guard<std::mutex> lk(sync->mtx);
            sync->cv.notify_all();
        }
    }
    if (supervisorThread_) {
        supervisorThread_->wait();
        delete supervisorThread_;
        supervisorThread_ = nullptr;
    }
    if (pool_) {
        pool_->waitForDone();
    }
    sync_.reset();
    running_.store(false);
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
                                   const std::string& folder,
                                   const std::string& variantSuffix,
                                   int duplicateIndex) const {
    // 子目录 = 模板的有效文件夹名（outputFolder，未设置时为模板名）
    const std::string subdir = sanitizeFilename(folder);

    // 文件名 = 源 PDF 的原文件名（stem+ext），可选拼上「变体后缀」，
    // 让同一文件夹内「文字相同、样式不同」的输出彼此可区分：报告_红色.pdf
    const std::string stem = pathToString(input.stem());
    const std::string ext = pathToString(input.extension());
    const std::string suffix = sanitizeFilenameOrEmpty(variantSuffix);

    std::string filename = pathToString(input.filename());
    if (!suffix.empty()) {
        filename = stem + "_" + suffix + ext;
    }
    if (duplicateIndex > 0) {
        // 后缀留空或后缀也相同导致仍然重名时，退化为 _1 / _2 序号
        const std::string tag = suffix.empty() ? ("_" + std::to_string(duplicateIndex))
                                               : ("_" + suffix + "_" + std::to_string(duplicateIndex));
        filename = stem + tag + ext;
    }
    if (!outputDir_.empty()) {
        return outputDir_ / stringToPath(subdir) / stringToPath(filename);
    }
    // 否则放置在源 PDF 所在目录下
    return input.parent_path() / stringToPath(subdir) / stringToPath(filename);
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
            res.watermarkText = task.tpl.displayLabel();
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
        // All lines of the template share one style and go onto the SAME output
        // document, so each page is encoded once per template.
        std::vector<WatermarkConfig> lines;
        std::vector<WatermarkRenderer::Stamp> stamps;
        // Tile layout depends only on (image size, config, font), so it is
        // computed once per template and reused for every page.
        std::vector<std::vector<TileItem>> tilesPerLine;
        int tilesForWidth = 0;   // page size the cached tiles were built for
        int tilesForHeight = 0;
        // Reused across pages so the per-page full-image copy becomes a blit
        // into an already-allocated buffer (no per-page heap allocation).
        QImage scratch;
        int dpi = 200;
        int jpegQuality = 85;
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

        contexts[k].lines = templateToConfigs(docJob.tasks[k].tpl);
        if (contexts[k].lines.empty()) {
            WatermarkConfig fallback;
            fallback.text = docJob.tasks[k].tpl.name;
            contexts[k].lines.push_back(fallback);
        }
        contexts[k].dpi = contexts[k].lines.front().dpi;
        contexts[k].jpegQuality = contexts[k].lines.front().jpegQuality;
        contexts[k].stamps.reserve(contexts[k].lines.size());
        for (const auto& line : contexts[k].lines) {
            contexts[k].stamps.push_back(WatermarkRenderer::createStamp(line));
        }

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
            res.watermarkText = ctx.task->tpl.displayLabel();
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

            // Single rasterization per page, shared by every template.
            const int baseDpi = contexts[0].dpi;
            const QImage baseImage = PdfRenderer::rasterizePage(srcPage.get(), baseDpi);

            for (size_t k = 0; k < contexts.size(); ++k) {
                OutputContext& ctx = contexts[k];

                if (ctx.dpi != baseDpi) {
                    // Different DPI -> must rasterize separately (rare path).
                    QImage pageImg = PdfRenderer::rasterizePage(srcPage.get(), ctx.dpi);
                    for (size_t j = 0; j < ctx.lines.size(); ++j) {
                        WatermarkRenderer::applyWatermark(pageImg, ctx.lines[j], ctx.stamps[j]);
                    }
                    PdfWriter::appendRasterPage(ctx.dstDoc.get(), pageImg, widthPt, heightPt, ctx.jpegQuality);
                    continue;
                }

                // Tile geometry only depends on (page size, config, font), so
                // recompute it only when the page size actually changes.
                if (ctx.tilesForWidth != baseImage.width() ||
                    ctx.tilesForHeight != baseImage.height()) {
                    ctx.tilesForWidth = baseImage.width();
                    ctx.tilesForHeight = baseImage.height();
                    ctx.tilesPerLine.resize(ctx.lines.size());
                    for (size_t j = 0; j < ctx.lines.size(); ++j) {
                        ctx.tilesPerLine[j] = WatermarkTileLayout::calculateLayout(
                            baseImage.width(), baseImage.height(),
                            ctx.lines[j], ctx.stamps[j].font);
                    }
                }

                // Paint into the reusable scratch buffer: a blit of the freshly
                // rasterized page, then one bitblt per watermark line. The
                // previous code allocated a brand-new full-page QImage here for
                // every page x template.
                if (!WatermarkRenderer::fillBuffer(ctx.scratch, baseImage)) {
                    throw PdfError("Failed to allocate watermark scratch buffer");
                }
                for (size_t j = 0; j < ctx.lines.size(); ++j) {
                    WatermarkRenderer::blitTiles(ctx.scratch, ctx.stamps[j], ctx.tilesPerLine[j]);
                }
                PdfWriter::appendRasterPage(ctx.dstDoc.get(), ctx.scratch, widthPt, heightPt, ctx.jpegQuality);
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
        res.watermarkText = ctx.task->tpl.displayLabel();
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
                    activeSubtasks.push_back({f, singleLineTemplate(c)});
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
        fs::path out = outputPathFor(st.input, st.tpl.folderName(), st.tpl.variantSuffix, dupIdx);
        while (usedOutputPaths.find(pathToString(out)) != usedOutputPaths.end()) {
            dupIdx++;
            out = outputPathFor(st.input, st.tpl.folderName(), st.tpl.variantSuffix, dupIdx);
        }
        usedOutputPaths.insert(pathToString(out));

        QString fileName = QString::fromUtf8(pathToString(st.input.filename()).c_str());
        QString displayName = QString("%1 [%2]")
            .arg(fileName)
            .arg(QString::fromUtf8(st.tpl.displayLabel().c_str()));

        allTasks.push_back({st.input, out, st.tpl, static_cast<int>(i), displayName});
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

    // Thread count comes from the ConcurrencyPolicy when the UI set one; the
    // performance mode is only a fallback for legacy callers.
    int maxWorkers = poolThreads_.load();
    if (maxWorkers <= 0) maxWorkers = WorkerPool::idealWorkerCount(perfMode_);
    maxWorkers = (std::max)(1, maxWorkers);
    pool_->setMaxThreadCount(maxWorkers);

    // Bounded sliding window: never keep more than `window` source documents in
    // flight. Peak memory therefore depends on `window`, not on how many files
    // are queued. Runtime adaptation may shrink it further (never below 1).
    const int requested = maxConcurrentDocs_.load();
    int window = requested > 0 ? requested : maxWorkers;
    window = std::clamp(window, 1, maxWorkers);
    effectiveConcurrency_.store(window);
    maxWindow_.store(window);
    currentWindow_.store(window);
    inFlightDocs_.store(0);
    peakInFlightDocs_.store(0);
    observedRss_.store(0);

    const int throttle = throttleMs_.load();
    const size_t jobCount = docJobs.size();

    sync_ = std::make_shared<BatchSyncState>();
    auto sync = sync_;

    supervisorThread_ = QThread::create(
        [this, docJobs, totalSubtasks, completedUnits, totalUnits, lastReportedPct,
         sync, window, throttle, jobCount]() {
            size_t next = 0;
            {
                std::unique_lock<std::mutex> lk(sync->mtx);

                // Dispatch loop: submit one document at a time, but never let
                // more than `window` run concurrently. When a document
                // finishes it signals the condition variable and we top up.
                int adaptiveThrottle = 0;
                while (next < jobCount) {
                    sync->cv.wait(lk, [&]() {
                        return sync->inFlight < currentWindow_.load() || cancelRequested_.load();
                    });
                    if (cancelRequested_.load()) break;

                    // ── Adaptive back-off ────────────────────────────────
                    // Sample the real process footprint before queueing more
                    // work: over budget -> shrink the window and slow down,
                    // well under budget -> allow the window to grow back.
                    const int64_t budget = ramBudgetBytes_.load();
                    if (budget > 0) {
                        const long long rss = MemoryProbe::currentPhysicalBytes();
                        observedRss_.store(rss);
                        const int before = currentWindow_.load();
                        if (rss > budget && before > 1) {
                            currentWindow_.store(before - 1);
                            adaptiveThrottle = (std::min)(adaptiveThrottle + 20, 200);
                        } else if (rss * 10 < budget * 6 && before < maxWindow_.load()) {
                            currentWindow_.store(before + 1);
                            adaptiveThrottle = (std::max)(0, adaptiveThrottle - 20);
                        }
                        const int now = currentWindow_.load();
                        if (now != before) {
                            emit concurrencyChanged(now);
                        }
                    }

                    const DocumentBatchJob& dj = docJobs[next++];
                    sync->inFlight++;
                    lk.unlock();

                    pool_->start(QRunnable::create(
                        [this, dj, totalSubtasks, completedUnits, totalUnits, lastReportedPct, sync]() {
                            const int cur = inFlightDocs_.fetch_add(1) + 1;
                            int prev = peakInFlightDocs_.load();
                            while (cur > prev && !peakInFlightDocs_.compare_exchange_weak(prev, cur)) {
                                // lock-free monotonic peak update
                            }
                            processDocumentBatch(dj, totalSubtasks, completedUnits, totalUnits, lastReportedPct);
                            inFlightDocs_.fetch_sub(1);
                            {
                                std::lock_guard<std::mutex> lk2(sync->mtx);
                                sync->inFlight--;
                                sync->cv.notify_one();
                            }
                        }));

                    lk.lock();

                    // Deliberate slow-down for very large batches so the machine
                    // (and the UI) stays responsive.
                    const int pause = throttle + adaptiveThrottle;
                    if (pause > 0 && next < jobCount && !cancelRequested_.load()) {
                        lk.unlock();
                        for (int waited = 0; waited < pause && !cancelRequested_.load(); waited += 10) {
                            QThread::msleep(10);
                        }
                        lk.lock();
                    }
                }

                // Jobs that were never submitted (user cancel or shutdown).
                for (; next < jobCount; ++next) {
                    for (const auto& task : docJobs[next].tasks) {
                        FileResult res;
                        res.inputPath = task.input;
                        res.outputPath = task.output;
                        res.watermarkText = task.tpl.displayLabel();
                        res.success = false;
                        res.errorMessage = "Operation cancelled";
                        {
                            std::lock_guard<std::mutex> lk2(mutex_);
                            results_[task.subtaskIndex] = res;
                        }
                        if (!shuttingDown_.load()) {
                            emit fileFinished(res);
                        }
                    }
                }

                // Let the in-flight documents drain before reporting completion.
                sync->cv.wait(lk, [&]() { return sync->inFlight == 0; });
            }

            std::vector<FileResult> fin;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                fin = results_;
            }
            running_.store(false);
            if (!shuttingDown_.load()) {
                emit allFinished(fin);
            }
        });

    supervisorThread_->start();
}

} // namespace pdfmark
