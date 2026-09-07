// PDFMark - Main GUI Window implementation.
#include "ui/MainWindow.h"
#include "ui/PasswordDialog.h"
#include "diagnostics/Diagnostics.h"
#include "pdf/PdfDocument.h"
#include "watermark/WatermarkRenderer.h"
#include "updater/AutoUpdater.h"
#include "updater/UpdateDialog.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QStatusBar>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QPushButton>
#include <QLineEdit>
#include <QScrollArea>
#include <QDesktopServices>
#include <QStandardPaths>
namespace pdfmark {

// ── WatermarkRow ────────────────────────────────────────────────────────────
WatermarkRow::WatermarkRow(const QString& initialText, int index, QWidget* parent)
    : QWidget(parent), index_(index) {
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);

    lineEdit_ = new QLineEdit(this);
    lineEdit_->setText(initialText);
    lineEdit_->setPlaceholderText("水印文字（例如：机密-张三）");
    h->addWidget(lineEdit_, 1);

    removeBtn_ = new QPushButton("×", this);
    removeBtn_->setFixedSize(28, 28);
    removeBtn_->setStyleSheet("color: #cc0000; font-weight: bold; font-size: 16px;");
    removeBtn_->setToolTip("删除此水印");
    h->addWidget(removeBtn_);

    connect(lineEdit_, &QLineEdit::textChanged, this, [this](const QString&) {
        emit textChanged();
    });
    connect(removeBtn_, &QPushButton::clicked, this, [this]() {
        emit removeRequested(index_);
    });
}

QString WatermarkRow::text() const {
    return lineEdit_->text();
}

