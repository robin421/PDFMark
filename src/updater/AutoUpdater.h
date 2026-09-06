// PDFMark - Lightweight auto-updater for GitHub Releases.
#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>

namespace pdfmark {

struct UpdateInfo {
    QString versionTag;      // e.g. "v1.1.0"
    QString versionName;     // e.g. "PDFMark v1.1.0"
    QString releaseNotes;    // Markdown/text from GitHub release body
    QString downloadUrl;     // Direct URL to platform zip/asset
    QString assetName;       // e.g. "PDFMark-Windows-x64.zip"
    qint64 assetSize = 0;    // Size in bytes
    QString publishedAt;     // ISO timestamp
    bool hasUpdate = false;
};

class AutoUpdater : public QObject {
    Q_OBJECT
public:
    explicit AutoUpdater(QObject* parent = nullptr);
    ~AutoUpdater() override;

    // Compare two version strings (e.g. "v1.0.9" and "v1.1.0")
    // Returns 1 if v1 > v2, -1 if v1 < v2, 0 if equal.
    static int compareVersions(const QString& v1, const QString& v2);

    // Parse version string into numbers [major, minor, patch]
    static QVector<int> parseVersionNumbers(const QString& versionStr);

    // Asynchronously check for update via GitHub API
    void checkForUpdates(bool silent = false);

    // Start downloading the asset specified by updateInfo
    void startDownload(const QString& downloadUrl);

    // Cancel ongoing download
    void cancelDownload();

    // Apply the downloaded zip update and restart the application
    // Generates a self-deleting platform script (PowerShell/bat on Windows)
    bool applyUpdateAndRestart(const QString& zipFilePath);

signals:
    void checkingStarted();
    void updateAvailable(const UpdateInfo& info);
    void noUpdateAvailable();
    void checkFailed(const QString& errorMessage);

    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(const QString& localZipPath);
    void downloadFailed(const QString& errorMessage);

private slots:
    void onCheckFinished();
    void onDownloadReadyRead();
    void onDownloadFinished();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
    QNetworkAccessManager* networkManager_ = nullptr;
    QNetworkReply* checkReply_ = nullptr;
    QNetworkReply* downloadReply_ = nullptr;
    QFile* downloadFile_ = nullptr;
    QString downloadDestPath_;
    bool silentCheck_ = false;
};

} // namespace pdfmark
