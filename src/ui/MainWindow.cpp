// PDFMark - Main GUI Window implementation.
#include "ui/MainWindow.h"
#include "ui/FileTableSelection.h"
#include "ui/PasswordDialog.h"
#include "diagnostics/Diagnostics.h"
#include "task/WorkerPool.h"
#include "pdf/PdfDocument.h"
#include "watermark/WatermarkRenderer.h"
#include "watermark/WatermarkSelection.h"
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
#include <QInputDialog>
#include <QCheckBox>
#include <algorithm>
#include <QListWidget>
namespace pdfmark {

// Pause inserted between job submissions when a batch is much larger than the
// concurrency window ("process progressively", keeps the machine responsive).
static constexpr int kThrottleMs = 30;

// ── WatermarkRow ────────────────────────────────────────────────────────────
WatermarkRow::WatermarkRow(const QString& initialText, int index, QWidget* parent)
    : QWidget(parent), index_(index) {
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);

    checkBox_ = new QCheckBox(this);
    checkBox_->setChecked(true);
    checkBox_->setToolTip("勾选后，「生成所选」会生成此水印；取消勾选则跳过");
    h->addWidget(checkBox_);

    lineEdit_ = new QLineEdit(this);
    lineEdit_->setText(initialText);
    lineEdit_->setPlaceholderText("水印文字（例如：机密-张三）");
    h->addWidget(lineEdit_, 1);

    removeBtn_ = new QPushButton("×", this);
    removeBtn_->setFixedSize(28, 28);
    removeBtn_->setStyleSheet("color: #cc0000; font-weight: bold; font-size: 16px;");
    removeBtn_->setToolTip("删除此水印");
    h->addWidget(removeBtn_);

    connect(checkBox_, &QCheckBox::toggled, this, [this](bool on) {
        lineEdit_->setStyleSheet(on ? QString() : QStringLiteral("color: #999999;"));
        emit selectionChanged();
    });
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

bool WatermarkRow::isSelected() const {
    return checkBox_->isChecked();
}

void WatermarkRow::setSelected(bool on) {
    checkBox_->setChecked(on);
    lineEdit_->setStyleSheet(on ? QString() : QStringLiteral("color: #999999;"));
}
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();
    setupConnections();
    setWindowTitle("PDF 水印固化工具");
    resize(1100, 720);

    templateStore_.load();
    refreshTemplateCombo();
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


    // Watermark templates (reusable text + style bundles)
    auto* tplGroup = new QGroupBox("水印模板", rightContainer);
    auto* tplLayout = new QVBoxLayout(tplGroup);
    tplLayout->setContentsMargins(10, 12, 10, 10);
    tplLayout->setSpacing(6);

    auto* tplRow1 = new QHBoxLayout();
    tplRow1->setSpacing(6);
    tplRow1->addWidget(new QLabel("模板:", tplGroup));
    templateCombo_ = new QComboBox(tplGroup);
    templateCombo_->setMinimumWidth(140);
    templateCombo_->setToolTip("选择一个已保存的水印模板（多行文字 + 一套样式）");
    tplRow1->addWidget(templateCombo_, 1);
    tplLayout->addLayout(tplRow1);