void WatermarkRow::setText(const QString& t) {
    lineEdit_->setText(t);
}
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();
    setupConnections();
    setWindowTitle("PDF 水印固化工具");
    resize(1100, 720);
    updateUiState(false);

    // Auto-update: create updater, add Help menu, and do a silent background check
    autoUpdater_ = new AutoUpdater(this);

    QMenu* helpMenu = menuBar()->addMenu("帮助(&H)");
    QAction* checkUpdateAct = helpMenu->addAction("检查更新(&U)...");
    connect(checkUpdateAct, &QAction::triggered, this, &MainWindow::onCheckForUpdates);

    QAction* openLogAct = helpMenu->addAction("打开日志目录(&L)...");
    connect(openLogAct, &QAction::triggered, this, []() {
        QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
    });
    // Background silent check 2 seconds after startup
    QTimer::singleShot(2000, this, [this]() {
        connect(autoUpdater_, &AutoUpdater::updateAvailable,
                this, &MainWindow::onSilentUpdateAvailable);
        autoUpdater_->checkForUpdates(true);
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // 1. Top action buttons
    auto* topBox = new QGroupBox("文件列表操作", this);
    auto* topLayout = new QHBoxLayout(topBox);
    topLayout->setContentsMargins(8, 8, 8, 8);
    topLayout->setSpacing(8);
    auto* addFilesBtn = new QPushButton("添加 PDF 文件...", topBox);
    auto* addFolderBtn = new QPushButton("添加文件夹...", topBox);
    auto* clearBtn = new QPushButton("清空列表", topBox);
    auto* removeSelBtn = new QPushButton("删除选中文件", topBox);
    auto* openFolderBtn = new QPushButton("打开生成文件夹", topBox);
    openFolderBtn->setEnabled(false);
    openFolderBtn->setStyleSheet("padding: 6px 14px;");
    openFolderBtn->setToolTip("在 Finder 中打开上次生成的 PDF 所在目录");
    connect(openFolderBtn, &QPushButton::clicked, this, &MainWindow::onOpenOutputFolder);

    addFilesBtn->setStyleSheet("padding: 6px 14px; font-weight: bold;");
    addFolderBtn->setStyleSheet("padding: 6px 14px;");
    clearBtn->setStyleSheet("padding: 6px 14px;");
    removeSelBtn->setStyleSheet("padding: 6px 14px; color: #cc0000;");

    topLayout->addWidget(addFilesBtn);
    topLayout->addWidget(addFolderBtn);
    topLayout->addWidget(clearBtn);
    topLayout->addWidget(removeSelBtn);
    topLayout->addStretch();
    topLayout->addWidget(openFolderBtn);
    openFolderBtn_ = openFolderBtn;
    mainLayout->addWidget(topBox);

    connect(addFilesBtn, &QPushButton::clicked, this, &MainWindow::onAddFiles);
    connect(addFolderBtn, &QPushButton::clicked, this, &MainWindow::onAddFolder);
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearFiles);
    connect(removeSelBtn, &QPushButton::clicked, this, &MainWindow::onRemoveSelectedFile);

    // 2. Middle Splitter (Left: Table, Right: Params + Preview)
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left: File Table
    auto* leftContainer = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    fileTable_ = new QTableWidget(0, 4, leftContainer);
    fileTable_->setHorizontalHeaderLabels({"文件名", "页数", "状态", "完整路径"});
    fileTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    fileTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    fileTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    fileTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    fileTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fileTable_->setAlternatingRowColors(true);
    leftLayout->addWidget(fileTable_);

    splitter->addWidget(leftContainer);

    // Right: per-file watermark panel + Params + Preview
    auto* rightContainer = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);


    // Watermark rows scroll area
    watermarkScrollArea_ = new QScrollArea(rightContainer);
    watermarkScrollArea_->setWidgetResizable(true);
    watermarkScrollArea_->setFrameShape(QFrame::NoFrame);
    watermarkContainer_ = new QWidget(watermarkScrollArea_);
    watermarkLayout_ = new QVBoxLayout(watermarkContainer_);
    watermarkLayout_->setContentsMargins(0, 0, 0, 0);
    watermarkLayout_->setSpacing(4);
    watermarkLayout_->addStretch();
    watermarkScrollArea_->setWidget(watermarkContainer_);
    rightLayout->addWidget(watermarkScrollArea_, 1);

    addWatermarkBtn_ = new QPushButton("+ 添加水印", rightContainer);
    addWatermarkBtn_->setStyleSheet("padding: 6px 14px; font-weight: bold;");
    auto* previewBtn = new QPushButton("🔍 预览效果", rightContainer);
    previewBtn->setStyleSheet("padding: 6px 14px;");
    previewBtn->setToolTip("预览当前水印样式在 PDF 页面上的实际效果");
    connect(previewBtn, &QPushButton::clicked, this, &MainWindow::onPreviewWatermark);
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);
    btnRow->addWidget(addWatermarkBtn_);
    btnRow->addWidget(previewBtn);
    btnRow->addStretch();
    rightLayout->addLayout(btnRow);

    // Watermark Params Group (shared style params)
    auto* paramGroup = new QGroupBox("水印样式设置", rightContainer);
    auto* grid = new QGridLayout(paramGroup);
    grid->setContentsMargins(10, 12, 10, 10);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    int row = 0;
    grid->addWidget(new QLabel("颜色深浅:"), row, 0);
    depthSlider_ = new QSlider(Qt::Horizontal, paramGroup);
    depthSlider_->setRange(5, 60);
    depthSlider_->setValue(15);
    depthValueLabel_ = new QLabel("15% (适中)", paramGroup);
    depthValueLabel_->setFixedWidth(75);
    grid->addWidget(depthSlider_, row, 1);
    grid->addWidget(depthValueLabel_, row, 2);
    row++;

    // Watermark Rotation (degrees)
    grid->addWidget(new QLabel("倾斜角度:"), row, 0);
    rotationSpin_ = new QDoubleSpinBox(paramGroup);
    rotationSpin_->setRange(-90.0, 90.0);
    rotationSpin_->setSingleStep(5.0);
    rotationSpin_->setValue(-35.0);
    rotationSpin_->setSuffix("°");
    rotationSpin_->setToolTip("水印倾斜角度，支持 -90° 到 90°，默认 -35°（左下到右上）");
    grid->addWidget(rotationSpin_, row, 1, 1, 2);
    row++;

    // Watermark Font Family
    grid->addWidget(new QLabel("水印字体:"), row, 0);
    fontCombo_ = new QFontComboBox(paramGroup);
    fontCombo_->setCurrentFont(QFont("Arial"));
    fontCombo_->setToolTip("选择水印文字所使用的字体");
    grid->addWidget(fontCombo_, row, 1, 1, 2);
    row++;

    // Font Style: Bold & Italic
    grid->addWidget(new QLabel("字体样式:"), row, 0);
    auto* styleWidget = new QWidget(paramGroup);
    auto* styleLayout = new QHBoxLayout(styleWidget);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    styleLayout->setSpacing(12);
    boldCheck_ = new QCheckBox("加粗", styleWidget);
    boldCheck_->setChecked(true);
    italicCheck_ = new QCheckBox("斜体", styleWidget);
    italicCheck_->setChecked(false);
    styleLayout->addWidget(boldCheck_);
    styleLayout->addWidget(italicCheck_);
    styleLayout->addStretch();
    grid->addWidget(styleWidget, row, 1, 1, 2);
    row++;

    // Performance mode
    grid->addWidget(new QLabel("性能模式:"), row, 0);
    perfCombo_ = new QComboBox(paramGroup);
    perfCombo_->addItem("保守运行（不卡电脑）", static_cast<int>(PerformanceMode::Low));
    perfCombo_->addItem("日常推荐（推荐）", static_cast<int>(PerformanceMode::Normal));
    perfCombo_->addItem("火力全开（极速处理）", static_cast<int>(PerformanceMode::High));
    perfCombo_->setCurrentIndex(1);
    perfCombo_->setToolTip("保守运行：仅用单线程，适合边办公边处理，速度较慢但不影响电脑其他操作\n"
                           "日常推荐：多线程平衡模式，速度与资源占用均衡，适合大多数场景\n"
                           "火力全开：全核并发，处理最快，但电脑会明显变慢、发热增加");
    grid->addWidget(perfCombo_, row, 1, 1, 2);
    row++;
    // Output directory
    grid->addWidget(new QLabel("输出目录:"), row, 0);
    outputDirEdit_ = new QLineEdit(paramGroup);
    outputDirEdit_->setPlaceholderText("默认与源文件相同目录");
    auto* browseBtn = new QPushButton("选择...", paramGroup);
    browseBtn->setStyleSheet("padding: 4px 10px;");
    grid->addWidget(outputDirEdit_, row, 1);
    grid->addWidget(browseBtn, row, 2);
    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::onSelectOutputDir);
    rightLayout->addWidget(paramGroup);

    splitter->addWidget(rightContainer);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    mainLayout->addWidget(splitter, 1);

    // 3. Bottom status & execution controls (no GroupBox frame — flat design)
    auto* statusCard = new QFrame(centralWidget);
    statusCard->setFrameShape(QFrame::StyledPanel);
    statusCard->setStyleSheet("QFrame { background-color: #f5f7fa; border-radius: 6px; }");
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(12, 8, 12, 8);
    statusLayout->setSpacing(6);

    // Progress bars row
    auto* progRow = new QHBoxLayout();
    progRow->addWidget(new QLabel("单文件页进度:"));
    pageProgressBar_ = new QProgressBar(statusCard);
    pageProgressBar_->setRange(0, 100);
    pageProgressBar_->setValue(0);
    pageProgressBar_->setTextVisible(true);
    progRow->addWidget(pageProgressBar_);
    progRow->addSpacing(16);
    progRow->addWidget(new QLabel("总体队列进度:"));
    totalProgressBar_ = new QProgressBar(statusCard);
    totalProgressBar_->setRange(0, 100);
    totalProgressBar_->setValue(0);
    totalProgressBar_->setTextVisible(true);
    progRow->addWidget(totalProgressBar_);
    statusLayout->addLayout(progRow);
    // Control row
    auto* ctlRow = new QHBoxLayout();
    statusLabel_ = new QLabel("就绪。请添加 PDF 文件后配置水印并开始批量固化。", statusCard);
    statusLabel_->setStyleSheet("color: #555555; font-size: 13px;");
    ctlRow->addWidget(statusLabel_, 1);

    cancelBtn_ = new QPushButton("取消", statusCard);
    cancelBtn_->setEnabled(false);
    cancelBtn_->setStyleSheet("padding: 5px 12px;");
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::onCancelClicked);
    ctlRow->addWidget(cancelBtn_);

    startSelectedBtn_ = new QPushButton("生成所选", statusCard);
    startSelectedBtn_->setEnabled(false);
    startSelectedBtn_->setStyleSheet(
        "QPushButton { background-color: #107c10; color: white; font-weight: bold; "
        "padding: 7px 16px; border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background-color: #0b5c0b; }"
        "QPushButton:pressed { background-color: #094509; }"
        "QPushButton:disabled { background-color: #cccccc; color: #888888; }");
    connect(startSelectedBtn_, &QPushButton::clicked, this, &MainWindow::onStartSelectedClicked);
    ctlRow->addWidget(startSelectedBtn_);

    startAllBtn_ = new QPushButton("生成全部", statusCard);
    startAllBtn_->setStyleSheet(
        "QPushButton { background-color: #0078d4; color: white; font-weight: bold; "
        "padding: 7px 16px; border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background-color: #106ebe; }"
        "QPushButton:pressed { background-color: #005a9e; }"
        "QPushButton:disabled { background-color: #cccccc; color: #888888; }");
    connect(startAllBtn_, &QPushButton::clicked, this, &MainWindow::onStartAllClicked);
    ctlRow->addWidget(startAllBtn_);

    statusLayout->addLayout(ctlRow);
    mainLayout->addWidget(statusCard);
}

