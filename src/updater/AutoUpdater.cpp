// PDFMark - AutoUpdater implementation.
#include "updater/AutoUpdater.h"
#include "common/Common.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDebug>
#include <QCoreApplication>

namespace pdfmark {

AutoUpdater::AutoUpdater(QObject* parent)
    : QObject(parent)
    , networkManager_(new QNetworkAccessManager(this))
{
}

AutoUpdater::~AutoUpdater() {
    if (downloadReply_) {
        downloadReply_->abort();
        downloadReply_->deleteLater();
    }
    if (checkReply_) {
        checkReply_->abort();
        checkReply_->deleteLater();
    }
    if (downloadFile_) {
        downloadFile_->close();
        delete downloadFile_;
    }
}

int AutoUpdater::compareVersions(const QString& v1, const QString& v2) {
    auto nums1 = parseVersionNumbers(v1);
    auto nums2 = parseVersionNumbers(v2);
    for (int i = 0; i < 3; ++i) {
        int n1 = i < nums1.size() ? nums1[i] : 0;
        int n2 = i < nums2.size() ? nums2[i] : 0;
        if (n1 < n2) return -1;
        if (n1 > n2) return 1;
    }
    return 0;
}

QVector<int> AutoUpdater::parseVersionNumbers(const QString& versionStr) {
    QVector<int> nums;
    QString clean = versionStr;
    if (clean.startsWith('v') || clean.startsWith('V'))
        clean = clean.mid(1);
    QStringList parts = clean.split('.', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        int num = part.toInt(&ok);
        if (ok)
            nums.append(num);
        else
            break; // stop at first non-numeric part
    }
    while (nums.size() < 3)
        nums.append(0);
    return nums;
}

void AutoUpdater::checkForUpdates(bool silent) {
    silentCheck_ = silent;
    if (checkReply_) {
        checkReply_->abort();
        checkReply_->deleteLater();
    }
    checkReply_ = nullptr;

    emit checkingStarted();

    QUrl url("https://api.github.com/repos/robin421/PDFMark/releases/latest");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "PDFMark-AutoUpdater/1.0");
    checkReply_ = networkManager_->get(request);
    connect(checkReply_, &QNetworkReply::finished, this, &AutoUpdater::onCheckFinished);
}

void AutoUpdater::startDownload(const QString& downloadUrl) {
    if (downloadReply_) {
        downloadReply_->abort();
        downloadReply_->deleteLater();
    }
    downloadReply_ = nullptr;

    // Determine where to save the zip file
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(tempDir);
    downloadDestPath_ = tempDir + "/PDFMark-Windows-x64.zip";

    // Remove any existing file
    QFile::remove(downloadDestPath_);

    downloadFile_ = new QFile(downloadDestPath_);
    if (!downloadFile_->open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot open file for writing:" << downloadDestPath_;
        emit downloadFailed(QString("Cannot open file for writing: %1").arg(downloadDestPath_));
        delete downloadFile_;
        downloadFile_ = nullptr;
        return;
    }

    QNetworkRequest request(downloadUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "PDFMark-AutoUpdater/1.0");
    downloadReply_ = networkManager_->get(request);
    connect(downloadReply_, &QNetworkReply::readyRead, this, &AutoUpdater::onDownloadReadyRead);
    connect(downloadReply_, &QNetworkReply::finished, this, &AutoUpdater::onDownloadFinished);
    connect(downloadReply_, &QNetworkReply::downloadProgress, this, &AutoUpdater::onDownloadProgress);
}

void AutoUpdater::cancelDownload() {
    if (downloadReply_) {
        downloadReply_->abort();
        downloadReply_->deleteLater();
        downloadReply_ = nullptr;
    }
    if (downloadFile_) {
        downloadFile_->close();
        downloadFile_->remove();
        delete downloadFile_;
        downloadFile_ = nullptr;
    }
    QFile::remove(downloadDestPath_);
}

