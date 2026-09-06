// PDFMark - Update notification and progress dialog.
#pragma once

#include "updater/AutoUpdater.h"
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTextBrowser>

namespace pdfmark {

class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(AutoUpdater* updater, QWidget* parent = nullptr);
    ~UpdateDialog() override;

private slots:
    void onCheckStarted();
    void onUpdateAvailable(const UpdateInfo& info);
    void onNoUpdateAvailable();
    void onCheckFailed(const QString& error);
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished(const QString& zipPath);
    void onDownloadFailed(const QString& error);
    void onApplyUpdate();
    void onClose();
    void onCancelDownload();

private:
    void setStatus(const QString& msg);
    QString formatSize(qint64 bytes) const;

    AutoUpdater* updater_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* versionLabel_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QLabel* publishedLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* updateBtn_ = nullptr;
    QPushButton* closeBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QTextBrowser* notesBrowser_ = nullptr;

    UpdateInfo currentInfo_;
    QString currentZipPath_;
};

} // namespace pdfmark