void MainWindow::setupConnections() {
    connect(depthSlider_, &QSlider::valueChanged, this, [this](int val) {
        QString hint = "适中";
        if (val <= 8) hint = "极浅";
        else if (val <= 12) hint = "偏浅";
        else if (val <= 25) hint = "适中";
        else if (val <= 40) hint = "偏深";
        else hint = "深色";
        depthValueLabel_->setText(QString("%1% (%2)").arg(val).arg(hint));
    });

    // File selection change
    connect(fileTable_, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::onFileSelectionChanged);

    // Add watermark row button
    connect(addWatermarkBtn_, &QPushButton::clicked, this, &MainWindow::addWatermarkRow);

    // TaskManager signals
    connect(&taskManager_, &TaskManager::fileStarted, this, &MainWindow::onFileStarted);
    connect(&taskManager_, &TaskManager::pageProgress, this, &MainWindow::onPageProgress);
    connect(&taskManager_, &TaskManager::fileProgress, this, &MainWindow::onFileProgress);
    connect(&taskManager_, &TaskManager::fileFinished, this, &MainWindow::onFileFinished);
    connect(&taskManager_, &TaskManager::allFinished, this, &MainWindow::onAllFinished);
    connect(&taskManager_, &TaskManager::cancelled, this, &MainWindow::onCancelled);
    connect(&taskManager_, &TaskManager::passwordRequired, this, &MainWindow::onPasswordRequired);
    connect(&taskManager_, &TaskManager::errorOccurred, this, [this](const QString& msg) {
        QMessageBox::critical(this, "错误", "任务处理发生异常：\n" + msg);
    });
}