    auto* tplRow2 = new QHBoxLayout();
    tplRow2->setSpacing(6);
    applyTemplateBtn_ = new QPushButton("应用", tplGroup);
    applyTemplateBtn_->setToolTip("将所选模板（文字 + 样式）套用到当前选中的 PDF");
    applyTemplateAllBtn_ = new QPushButton("应用到全部文件", tplGroup);
    applyTemplateAllBtn_->setToolTip("将所选模板套用到左侧列表中的所有 PDF，然后点击「生成全部」批量固化");
    saveTemplateBtn_ = new QPushButton("存为模板...", tplGroup);
    saveTemplateBtn_->setToolTip("把当前文件的水印文字与样式保存为可复用的模板");
    manageTemplateBtn_ = new QPushButton("管理...", tplGroup);
    manageTemplateBtn_->setToolTip("重命名或删除已保存的水印模板");
    tplRow2->addWidget(applyTemplateBtn_);
    tplRow2->addWidget(applyTemplateAllBtn_);
    tplRow2->addStretch();
    tplRow2->addWidget(saveTemplateBtn_);
    tplRow2->addWidget(manageTemplateBtn_);
    tplLayout->addLayout(tplRow2);
    rightLayout->addWidget(tplGroup);

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
    perfCombo_->setToolTip("引擎限制：PDFium 非线程安全，页面解析与光栅化已全局串行；\n"
                           "并发只用于重叠水印绘制、JPEG 编码与磁盘 I/O。\n"
                           "保守运行：同一时刻只处理 1 个文件，最省内存，适合边办公边处理\n"
                           "日常推荐：2 个文件并行，速度与内存均衡，适合大多数场景\n"
                           "火力全开：最多 4 个文件并行，对扫描件/大页 PDF 的 JPEG 编码更快，但内存占用更高");
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
    startSelectedBtn_->setToolTip("仅生成当前文件中已勾选的水印，未勾选的水印会跳过");
    startSelectedBtn_->setStyleSheet(
        "QPushButton { background-color: #107c10; color: white; font-weight: bold; "
        "padding: 7px 16px; border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background-color: #0b5c0b; }"
        "QPushButton:pressed { background-color: #094509; }"
        "QPushButton:disabled { background-color: #cccccc; color: #888888; }");
    connect(startSelectedBtn_, &QPushButton::clicked, this, &MainWindow::onStartSelectedClicked);
    ctlRow->addWidget(startSelectedBtn_);

    startAllBtn_ = new QPushButton("生成全部", statusCard);
    startAllBtn_->setToolTip("忽略勾选，生成所有文件的所有水印");
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

    // Watermark template controls
    connect(applyTemplateBtn_, &QPushButton::clicked, this, &MainWindow::onApplyTemplateToCurrent);
    connect(applyTemplateAllBtn_, &QPushButton::clicked, this, &MainWindow::onApplyTemplateToAll);
    connect(saveTemplateBtn_, &QPushButton::clicked, this, &MainWindow::onSaveAsTemplate);
    connect(manageTemplateBtn_, &QPushButton::clicked, this, &MainWindow::onManageTemplates);
    connect(templateCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        updateUiState(taskManager_.isRunning());
    });

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

WatermarkConfig MainWindow::styleFromUi() const {
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
    return cfg;
}

void MainWindow::applyStyleToUi(const WatermarkConfig& style) {
    rotationSpin_->setValue(style.rotationDegrees);
    const QString family = style.fontFamily.empty()
        ? QStringLiteral("Arial")
        : QString::fromUtf8(style.fontFamily.c_str());
    fontCombo_->setCurrentFont(QFont(family));
    boldCheck_->setChecked(style.fontBold);
    italicCheck_->setChecked(style.fontItalic);
    depthSlider_->setValue(static_cast<int>(style.opacity * 100.0 + 0.5));
}

WatermarkConfig MainWindow::currentConfig() const {
    WatermarkConfig cfg = styleFromUi();
    // Prefer the first checked, non-empty row so the preview matches what
    // "生成所选" will actually produce; fall back to the first non-empty row.
    WatermarkConfig fallback;
    bool hasFallback = false;
    for (int i = 0; i < watermarkLayout_->count(); ++i) {
        auto* item = watermarkLayout_->itemAt(i);
        if (auto* row = qobject_cast<WatermarkRow*>(item->widget())) {
            QString t = row->text().trimmed();
            if (t.isEmpty()) continue;
            cfg.text = t.toStdString();
            if (row->isSelected()) return cfg;
            if (!hasFallback) {
                fallback = cfg;
                hasFallback = true;
            }
        }
    }
    if (hasFallback) return fallback;

    cfg.text = "";
    return cfg;
}


