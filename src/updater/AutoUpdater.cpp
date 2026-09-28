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

QString AutoUpdater::buildUpdateScript(const QString& appDir, const QString& zipPath) {
    // The previous version used a hard-coded RELATIVE path (`xcopy . ..\..` and
    // `start ..\PdfMark.exe`). That relative path is resolved against %TEMP%,
    // NOT against the app folder, so the update was installed into the wrong
    // place and then tried to launch a non-existent exe -- silently, because
    // all output was redirected to nul. Everything now uses the real absolute
    // application directory.
    //
    // NOTE: placeholders are substituted with QString::replace (never arg()),
    // because arg() would mangle the %%i batch loop variable.
    QString script = QStringLiteral(
        "@echo off\n"
        "chcp 65001 >nul\n"
        "setlocal\n"
        "set \"APP_DIR=%APPDIR%\"\n"
        "set \"ZIP_PATH=%ZIPPATH%\"\n"
        "set \"TEMP_DIR=%TEMP%\\PDFMarkUpdate\"\n"
        "if not exist \"%TEMP_DIR%\" mkdir \"%TEMP_DIR%\"\n"
        "cd /d \"%TEMP_DIR%\"\n"
        "\n"
        "echo [1/5] 等待 PDFMark 退出...\n"
        "taskkill /IM PdfMark.exe /F >nul 2>nul\n"
        "for /L %%i in (1,1,20) do (\n"
        "    tasklist /FI \"IMAGENAME eq PdfMark.exe\" 2>nul | find /I \"PdfMark.exe\" >nul\n"
        "    if errorlevel 1 goto :killed\n"
        "    timeout /t 1 /nobreak >nul\n"
        ")\n"
        ":killed\n"
        "\n"
        "echo [2/5] 解压更新包...\n"
        "powershell -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '%ZIPPATH%' -DestinationPath '.' -Force\"\n"
        "if errorlevel 1 goto :failed\n"
        "if not exist \"%APP_DIR%\\PdfMark.exe\" goto :failed\n"
        "\n"
        "echo [3/5] 写入 %APPDIR% ...\n"
        "xcopy /E /Y /I /Q \".\" \"%APP_DIR%\\\" >nul\n"
        "if errorlevel 1 goto :readonly\n"
        "\n"
        "echo [4/5] 启动新版本...\n"
        "start \"\" \"%APP_DIR%\\PdfMark.exe\"\n"
        "echo [5/5] 更新完成。\n"
        "cd /d \"%TEMP%\"\n"
        "rd /S /Q \"%TEMP_DIR%\" >nul 2>nul\n"
        "del /Q \"%~f0\" >nul 2>nul\n"
        "exit /b 0\n"
        "\n"
        ":readonly\n"
        "echo.\n"
        "echo [失败] 没有权限写入程序目录：\n"
        "echo         %APPDIR%\n"
        "echo 请把 PDFMark 放在有写入权限的目录（例如 D:\\PDFMark），\n"
        "echo 或以管理员身份运行本程序后重新检查更新。\n"
        "goto :cleanup\n"
        "\n"
        ":failed\n"
        "echo.\n"
        "echo [失败] 更新包解压失败或内容不完整，本次未做任何修改。\n"
        "\n"
        ":cleanup\n"
        "cd /d \"%TEMP%\"\n"
        "rd /S /Q \"%TEMP_DIR%\" >nul 2>nul\n"
        "del /Q \"%~f0\" >nul 2>nul\n"
        "echo.\n"
        "pause\n"
        "exit /b 1\n");

    script.replace("%APPDIR%", QDir::toNativeSeparators(appDir));
    script.replace("%ZIPPATH%", QDir::toNativeSeparators(zipPath));
    return script;
}

bool AutoUpdater::applyUpdateAndRestart(const QString& zipFilePath) {
    // Place the helper script next to the downloaded zip so both live in temp.
    const QString scriptDir = QFileInfo(zipFilePath).absolutePath();
    const QString scriptPath = scriptDir + "/pdfmark_update.bat";

    // This is the folder the running exe lives in - the update is installed
    // there (portable, no installer, no admin rights required).
    const QString appDir = QCoreApplication::applicationDirPath();
    if (appDir.isEmpty()) {
        qWarning() << "Cannot resolve application directory for update";
        return false;
    }

    const QString scriptContent = buildUpdateScript(appDir, zipFilePath);

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Cannot create update script:" << scriptPath;
        return false;
    }
    // Write as UTF-8 with BOM-free plain text; chcp 65001 inside handles display.
    scriptFile.write(scriptContent.toUtf8());
    scriptFile.close();

    // Launch detached so it survives our own exit.
    const bool started = QProcess::startDetached(scriptPath, QStringList());
    if (!started) {
        qWarning() << "Failed to start update script:" << scriptPath;
        return false;
    }

    // The script taskkills us and relaunches; quit so it can proceed.
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