WatermarkConfig MainWindow::currentConfig() const {
    WatermarkConfig cfg;
    cfg.fontSizePt = 24;
    cfg.rotationDegrees = rotationSpin_->value();
    cfg.fontFamily = fontCombo_->currentFont().family().toStdString();
    cfg.fontBold = boldCheck_->isChecked();
    cfg.fontItalic = italicCheck_->isChecked();
    cfg.dpi = 200;
    cfg.jpegQuality = 85;
    cfg.colorHex = "#808080";
    cfg.opacity = depthSlider_->value() / 100.0;
    // Get first visible text row, or saved text for currently selected file
    for (int i = 0; i < watermarkLayout_->count(); ++i) {
        auto* item = watermarkLayout_->itemAt(i);
        if (auto* row = qobject_cast<WatermarkRow*>(item->widget())) {
            QString t = row->text().trimmed();
            if (!t.isEmpty()) {
                cfg.text = t.toStdString();
                return cfg;
            }
        }
    }

    cfg.text = "";
    return cfg;
}


void MainWindow::saveCurrentWatermarks() {
    if (currentSelectedFile_.isEmpty()) return;
    std::vector<WatermarkConfig> configs;
    for (int i = 0; i < watermarkLayout_->count(); ++i) {
        auto* item = watermarkLayout_->itemAt(i);
        if (auto* row = qobject_cast<WatermarkRow*>(item->widget())) {
            QString t = row->text().trimmed();
            if (!t.isEmpty()) {
                WatermarkConfig cfg;
                cfg.text = t.toStdString();
                // Inherit style from current per-PDF config or use defaults
                auto itStyle = perPdfConfigMap_.find(currentSelectedFile_);
                if (itStyle != perPdfConfigMap_.end()) {
                    cfg.fontFamily = itStyle->second.fontFamily;
                    cfg.fontBold = itStyle->second.fontBold;
                    cfg.fontItalic = itStyle->second.fontItalic;
                    cfg.fontSizePt = itStyle->second.fontSizePt;
                    cfg.rotationDegrees = itStyle->second.rotationDegrees;
                    cfg.colorHex = itStyle->second.colorHex;
                    cfg.opacity = itStyle->second.opacity;
                    cfg.dpi = itStyle->second.dpi;
                    cfg.jpegQuality = itStyle->second.jpegQuality;
                } else {
                    // Defaults from live UI controls
                    cfg.fontFamily = fontCombo_->currentFont().family().toStdString();
                    cfg.fontBold = boldCheck_->isChecked();
                    cfg.fontItalic = italicCheck_->isChecked();
                    cfg.fontSizePt = 24;
                    cfg.rotationDegrees = rotationSpin_->value();
                    cfg.colorHex = "#808080";
                    cfg.opacity = depthSlider_->value() / 100.0;
                    cfg.dpi = 200;
                    cfg.jpegQuality = 85;
                }
                configs.push_back(cfg);
            }
        }
    }
    fileWatermarkConfigs_[currentSelectedFile_] = configs;
    // Sync current UI style settings into perPdfConfigMap_ so that buildAllConfigs uses live values
    auto itSync = perPdfConfigMap_.find(currentSelectedFile_);
    if (itSync != perPdfConfigMap_.end()) {
        itSync->second.fontFamily = fontCombo_->currentFont().family().toStdString();
        itSync->second.fontBold = boldCheck_->isChecked();
        itSync->second.fontItalic = italicCheck_->isChecked();
        itSync->second.fontSizePt = 24;
        itSync->second.rotationDegrees = rotationSpin_->value();
        itSync->second.colorHex = "#808080";
        itSync->second.opacity = depthSlider_->value() / 100.0;
        itSync->second.dpi = 200;
        itSync->second.jpegQuality = 85;
    } else {
        WatermarkConfig defaultCfg;
        defaultCfg.fontFamily = fontCombo_->currentFont().family().toStdString();
        defaultCfg.fontBold = boldCheck_->isChecked();
        defaultCfg.fontItalic = italicCheck_->isChecked();
        defaultCfg.fontSizePt = 24;
        defaultCfg.rotationDegrees = rotationSpin_->value();
        defaultCfg.colorHex = "#808080";
        defaultCfg.opacity = depthSlider_->value() / 100.0;
        defaultCfg.dpi = 200;
        defaultCfg.jpegQuality = 85;
        perPdfConfigMap_[currentSelectedFile_] = defaultCfg;
    }
}
void MainWindow::loadWatermarksForSelectedFile() {
    // Clear existing rows
    QLayoutItem* child;
    while ((child = watermarkLayout_->takeAt(0)) != nullptr) {
        if (child->widget()) {
            delete child->widget();
        }
        delete child;
    }

    if (currentSelectedFile_.isEmpty()) {
        addWatermarkBtn_->setEnabled(false);
        return;
    }
    addWatermarkBtn_->setEnabled(true);
    // Sync style controls from per-PDF config or defaults
    auto itStyle = perPdfConfigMap_.find(currentSelectedFile_);
    if (itStyle != perPdfConfigMap_.end()) {
        const auto& cfg = itStyle->second;
        rotationSpin_->setValue(cfg.rotationDegrees);
        fontCombo_->setCurrentFont(QFont(cfg.fontFamily.empty() ? "Arial" : cfg.fontFamily.c_str()));
        boldCheck_->setChecked(cfg.fontBold);
        italicCheck_->setChecked(cfg.fontItalic);
    } else {
        rotationSpin_->setValue(-35.0);
        fontCombo_->setCurrentFont(QFont("Arial"));
        boldCheck_->setChecked(true);
        italicCheck_->setChecked(false);
    }

    // Load existing watermark configs, or leave empty
    auto it = fileWatermarkConfigs_.find(currentSelectedFile_);
    const std::vector<WatermarkConfig> emptyList;
    const auto& cfgs = (it != fileWatermarkConfigs_.end()) ? it->second : emptyList;
    for (const auto& cfg : cfgs) {
        int idx = nextWatermarkIndex_++;
        auto* row = new WatermarkRow(QString::fromStdString(cfg.text), idx, watermarkContainer_);
        connect(row, &WatermarkRow::textChanged, this, &MainWindow::onWatermarkTextChanged);
        connect(row, &WatermarkRow::removeRequested, this, &MainWindow::removeWatermarkRow);
        watermarkLayout_->addWidget(row);
    }
    watermarkLayout_->addStretch();
}
void MainWindow::addWatermarkRow() {
    if (currentSelectedFile_.isEmpty()) return;
    int idx = nextWatermarkIndex_++;
    auto* row = new WatermarkRow("", idx, watermarkContainer_);
    connect(row, &WatermarkRow::textChanged, this, &MainWindow::onWatermarkTextChanged);
    connect(row, &WatermarkRow::removeRequested, this, &MainWindow::removeWatermarkRow);

    // Insert before the trailing stretch item
    int count = watermarkLayout_->count();
    watermarkLayout_->insertWidget(std::max(0, count - 1), row);
    saveCurrentWatermarks();
}
void MainWindow::removeWatermarkRow(int index) {
    for (int i = 0; i < watermarkLayout_->count(); ++i) {
        auto* item = watermarkLayout_->itemAt(i);
        if (auto* row = qobject_cast<WatermarkRow*>(item->widget())) {
            if (row->index() == index) {
                delete row;
                break;
            }
        }
    }
    saveCurrentWatermarks();
}