void MainWindow::saveCurrentWatermarks() {
    if (currentSelectedFile_.isEmpty()) return;

    const WatermarkConfig style = styleFromUi();
    std::vector<WatermarkConfig> configs;
    for (int i = 0; i < watermarkLayout_->count(); ++i) {
        auto* item = watermarkLayout_->itemAt(i);
        if (auto* row = qobject_cast<WatermarkRow*>(item->widget())) {
            QString t = row->text().trimmed();
            if (t.isEmpty()) continue;
            WatermarkConfig cfg = style;
            cfg.text = t.toStdString();
            cfg.selected = row->isSelected();
            configs.push_back(cfg);
        }
    }
    fileWatermarkConfigs_[currentSelectedFile_] = configs;
    // Keep the per-file style template in sync with the live controls so
    // templates and batch generation always see the same style.
    perPdfConfigMap_[currentSelectedFile_] = style;
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
        applyStyleToUi(itStyle->second);
    } else {
        WatermarkConfig def;
        def.rotationDegrees = -35.0;
        def.fontFamily = "Arial";
        def.fontBold = true;
        def.fontItalic = false;
        applyStyleToUi(def);
    }

    // Load existing watermark configs, or leave empty
    auto it = fileWatermarkConfigs_.find(currentSelectedFile_);
    const std::vector<WatermarkConfig> emptyList;
    const auto& cfgs = (it != fileWatermarkConfigs_.end()) ? it->second : emptyList;
    for (const auto& cfg : cfgs) {
        int idx = nextWatermarkIndex_++;
        auto* row = new WatermarkRow(QString::fromStdString(cfg.text), idx, watermarkContainer_);
        // Restore the saved checkbox state before wiring signals, so loading
        // rows does not trigger a save while the list is still being rebuilt.
        row->setSelected(cfg.selected);
        connect(row, &WatermarkRow::textChanged, this, &MainWindow::onWatermarkTextChanged);
        connect(row, &WatermarkRow::removeRequested, this, &MainWindow::removeWatermarkRow);
        connect(row, &WatermarkRow::selectionChanged, this, &MainWindow::onWatermarkSelectionChanged);
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
    updateUiState(taskManager_.isRunning());
}

void MainWindow::onWatermarkTextChanged() {
    saveCurrentWatermarks();
    updateUiState(taskManager_.isRunning());
}

void MainWindow::onWatermarkSelectionChanged() {
    saveCurrentWatermarks();
    updateUiState(taskManager_.isRunning());
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

    rebuildFileIndex();
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
    rebuildFileIndex();
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
    rebuildFileIndex();
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
    fileNameToRow_.clear();
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

    // "生成所选" only makes sense when the current file has at least one
    // checked, non-empty watermark.
    bool hasCheckedWatermark = false;
    auto itChecked = fileWatermarkConfigs_.find(currentSelectedFile_);
    if (!currentSelectedFile_.isEmpty() && itChecked != fileWatermarkConfigs_.end()) {
        for (const auto& cfg : itChecked->second) {
            if (cfg.selected && !cfg.text.empty()) {
                hasCheckedWatermark = true;
                break;
            }
        }
    }
    startSelectedBtn_->setEnabled(!running && hasCheckedWatermark);
    addWatermarkBtn_->setEnabled(!running && !currentSelectedFile_.isEmpty());

    const bool hasTemplate = templateCombo_ && !templateCombo_->currentData().toString().isEmpty();
    templateCombo_->setEnabled(!running);
    applyTemplateBtn_->setEnabled(!running && hasTemplate && !currentSelectedFile_.isEmpty());
    applyTemplateAllBtn_->setEnabled(!running && hasTemplate && fileTable_->rowCount() > 0);
    saveTemplateBtn_->setEnabled(!running && !currentSelectedFile_.isEmpty());
    manageTemplateBtn_->setEnabled(!running);

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

    // Descending, de-duplicated rows (bottom-to-top so removeRow() does not
    // shift the indices still to be processed).
    // NOTE: fileTable_->selectedItems() must NOT be used here: with
    // SelectRows it returns one item per *cell* (4 per row in this 4-column
    // table), and the resulting duplicate row indices each removed a freshly
    // shifted row — deleting 3 extra files per selected row.
    QList<int> selectedRows = selectedRowsDescending(fileTable_);
    if (selectedRows.empty()) return;

    for (int row : selectedRows) {
        if (row < 0 || row >= fileTable_->rowCount()) continue;
        QTableWidgetItem* pathItem = fileTable_->item(row, 3);
        if (!pathItem) continue;
        QString filePath = pathItem->text();
        fileTable_->removeRow(row);
        fileWatermarkConfigs_.erase(filePath);
        perPdfConfigMap_.erase(filePath);
        if (currentSelectedFile_ == filePath) {
            currentSelectedFile_.clear();
        }
    }

    int remaining = fileTable_->rowCount();
    if (remaining > 0) {
        // selectedRows is descending, so its last element is the top-most
        // removed row; select whatever row now occupies that position.
        int anchor = std::min(selectedRows.last(), remaining - 1);
        fileTable_->selectRow(std::max(0, anchor));
    } else {
        loadWatermarksForSelectedFile();
    }

    rebuildFileIndex();
    statusLabel_->setText(QString("已移除 %1 个文件。剩余 %2 个文件。")
        .arg(selectedRows.size()).arg(remaining));
}
void MainWindow::rebuildFileIndex() {
    fileNameToRow_.clear();
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        QTableWidgetItem* nameItem = fileTable_->item(r, 0);
        if (!nameItem) continue;
        if (!fileNameToRow_.contains(nameItem->text())) {
            fileNameToRow_.insert(nameItem->text(), r);
        }
    }
}

