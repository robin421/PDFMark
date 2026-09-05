// PDFMark - TaskManager implementation.
#include "task/TaskManager.h"
#include "task/WorkerPool.h"
#include "pdf/PdfDocument.h"
#include "pdf/PdfRenderer.h"
#include "pdf/PdfWriter.h"
#include "watermark/WatermarkRenderer.h"
#include "diagnostics/Diagnostics.h"
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <QDir>
#include <iostream>

namespace pdfmark {

TaskManager::TaskManager(QObject* parent)
    : QObject(parent),
      pool_(QThreadPool::globalInstance()) {}

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

fs::path TaskManager::outputPathFor(const fs::path& input, const std::string& watermarkText) const {
    std::string stem = sanitizeFilename(input.stem().string());
    std::string cleanTag = watermarkText.empty() ? std::string("watermarked")
                                                 : sanitizeFilename(watermarkText);
    std::string filename = cleanTag + ".pdf";
    std::string subdir = stem.empty() ? "output" : stem;

    if (!outputDir_.empty()) {
        return outputDir_ / subdir / filename;
    }
    return input.parent_path() / subdir / filename;
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

    fs::path tempOutput = output;
    tempOutput += ".tmp.pdf";

    std::string password;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Use pathToString (UTF-8) to match keys stored from QString::toStdString (UTF-8)
        auto it = passwords_.find(pathToString(input));
        if (it != passwords_.end()) password = it->second;
    }
    try {
        // Ensure target subdirectory exists
        fs::create_directories(output.parent_path());

        PdfDocumentHandle srcDoc = PdfDocument::open(input, password);
        int totalPages = PdfDocument::pageCount(srcDoc.get());
        result.totalPages = totalPages;

        if (totalPages <= 0) {
            throw PdfError("PDF has no pages: " + input.string());
        }
        PdfDocumentHandle dstDoc = PdfDocument::create();

        for (int i = 0; i < totalPages; ++i) {
            if (cancelRequested_.load()) {
                throw PdfError("Operation cancelled by user");
            }

            // Stream single page
            PdfPageHandle srcPage = PdfDocument::loadPage(srcDoc.get(), i);
            double widthPt = PdfDocument::getPageWidth(srcPage.get());
            double heightPt = PdfDocument::getPageHeight(srcPage.get());

            // 1. Rasterize
            QImage image = PdfRenderer::rasterizePage(srcPage.get(), config.dpi);

            // 2. Watermark fuse
            WatermarkRenderer::applyWatermark(image, config);

            // 3. Bake into new page
            PdfWriter::appendRasterPage(dstDoc.get(), image, widthPt, heightPt, config.jpegQuality);

            // Notify progress
            if (pageCallback) {
                pageCallback(i + 1, totalPages);
            }
        }

        // Write output
        PdfDocument::save(dstDoc.get(), tempOutput);

        // Atomic rename
        std::error_code ec;
        fs::rename(tempOutput, output, ec);
        if (ec) {
            // Fallback for cross-filesystem moves
            fs::copy_file(tempOutput, output, fs::copy_options::overwrite_existing, ec);
            fs::remove(tempOutput, ec);
        }

        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = e.what();
        // Clean up partial file on error
        std::error_code ec;
        fs::remove(tempOutput, ec);
    }

    result.elapsedMs = elapsed;
    return result;
}

void TaskManager::start() {
    if (running_.exchange(true)) {
        return; // Already running
    }

    cancelRequested_.store(false);

    // Launch worker thread
    pool_->start([this]() {
        std::vector<FileSubtask> activeSubtasks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!subtasks_.empty()) {
                activeSubtasks = subtasks_;
            } else {
                std::vector<WatermarkConfig> activeConfigs = configs_.empty() ? std::vector<WatermarkConfig>{config_} : configs_;
                for (const auto& f : queue_) {
                    for (const auto& c : activeConfigs) {
                        activeSubtasks.push_back({f, c});
                    }
                }
            }
            results_.clear();
        }
        int totalSubtasks = static_cast<int>(activeSubtasks.size());
        int completed = 0;

        for (size_t idx = 0; idx < activeSubtasks.size(); ++idx) {
            if (cancelRequested_.load()) {
                break;
            }

            const auto& task = activeSubtasks[idx];
            const auto& filePath = task.input;
            const auto& cfg = task.config;
            // Use UTF-8 → QString to avoid GBK/ACP corruption on Chinese Windows
            QString fileName = QString::fromUtf8(filePath.filename().u8string().c_str());
            QString displayName = QString::fromUtf8("%1 [%2]")
                .arg(fileName)
                .arg(QString::fromUtf8(cfg.text));

            fs::path outPath = outputPathFor(filePath, cfg.text);

            auto pageCb = [this, displayName](int cur, int tot) {
                emit pageProgress(displayName, cur, tot);
            };

            FileResult res = processSingleFile(filePath, outPath, cfg, pageCb);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                results_.push_back(res);
            }

            completed++;
            emit fileFinished(res);
            emit fileProgress(completed, totalSubtasks);
        }

        std::vector<FileResult> finalResults;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finalResults = results_;
        }

        running_.store(false);
        emit allFinished(finalResults);
    });
}

} // namespace pdfmark