void MainWindow::onWatermarkTextChanged() {
    saveCurrentWatermarks();
}

void MainWindow::onFileSelectionChanged() {
    saveCurrentWatermarks();

    auto items = fileTable_->selectedItems();
    if (items.isEmpty()) {
        currentSelectedFile_.clear();
    } else {
        int row = fileTable_->row(items.first());
        auto* item = fileTable_->item(row, 3);
        currentSelectedFile_ = item ? item->text() : QString();
    }
    loadWatermarksForSelectedFile();
    updateUiState(false);
}

void MainWindow::onAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, "选择 PDF 文件", QString(), "PDF 文件 (*.pdf)"
    );
    if (files.isEmpty()) return;

    bool wasEmpty = fileTable_->rowCount() == 0;
    for (const auto& file : files) {
        try {
            fs::path p = qstringToPath(file);
            taskManager_.addFile(p);

            int row = fileTable_->rowCount();
            fileTable_->insertRow(row);
            QFileInfo fi(file);
            fileTable_->setItem(row, 0, new QTableWidgetItem(fi.fileName()));

            int pages = 0;
            try {
                auto docRef = PdfDocument::open(p);
                pages = PdfDocument::pageCount(docRef.first.get());
            } catch (...) {
                // Might be encrypted or bad format, will show 0 or handle later
            }

            fileTable_->setItem(row, 1, new QTableWidgetItem(pages > 0 ? QString::number(pages) : "待检测"));
            fileTable_->setItem(row, 2, new QTableWidgetItem("等待处理"));
            fileTable_->setItem(row, 3, new QTableWidgetItem(file));
        } catch (const std::exception& e) {
            qWarning() << "Failed to add file:" << file << e.what();
        } catch (...) {
            qWarning() << "Unknown error adding file:" << file;
        }
    }

    if (wasEmpty && fileTable_->rowCount() > 0) {
        fileTable_->selectRow(0);
    }

    statusLabel_->setText(QString("已加载 %1 个文件。").arg(fileTable_->rowCount()));
}

