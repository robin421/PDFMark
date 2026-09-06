// PDFMark - Update notification dialog implementation.
#include "common/Common.h"
#include "updater/UpdateDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTextBrowser>
#include <QFont>
#include <QApplication>
#include <QMessageBox>

namespace pdfmark {

UpdateDialog::UpdateDialog(AutoUpdater* updater, QWidget* parent)
    : QDialog(parent)
    , updater_(updater)
{
    setWindowTitle("检查更新");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setMinimumWidth(480);
    setModal(true);

    // Header label
    versionLabel_ = new QLabel(this);
    versionLabel_->setAlignment(Qt::AlignCenter);
    QFont titleFont = versionLabel_->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    versionLabel_->setFont(titleFont);

    // Metadata
    sizeLabel_ = new QLabel(this);
    sizeLabel_->setAlignment(Qt::AlignCenter);
    publishedLabel_ = new QLabel(this);
    publishedLabel_->setAlignment(Qt::AlignCenter);

    // Release notes
    notesBrowser_ = new QTextBrowser(this);
    notesBrowser_->setMaximumHeight(200);
    notesBrowser_->setReadOnly(true);
    notesBrowser_->setPlaceholderText("暂无更新说明");

    // Status
    statusLabel_ = new QLabel(this);
    statusLabel_->setAlignment(Qt::AlignCenter);

    // Progress bar
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setVisible(false);

    // Buttons
    updateBtn_ = new QPushButton("⬇ 一键更新", this);
    updateBtn_->setEnabled(false);
    closeBtn_ = new QPushButton("稍后再说", this);
    cancelBtn_ = new QPushButton("取消下载", this);
    cancelBtn_->setVisible(false);

    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(updateBtn_);
    btnRow->addWidget(cancelBtn_);
    btnRow->addWidget(closeBtn_);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(versionLabel_);
    mainLayout->addWidget(sizeLabel_);
    mainLayout->addWidget(publishedLabel_);
    mainLayout->addSpacing(8);
    mainLayout->addWidget(new QLabel("更新说明:", this));
    mainLayout->addWidget(notesBrowser_);
    mainLayout->addSpacing(8);
    mainLayout->addWidget(statusLabel_);
    mainLayout->addWidget(progressBar_);
    mainLayout->addLayout(btnRow);

    // Connect signals
    connect(updateBtn_, &QPushButton::clicked, this, &UpdateDialog::onApplyUpdate);
    connect(closeBtn_, &QPushButton::clicked, this, &UpdateDialog::onClose);
    connect(cancelBtn_, &QPushButton::clicked, this, &UpdateDialog::onCancelDownload);

    // Forward AutoUpdater signals
    connect(updater_, &AutoUpdater::checkingStarted, this, &UpdateDialog::onCheckStarted);
    connect(updater_, &AutoUpdater::updateAvailable, this, &UpdateDialog::onUpdateAvailable);
    connect(updater_, &AutoUpdater::noUpdateAvailable, this, &UpdateDialog::onNoUpdateAvailable);
    connect(updater_, &AutoUpdater::checkFailed, this, &UpdateDialog::onCheckFailed);
    connect(updater_, &AutoUpdater::downloadProgress, this, &UpdateDialog::onDownloadProgress);
    connect(updater_, &AutoUpdater::downloadFinished, this, &UpdateDialog::onDownloadFinished);
    connect(updater_, &AutoUpdater::downloadFailed, this, &UpdateDialog::onDownloadFailed);

    // Start checking
    updater_->checkForUpdates(false);
}

UpdateDialog::~UpdateDialog() = default;

void UpdateDialog::setStatus(const QString& msg) {
    statusLabel_->setText(msg);
}

QString UpdateDialog::formatSize(qint64 bytes) const {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

void UpdateDialog::onCheckStarted() {
    versionLabel_->setText("正在检查更新...");
    sizeLabel_->clear();
    publishedLabel_->clear();
    notesBrowser_->clear();
    setStatus("正在连接 GitHub...");
    progressBar_->setVisible(false);
    updateBtn_->setEnabled(false);
}

void UpdateDialog::onUpdateAvailable(const UpdateInfo& info) {
    currentInfo_ = info;
    versionLabel_->setText(QString("发现新版本: %1").arg(info.versionTag));
    sizeLabel_->setText(QString("下载大小: %1").arg(formatSize(info.assetSize)));
    publishedLabel_->setText(QString("发布日期: %1").arg(info.publishedAt.left(10)));
    notesBrowser_->setPlainText(info.releaseNotes);
    setStatus(QString("发现新版本 %1，点击「一键更新」立即升级").arg(info.versionTag));
    progressBar_->setVisible(false);
    updateBtn_->setEnabled(true);
    updateBtn_->setFocus();
}

void UpdateDialog::onNoUpdateAvailable() {
    versionLabel_->setText("当前已是最新版本");
    setStatus(QString("当前版本 %1 已是最新，无需更新。").arg(PDFMARK_VERSION));
}

void UpdateDialog::onCheckFailed(const QString& error) {
    versionLabel_->setText("检查更新失败");
    setStatus(QString("无法检查更新: %1").arg(error));
    updateBtn_->setEnabled(false);
}

void UpdateDialog::onDownloadProgress(qint64 received, qint64 total) {
    if (total > 0) {
        progressBar_->setVisible(true);
        int pct = int(100.0 * received / total);
        progressBar_->setValue(pct);
        setStatus(QString("下载中... %1 / %2 (%3%)")
                      .arg(formatSize(received))
                      .arg(formatSize(total))
                      .arg(pct));
    } else {
        setStatus(QString("下载中... %1").arg(formatSize(received)));
    }
}

void UpdateDialog::onDownloadFinished(const QString& zipPath) {
    currentZipPath_ = zipPath;
    progressBar_->setVisible(false);
    setStatus("下载完成！正在准备更新...");
    updateBtn_->setEnabled(false);
    closeBtn_->setEnabled(false);

    // Apply update and restart
    bool ok = updater_->applyUpdateAndRestart(zipPath);
    if (!ok) {
        QMessageBox::critical(this, "更新失败",
            QString("无法应用更新脚本。请手动解压 %1 到安装目录。").arg(zipPath));
        closeBtn_->setEnabled(true);
    }
    // If ok, process quits and batch script handles the rest
}

void UpdateDialog::onDownloadFailed(const QString& error) {
    setStatus(QString("下载失败: %1").arg(error));
    QMessageBox::warning(this, "下载失败", error);
    cancelBtn_->setVisible(false);
    updateBtn_->setEnabled(true);
}

void UpdateDialog::onApplyUpdate() {
    if (currentInfo_.downloadUrl.isEmpty()) {
        QMessageBox::warning(this, "错误", "无效的下载链接");
        return;
    }
    cancelBtn_->setVisible(true);
    updateBtn_->setEnabled(false);
    setStatus(QString("开始下载 %1 ...").arg(currentInfo_.assetName));
    updater_->startDownload(currentInfo_.downloadUrl);
}

void UpdateDialog::onClose() {
    close();
}

void UpdateDialog::onCancelDownload() {
    updater_->cancelDownload();
    cancelBtn_->setVisible(false);
    updateBtn_->setEnabled(true);
    progressBar_->setVisible(false);
    setStatus("下载已取消");
}

} // namespace pdfmark