WorkloadEstimate MainWindow::estimateCurrentWorkload(
    const std::vector<TaskManager::FileSubtask>& subtasks) const {
    WorkloadInput in;
    in.maxDpi = 200;
    in.totalRamBytes = MemoryProbe::totalPhysicalBytes();
    in.currentRssBytes = MemoryProbe::currentPhysicalBytes();
    in.cpuCap = WorkerPool::idealWorkerCount(
        static_cast<PerformanceMode>(perfCombo_->currentData().toInt()));
    if (in.cpuCap < 1) in.cpuCap = 1;

    // Page counts come from the file table (column 1); "待检测" => unknown (0).
    std::unordered_map<std::string, int> pagesByPath;
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        QTableWidgetItem* pathItem = fileTable_->item(r, 3);
        if (!pathItem) continue;
        bool ok = false;
        int pages = 0;
        if (QTableWidgetItem* pageItem = fileTable_->item(r, 1)) {
            pages = pageItem->text().toInt(&ok);
        }
        pagesByPath[pathToString(qstringToPath(pathItem->text()))] = ok ? pages : 0;
    }

    std::unordered_map<std::string, size_t> indexOf;
    for (const auto& st : subtasks) {
        in.maxDpi = std::max(in.maxDpi, st.config.dpi);
        const std::string key = pathToString(st.input);
        auto it = indexOf.find(key);
        if (it == indexOf.end()) {
            WorkloadFileInfo fi;
            auto pit = pagesByPath.find(key);
            fi.pages = (pit != pagesByPath.end()) ? pit->second : 0;
            fi.watermarks = 1;
            std::error_code ec;
            const auto size = fs::file_size(st.input, ec);
            fi.sourceBytes = ec ? 0 : static_cast<int64_t>(size);
            indexOf.emplace(key, in.files.size());
            in.files.push_back(fi);
        } else {
            in.files[it->second].watermarks++;
        }
    }
    return estimateWorkload(in);
}