void MainWindow::onAddFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择包含 PDF 的文件夹");
    if (dir.isEmpty()) return;

    int beforeCount = fileTable_->rowCount();
    QDir d(dir);
    QStringList filters;
    filters << "*.pdf";
    QFileInfoList list = d.entryInfoList(filters, QDir::Files, QDir::Name);

    for (const auto& fi : list) {
        try {
            QString pathStr = fi.absoluteFilePath();
            fs::path p = qstringToPath(pathStr);
            taskManager_.addFile(p);

            int row = fileTable_->rowCount();
            fileTable_->insertRow(row);
            fileTable_->setItem(row, 0, new QTableWidgetItem(fi.fileName()));
            fileTable_->setItem(row, 1, new QTableWidgetItem("待检测"));
            fileTable_->setItem(row, 2, new QTableWidgetItem("等待处理"));
            fileTable_->setItem(row, 3, new QTableWidgetItem(pathStr));
        } catch (const std::exception& e) {
            qWarning() << "Failed to add folder item:" << fi.absoluteFilePath() << e.what();
        } catch (...) {
            qWarning() << "Unknown error adding folder item:" << fi.absoluteFilePath();
        }
    }
    int added = fileTable_->rowCount() - beforeCount;
    if (beforeCount == 0 && added > 0) {
        fileTable_->selectRow(0);
    }
    statusLabel_->setText(QString("从文件夹添加了 %1 个 PDF 文件。").arg(added));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (!mimeData->hasUrls()) return;

    bool wasEmpty = fileTable_->rowCount() == 0;
    for (const QUrl& url : mimeData->urls()) {
        QString file = url.toLocalFile();
        if (!file.endsWith(".pdf", Qt::CaseInsensitive)) continue;

        try {
            fs::path p = qstringToPath(file);
            taskManager_.addFile(p);

            int row = fileTable_->rowCount();
            fileTable_->insertRow(row);
            QFileInfo fi(file);
            fileTable_->setItem(row, 0, new QTableWidgetItem(fi.fileName()));

            int pages = 0;
            try {
                auto docRef = PdfDocument::open(p);
                pages = PdfDocument::pageCount(docRef.first.get());
            } catch (...) {}

            fileTable_->setItem(row, 1, new QTableWidgetItem(pages > 0 ? QString::number(pages) : "待检测"));
            fileTable_->setItem(row, 2, new QTableWidgetItem("等待处理"));
            fileTable_->setItem(row, 3, new QTableWidgetItem(file));
        } catch (const std::exception& e) {
            qWarning() << "Failed to add dropped file:" << file << e.what();
        } catch (...) {
            qWarning() << "Unknown error adding dropped file:" << file;
        }
    }
    if (wasEmpty && fileTable_->rowCount() > 0) {
        fileTable_->selectRow(0);
    }

    statusLabel_->setText(QString("已通过拖拽加载，当前共 %1 个文件。").arg(fileTable_->rowCount()));
}

void MainWindow::onClearFiles() {
    if (taskManager_.isRunning()) return;
    fileTable_->setRowCount(0);
    fileWatermarkConfigs_.clear();
    perPdfConfigMap_.clear();
    currentSelectedFile_.clear();
    taskManager_.clear();
    pageProgressBar_->setValue(0);
    totalProgressBar_->setValue(0);
    loadWatermarksForSelectedFile();
    statusLabel_->setText("列表已清空。");
}

void MainWindow::onSelectOutputDir() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择输出目录");
    if (!dir.isEmpty()) {
        outputDirEdit_->setText(dir);
    }
}

void MainWindow::onCancelClicked() {
    taskManager_.cancel();
    statusLabel_->setText("正在取消任务...");
}
void MainWindow::updateUiState(bool running) {
    cancelBtn_->setEnabled(running);
    startAllBtn_->setEnabled(!running && fileTable_->rowCount() > 0);
    startSelectedBtn_->setEnabled(!running && !currentSelectedFile_.isEmpty());
    addWatermarkBtn_->setEnabled(!running && !currentSelectedFile_.isEmpty());
    depthSlider_->setEnabled(!running);
    perfCombo_->setEnabled(!running);
    outputDirEdit_->setEnabled(!running);
    rotationSpin_->setEnabled(!running);
    fontCombo_->setEnabled(!running);
    boldCheck_->setEnabled(!running);
    italicCheck_->setEnabled(!running);
}