bool AutoUpdater::applyUpdateAndRestart(const QString& zipFilePath) {
    // Create the batch script in the same directory as the zip file
    QString scriptDir = QFileInfo(zipFilePath).absolutePath();
    QString scriptPath = scriptDir + "/pdfmark_update.bat";

    // Get the current application directory
    QString appDir = QCoreApplication::applicationDirPath();

    // Build the batch script content
    QString scriptContent = QStringLiteral(
        "@echo off\n"
        "setlocal enabledelayedexpansion\n"
        "set ZIP_PATH=%~dp0PDFMark-Windows-x64.zip\n"
        "set TEMP_DIR=%TEMP%\\PDFMarkUpdate\n"
        "mkdir \"%TEMP_DIR%\" 2>nul\n"
        "cd /d \"%TEMP_DIR%\"\n"
        "echo 等待当前进程退出...\n"
        "timeout /t 5\n"
        "echo 解压 %ZIP_PATH%...\n"
        "powershell -Command \"Expand-Archive -Path '%ZIP_PATH%' -DestinationPath '.' -Force\"\n"
        "echo 覆盖文件...\n"
        "xcopy /E /Q /Y . ..\\.. >nul\n"
        "echo 启动新版本...\n"
        "start \"\" ..\\PdfMark.exe\n"
        "echo 更新完成。\n"
        "del /Q \"%TEMP_DIR%\\*.*\"\n"
        "rmdir \"%TEMP_DIR%\"\n"
        "del /Q \"%~f0\"\n"
    );

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot create update script:" << scriptPath;
        return false;
    }
    scriptFile.write(scriptContent.toUtf8());
    scriptFile.close();

    // Make sure the script is executable? Not needed on Windows .bat

    // Launch the batch script in detached mode
    bool started = QProcess::startDetached(scriptPath, QStringList());
    if (!started) {
        qWarning() << "Failed to start update script:" << scriptPath;
        return false;
    }

    // The batch script will wait for the current process to exit, then update and restart.
    // We should exit the current instance to allow the batch script to proceed.
    QCoreApplication::quit();
    return true;
}

void AutoUpdater::onCheckFinished() {
    if (!checkReply_) {
        emit checkFailed(QString("No reply object"));
        return;
    }

    if (checkReply_->error() != QNetworkReply::NoError) {
        emit checkFailed(checkReply_->errorString());
        checkReply_->deleteLater();
        checkReply_ = nullptr;
        return;
    }

    QByteArray data = checkReply_->readAll();
    checkReply_->deleteLater();
    checkReply_ = nullptr;

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit checkFailed(QString("Invalid JSON response"));
        return;
    }

    QJsonObject root = doc.object();
    QString tagName = root["tag_name"].toString();
    if (tagName.isEmpty()) {
        emit checkFailed(QString("No tag_name in release"));
        return;
    }

    // Compare with current version
    int cmp = compareVersions(tagName, QString("v") + PDFMARK_VERSION);
    if (cmp <= 0) {
        // No update available
        if (!silentCheck_) {
            emit noUpdateAvailable();
        }
        return;
    }

    // We have an update
    UpdateInfo info;
    info.versionTag = tagName;
    info.versionName = root["name"].toString();
    if (info.versionName.isEmpty())
        info.versionName = tagName;
    info.releaseNotes = root["body"].toString();
    info.publishedAt = root["published_at"].toString();

    // Find the asset for Windows x64 zip
    QJsonArray assets = root["assets"].toArray();
    for (const QJsonValue& val : assets) {
        if (!val.isObject()) continue;
        QJsonObject asset = val.toObject();
        QString name = asset["name"].toString();
        if (name.contains("Windows") && name.contains("x64") && name.endsWith(".zip", Qt::CaseInsensitive)) {
            info.assetName = name;
            info.downloadUrl = asset["browser_download_url"].toString();
            info.assetSize = asset["size"].toVariant().toLongLong();
            break;
        }
    }

    if (info.downloadUrl.isEmpty()) {
        emit checkFailed(QString("No suitable Windows x64 zip asset found"));
        return;
    }

    info.hasUpdate = true;
    emit updateAvailable(info);
}

void AutoUpdater::onDownloadReadyRead() {
    if (!downloadFile_ || !downloadReply_) return;
    downloadFile_->write(downloadReply_->readAll());
}

void AutoUpdater::onDownloadFinished() {
    if (downloadReply_->error() != QNetworkReply::NoError) {
        emit downloadFailed(downloadReply_->errorString());
        downloadReply_->deleteLater();
        downloadReply_ = nullptr;
        if (downloadFile_) {
            downloadFile_->close();
            downloadFile_->remove();
            delete downloadFile_;
            downloadFile_ = nullptr;
        }
        return;
    }

    if (downloadFile_) {
        downloadFile_->flush();
        downloadFile_->close();
    }
    downloadReply_->deleteLater();
    downloadReply_ = nullptr;

    emit downloadFinished(downloadDestPath_);
}

void AutoUpdater::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    emit downloadProgress(bytesReceived, bytesTotal);
}

} // namespace pdfmark