bool MainWindow::preflightAllowsRun(const std::vector<TaskManager::FileSubtask>& subtasks) {
    const WorkloadEstimate est = estimateCurrentWorkload(subtasks);
    const PerformanceMode mode =
        static_cast<PerformanceMode>(perfCombo_->currentData().toInt());
    const int cpuCap = std::max(1, WorkerPool::idealWorkerCount(mode));
    const int recommended = std::max(1, est.recommendedConcurrentDocs);
    const bool bigBatch = est.fileCount > 2 * recommended;

    auto applyPlan = [this](int docs, bool throttle) {
        taskManager_.setMaxConcurrentDocuments(docs);
        taskManager_.setThrottleMs(throttle ? kThrottleMs : 0);
    };
    auto mb = [](int64_t bytes) {
        return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 0);
    };

    if (preflightSuppressed_ || !needsPreflightWarning(est)) {
        // Silent safety cap: never spawn more concurrent documents than the
        // RAM budget allows, even when the batch is small enough to skip the UI.
        applyPlan(recommended, bigBatch);
        return true;
    }

    const QString detail = QString(
        "本次任务规模：\n"
        "  • 文件：%1 个\n"
        "  • 水印：%2 条（共 %3 次页面水印处理）\n"
        "  • 部分文件页数未知时按 10 页估算：%4\n"
        "  • 单个文件峰值内存：约 %5 MB\n\n"
        "  推荐并发：%6 个文件（当前性能模式为 %7 个）\n"
        "  预计峰值内存：约 %8 MB，可用约 %9 MB\n"
        "  内存风险评估：%10")
        .arg(est.fileCount)
        .arg(est.watermarkCount)
        .arg(est.estimatedPages)
        .arg(est.pagesEstimated ? "是" : "否")
        .arg(mb(est.perDocPeakBytes))
        .arg(recommended)
        .arg(cpuCap)
        .arg(mb(std::max(est.peakBytes, est.perDocPeakBytes)))
        .arg(mb(est.usableRamBytes))
        .arg(QString::fromUtf8(workloadRiskLabel(est.risk)));

    QMessageBox box(this);
    box.setWindowTitle("开始前的资源检查");
    box.setIcon(est.risk == WorkloadRisk::Risky ? QMessageBox::Warning
                                                : QMessageBox::Information);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.setText(detail);
    box.setInformativeText(est.risk == WorkloadRisk::Risky
        ? "按当前设置运行很可能因内存不足而崩溃或被系统终止，建议使用推荐设置。"
        : "建议使用推荐设置：程序会自动分批处理，并在必要时降速，以避免卡顿或崩溃。");

    QPushButton* recommendBtn = box.addButton("按推荐设置继续", QMessageBox::AcceptRole);
    QPushButton* currentBtn = box.addButton("按当前设置继续", QMessageBox::DestructiveRole);
    QPushButton* cancelBtn = box.addButton("取消", QMessageBox::RejectRole);
    QCheckBox* dontAsk = new QCheckBox("本次会话不再提示", &box);
    box.setCheckBox(dontAsk);
    box.setDefaultButton(est.risk == WorkloadRisk::Risky ? cancelBtn : recommendBtn);

    box.exec();
    if (dontAsk->isChecked()) preflightSuppressed_ = true;

    if (box.clickedButton() == cancelBtn) {
        statusLabel_->setText("已取消（资源预检未通过）。");
        return false;
    }

    if (box.clickedButton() == currentBtn) {
        if (est.risk == WorkloadRisk::Risky) {
            const auto answer = QMessageBox::warning(
                this, "确认风险",
                "预计峰值内存将超过当前可用内存，程序可能崩溃或被系统终止。\n\n"
                "确定仍按当前设置继续吗？",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                statusLabel_->setText("已取消（资源预检未通过）。");
                return false;
            }
        }
        applyPlan(cpuCap, bigBatch);
        statusLabel_->setText(QString("按当前设置运行（并发 %1）。").arg(cpuCap));
        return true;
    }

    applyPlan(recommended, bigBatch);
    statusLabel_->setText(QString("按推荐设置运行（并发 %1，分批处理）。").arg(recommended));
    return true;
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
        // "生成所选" honours the per-watermark checkboxes; "生成全部" ignores
        // them and generates every configured watermark.
        std::vector<WatermarkConfig> configs = it->second;
        if (onlySelected) {
            configs = filterSelectedWatermarks(configs);
        }
        for (const auto& cfg : configs) {
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
        if (onlySelected) {
            QMessageBox::warning(this, "提示",
                "当前文件没有已勾选的水印，请至少勾选一个水印文字后再次开始。\n"
                "操作：左侧选中文件 → 右侧勾选要生成的水印。");
        } else {
            QMessageBox::warning(this, "提示",
                "请为列表中的文件添加至少一个水印文字后再次开始。\n"
                "操作：左侧选中文件 → 右侧点击 \"+ 添加水印\"。");
        }
        return;
    }

    // Pre-flight memory/concurrency guard: may warn, may cancel, and always
    // caps concurrency so a huge batch cannot exhaust RAM.
    if (!preflightAllowsRun(subtasks)) {
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
    QString pace = QString("并发 %1").arg(std::max(1, taskManager_.effectiveConcurrency()));
    if (taskManager_.throttleMs() > 0) {
        pace += QString("，限速 %1ms").arg(taskManager_.throttleMs());
    }
    statusLabel_->setText(QString("正在处理 [%1/%2]: %3（%4）").arg(index).arg(total).arg(fileName).arg(pace));

    // Extract base file name if fileName is formatted like "file.pdf [watermark]"
    QString baseName = fileName;
    int bracketIdx = baseName.indexOf(" [");
    if (bracketIdx != -1) {
        baseName = baseName.left(bracketIdx);
    }
    const auto it = fileNameToRow_.constFind(baseName);
    if (it != fileNameToRow_.constEnd()) {
        if (auto* statusItem = fileTable_->item(it.value(), 2)) {
            statusItem->setText("处理中...");
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
    const auto it = fileNameToRow_.constFind(fileName);
    if (it == fileNameToRow_.constEnd()) return;
    const int row = it.value();
    auto* pageItem = fileTable_->item(row, 1);
    auto* statusItem = fileTable_->item(row, 2);
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


// ── Watermark templates ─────────────────────────────────────────────────────

void MainWindow::refreshTemplateCombo(const QString& select) {
    if (!templateCombo_) return;
    const QString previous = templateCombo_->currentData().toString();

    templateCombo_->blockSignals(true);
    templateCombo_->clear();
    templateCombo_->addItem(QStringLiteral("（选择模板）"), QString());
    for (const auto& tpl : templateStore_.templates()) {
        const QString name = QString::fromUtf8(tpl.name.c_str());
        templateCombo_->addItem(name, name);
    }
    QString want = select.isEmpty() ? previous : select;
    int idx = want.isEmpty() ? 0 : templateCombo_->findData(want);
    if (idx < 0) idx = 0;
    templateCombo_->setCurrentIndex(idx);
    templateCombo_->blockSignals(false);

    // Enablement of the template buttons depends on whether a template is
    // selected, so refresh it here as well.
    updateUiState(taskManager_.isRunning());
}

WatermarkTemplate MainWindow::currentUiAsTemplate(const QString& name) const {
    WatermarkTemplate tpl;
    tpl.name = name.toUtf8().toStdString();
    const WatermarkConfig style = styleFromUi();
    for (int i = 0; i < watermarkLayout_->count(); ++i) {
        auto* item = watermarkLayout_->itemAt(i);
        if (auto* row = qobject_cast<WatermarkRow*>(item->widget())) {
            const QString t = row->text().trimmed();
            if (t.isEmpty()) continue;
            WatermarkConfig cfg = style;
            cfg.text = t.toStdString();
            cfg.selected = row->isSelected();
            tpl.watermarks.push_back(cfg);
        }
    }
    return tpl;
}

void MainWindow::applyTemplateToFile(const WatermarkTemplate& tpl, const QString& filePath) {
    if (filePath.isEmpty()) return;
    fileWatermarkConfigs_[filePath] = templateToConfigs(tpl);
    perPdfConfigMap_[filePath] = tpl.style();
}

void MainWindow::applyTemplateToCurrentUi(const WatermarkTemplate& tpl) {
    if (currentSelectedFile_.isEmpty()) return;
    applyTemplateToFile(tpl, currentSelectedFile_);
    loadWatermarksForSelectedFile();
    updateUiState(taskManager_.isRunning());
}

void MainWindow::onApplyTemplateToCurrent() {
    const QString name = templateCombo_ ? templateCombo_->currentData().toString() : QString();
    if (name.isEmpty()) {
        QMessageBox::information(this, "提示", "请先在水印模板下拉框中选择一个模板。");
        return;
    }
    const WatermarkTemplate* tpl = templateStore_.find(name);
    if (!tpl) {
        refreshTemplateCombo();
        return;
    }
    if (currentSelectedFile_.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先在左侧列表选择一个 PDF 文件。");
        return;
    }
    applyTemplateToCurrentUi(*tpl);
    statusLabel_->setText(QString("已套用模板「%1」到当前文件。").arg(name));
}

void MainWindow::onApplyTemplateToAll() {
    const QString name = templateCombo_ ? templateCombo_->currentData().toString() : QString();
    if (name.isEmpty()) {
        QMessageBox::information(this, "提示", "请先在水印模板下拉框中选择一个模板。");
        return;
    }
    const WatermarkTemplate* tpl = templateStore_.find(name);
    if (!tpl) {
        refreshTemplateCombo();
        return;
    }
    if (fileTable_->rowCount() == 0) {
        QMessageBox::warning(this, "提示", "请先添加至少一个 PDF 文件。");
        return;
    }

    int applied = 0;
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        auto* item = fileTable_->item(r, 3);
        if (!item) continue;
        const QString path = item->text();
        if (path.isEmpty()) continue;
        applyTemplateToFile(*tpl, path);
        ++applied;
    }
    if (!currentSelectedFile_.isEmpty()) {
        loadWatermarksForSelectedFile();
    }
    updateUiState(taskManager_.isRunning());
    statusLabel_->setText(QString("已套用模板「%1」到 %2 个文件，可直接点击「生成全部」批量固化。")
        .arg(name).arg(applied));
}

void MainWindow::onSaveAsTemplate() {
    if (currentSelectedFile_.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择一个 PDF 文件，模板将保存其当前水印文字与样式。");
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, "保存为水印模板",
        "模板名称：", QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    WatermarkTemplate tpl = currentUiAsTemplate(name);
    if (!tpl.isValid()) {
        QMessageBox::warning(this, "提示", "请至少填写一个水印文字后再保存为模板。");
        return;
    }
    if (templateStore_.find(name)) {
        if (QMessageBox::question(this, "覆盖模板",
                QString("模板「%1」已存在，是否覆盖？").arg(name)) != QMessageBox::Yes) {
            return;
        }
    }
    templateStore_.addOrReplace(tpl);
    templateStore_.save();
    refreshTemplateCombo(name);
    updateUiState(taskManager_.isRunning());
    statusLabel_->setText(QString("模板「%1」已保存。").arg(name));
}

void MainWindow::onManageTemplates() {
    QDialog dlg(this);
    dlg.setWindowTitle("管理水印模板");
    dlg.resize(420, 340);
    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* list = new QListWidget(&dlg);
    for (const auto& tpl : templateStore_.templates()) {
        list->addItem(QString::fromUtf8(tpl.name.c_str()));
    }
    layout->addWidget(list, 1);

    auto* btnRow = new QHBoxLayout();
    auto* renameBtn = new QPushButton("重命名...", &dlg);
    auto* deleteBtn = new QPushButton("删除", &dlg);
    deleteBtn->setStyleSheet("color: #cc0000;");
    auto* closeBtn = new QPushButton("关闭", &dlg);
    btnRow->addWidget(renameBtn);
    btnRow->addWidget(deleteBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(renameBtn, &QPushButton::clicked, &dlg, [&]() {
        auto* cur = list->currentItem();
        if (!cur) return;
        const QString oldName = cur->text();
        bool ok = false;
        const QString newName = QInputDialog::getText(&dlg, "重命名模板", "新名称：",
            QLineEdit::Normal, oldName, &ok).trimmed();
        if (!ok || newName.isEmpty() || newName == oldName) return;
        if (!templateStore_.rename(oldName, newName)) {
            QMessageBox::warning(&dlg, "提示", QString("模板「%1」已存在或名称无效。").arg(newName));
            return;
        }
        templateStore_.save();
        cur->setText(newName);
        refreshTemplateCombo(newName);
    });
    connect(deleteBtn, &QPushButton::clicked, &dlg, [&]() {
        auto* cur = list->currentItem();
        if (!cur) return;
        const QString name = cur->text();
        if (QMessageBox::question(&dlg, "删除模板",
                QString("确定删除模板「%1」？").arg(name)) != QMessageBox::Yes) {
            return;
        }
        templateStore_.remove(name);
        templateStore_.save();
        delete cur;
        refreshTemplateCombo();
    });

    dlg.exec();
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