void MainWindow::onRemoveSelectedFile() {
    if (taskManager_.isRunning()) return;

    QList<int> selectedRows;
    for (auto* item : fileTable_->selectedItems()) {
        selectedRows.append(fileTable_->row(item));
    }
    if (selectedRows.empty()) return;

    // Sort descending so we remove from bottom to top (avoids row-index shifting)
    std::sort(selectedRows.begin(), selectedRows.end(), std::greater<int>());
    for (int row : selectedRows) {
        if (row < 0 || row >= fileTable_->rowCount()) continue;
        QString filePath = fileTable_->item(row, 3)->text();
        fileTable_->removeRow(row);
        fileWatermarkConfigs_.erase(filePath);
        perPdfConfigMap_.erase(filePath);
        if (currentSelectedFile_ == filePath) {
            currentSelectedFile_.clear();
        }
    }

    int remaining = fileTable_->rowCount();
    if (remaining > 0) {
        int newRow = std::min(selectedRows.first(), remaining - 1);
        fileTable_->selectRow(newRow);
    } else {
        loadWatermarksForSelectedFile();
    }

    statusLabel_->setText(QString("已移除 %1 个文件。剩余 %2 个文件。")
        .arg(selectedRows.size()).arg(remaining));
}
std::vector<TaskManager::FileSubtask> MainWindow::buildAllConfigs(bool onlySelected) {
    saveCurrentWatermarks();
    std::vector<TaskManager::FileSubtask> subtasks;

    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        if (onlySelected) {
            // Only process the currently selected file
            if (r != fileTable_->currentRow()) continue;
        }
        QTableWidgetItem* pathItem = fileTable_->item(r, 3);
        if (!pathItem) continue;
        QString filePath = pathItem->text();
        if (filePath.isEmpty()) continue;
        fs::path p = qstringToPath(filePath);

        auto it = fileWatermarkConfigs_.find(filePath);
        if (it == fileWatermarkConfigs_.end() || it->second.empty()) {
            continue;
        }
        for (const auto& cfg : it->second) {
            if (cfg.text.empty()) continue;
            subtasks.push_back({ p, cfg });
        }
    }
    return subtasks;
}

void MainWindow::onStartAllClicked() {
    runBatch(false);
}

void MainWindow::onStartSelectedClicked() {
    if (fileTable_->currentRow() < 0) {
        QMessageBox::warning(this, "提示", "请先在左侧列表选择一个 PDF 文件。");
        return;
    }
    runBatch(true);
}

void MainWindow::runBatch(bool onlySelected) {
    if (fileTable_->rowCount() == 0) {
        QMessageBox::warning(this, "提示", "请先添加至少一个 PDF 文件。");
        return;
    }

    auto subtasks = buildAllConfigs(onlySelected);
    if (subtasks.empty()) {
        QMessageBox::warning(this, "提示",
            "请为" + QString(onlySelected ? "当前选中的文件" : "列表中的文件")
            + "添加至少一个水印文字后再次开始。\n"
            "操作：左侧选中文件 → 右侧点击 \"+ 添加水印\"。");
        return;
    }

    taskManager_.clear();
    taskManager_.setSubtasks(subtasks);
    taskManager_.setPerformanceMode(static_cast<PerformanceMode>(perfCombo_->currentData().toInt()));
    if (!outputDirEdit_->text().trimmed().isEmpty()) {
        taskManager_.setOutputDirectory(qstringToPath(outputDirEdit_->text().trimmed()));
    } else {
        taskManager_.setOutputDirectory(fs::path());
    }

    updateUiState(true);
    taskManager_.start();
}
void MainWindow::onFileStarted(const QString& fileName, int index, int total) {
    statusLabel_->setText(QString("正在处理 [%1/%2]: %3").arg(index).arg(total).arg(fileName));
    // Extract base file name if fileName is formatted like "file.pdf [watermark]"
    QString baseName = fileName;
    int bracketIdx = baseName.indexOf(" [");
    if (bracketIdx != -1) {
        baseName = baseName.left(bracketIdx);
    }
    // Update row status
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        auto* nameItem = fileTable_->item(r, 0);
        if (!nameItem) continue;
        if (nameItem->text() == baseName) {
            auto* statusItem = fileTable_->item(r, 2);
            if (statusItem) statusItem->setText("处理中...");
            break;
        }
    }
}

void MainWindow::onPageProgress(const QString& /*fileName*/, int current, int total) {
    int pct = total > 0 ? (current * 100 / total) : 0;
    pageProgressBar_->setValue(pct);
}

void MainWindow::onFileProgress(int completed, int total) {
    int pct = total > 0 ? (completed * 100 / total) : 0;
    totalProgressBar_->setValue(pct);
}
void MainWindow::onFileFinished(const FileResult& result) {
    // Use UTF-8 safe conversion to match the path encoding used everywhere else
    QString fileName = QString::fromUtf8(pathToString(result.inputPath.filename()).c_str());
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        auto* nameItem = fileTable_->item(r, 0);
        if (!nameItem) continue;
        if (nameItem->text() != fileName) continue;
        auto* pageItem = fileTable_->item(r, 1);
        auto* statusItem = fileTable_->item(r, 2);
        if (pageItem && result.totalPages > 0) pageItem->setText(QString::number(result.totalPages));
        if (statusItem) {
            if (result.success) {
                statusItem->setText(QString("完成 [%1]").arg(QString::fromUtf8(result.watermarkText.c_str())));
            } else {
                statusItem->setText(QString("失败 [%1]: %2").arg(
                    QString::fromUtf8(result.watermarkText.c_str()),
                    QString::fromUtf8(result.errorMessage.c_str())));
            }
        }
        break;
    }
}

void MainWindow::onAllFinished(const std::vector<FileResult>& results) {
    updateUiState(false);
    if (results.empty()) {
        statusLabel_->setText("未执行任何任务。");
        QMessageBox::information(this, "提示", "未执行任何任务，请检查输入文件及水印设置。");
        return;
    }

    int successCount = 0;
    QString failDetails;
    for (const auto& r : results) {
        if (r.success) {
            successCount++;
        } else {
            QString fname = QString::fromUtf8(pathToString(r.inputPath.filename()).c_str());
            QString wm = QString::fromUtf8(r.watermarkText.c_str());
            QString err = QString::fromUtf8(r.errorMessage.c_str());
            failDetails += QString("\n• %1 [水印: %2]: %3").arg(fname, wm, err);
        }
    }

    int failCount = static_cast<int>(results.size()) - successCount;
    statusLabel_->setText(QString("全部任务完成！成功: %1 / 总计: %2")
                          .arg(successCount).arg(results.size()));

    if (failCount > 0) {
        QMessageBox::warning(this, "处理完成（存在失败）",
                             QString("批量固化完成！\n成功: %1\n失败: %2\n\n失败详情:%3")
                             .arg(successCount)
                             .arg(failCount)
                             .arg(failDetails));
    } else {
        QMessageBox::information(this, "完成",
                                 QString("批量固化完成！\n成功: %1\n失败: 0")
                                 .arg(successCount));
    }
    for (const auto& r : results) {
        if (r.success) {
            lastOutputDir_ = QString::fromUtf8(pathToString(r.outputPath.parent_path()).c_str());
            break;
        }
    }
    if (lastOutputDir_.isEmpty()) {
        if (!outputDirEdit_->text().trimmed().isEmpty()) {
            lastOutputDir_ = outputDirEdit_->text().trimmed();
        } else if (!results.empty()) {
            lastOutputDir_ = QString::fromUtf8(pathToString(results[0].outputPath.parent_path()).c_str());
        }
    }
    openFolderBtn_->setEnabled(!lastOutputDir_.isEmpty());
}

void MainWindow::onCancelled() {
    updateUiState(false);
    statusLabel_->setText("用户已取消处理。");
}

void MainWindow::onPasswordRequired(const QString& filePath) {
    QFileInfo fi(filePath);
    PasswordDialog dlg(fi.fileName(), this);
    if (dlg.exec() == QDialog::Accepted) {
        knownPasswords_[filePath.toStdString()] = dlg.password().toStdString();
        taskManager_.setPasswords(knownPasswords_);
    }
}
void MainWindow::onPreviewWatermark() {
    // Get current config
    WatermarkConfig cfg = currentConfig();
    if (cfg.text.empty()) {
        cfg.text = "机密文件 请勿外传";
    }

    // Render 600x800 preview (approx standard 3:4 portrait page proportion)
    QImage previewImg = WatermarkRenderer::renderPreview(600, 800, cfg);

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle("水印效果实时预览");
    dlg->resize(640, 860);
    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* infoLabel = new QLabel(
        QString("水印文字：%1   倾斜：%2°   字体：%3   深浅：%4%")
            .arg(QString::fromUtf8(cfg.text.c_str()))
            .arg(cfg.rotationDegrees)
            .arg(QString::fromStdString(cfg.fontFamily))
            .arg(static_cast<int>(cfg.opacity * 100)),
        dlg
    );
    infoLabel->setStyleSheet("color: #444; font-size: 13px;");
    layout->addWidget(infoLabel);

    auto* imgLabel = new QLabel(dlg);
    imgLabel->setFrameShape(QFrame::Box);
    imgLabel->setAlignment(Qt::AlignCenter);
    imgLabel->setPixmap(QPixmap::fromImage(previewImg));
    imgLabel->setStyleSheet("background-color: white; border: 1px solid #ccc;");
    layout->addWidget(imgLabel, 1);

    auto* closeBtn = new QPushButton("关闭", dlg);
    closeBtn->setStyleSheet("padding: 6px 20px;");
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    auto* btnBox = new QHBoxLayout();
    btnBox->addStretch();
    btnBox->addWidget(closeBtn);
    layout->addLayout(btnBox);

    dlg->exec();
}


void MainWindow::onOpenOutputFolder() {
    if (lastOutputDir_.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(lastOutputDir_));
}

void MainWindow::onCheckForUpdates() {
    auto* dlg = new UpdateDialog(autoUpdater_, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void MainWindow::onSilentUpdateAvailable(const UpdateInfo& info) {
    // Disconnect so this fires only once per session
    disconnect(autoUpdater_, &AutoUpdater::updateAvailable,
               this, &MainWindow::onSilentUpdateAvailable);
    // Show a non-blocking notification in the status bar
    statusBar()->showMessage(
        QString("发现新版本 %1，访问「帮助→检查更新」进行升级").arg(info.versionTag), 10000);
}

} // namespace pdfmark