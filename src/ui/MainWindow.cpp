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
#include <QRadioButton>
#include <QStackedWidget>
#include <QButtonGroup>
#include <QMenu>
#include <QSignalBlocker>
#include <QShortcut>
#include <algorithm>
#include <QListWidget>
namespace pdfmark {

// Pause inserted between job submissions when a batch is much larger than the
// concurrency window ("process progressively", keeps the machine responsive).
static constexpr int kThrottleMs = 30;

// Human hint for the watermark opacity slider (shared by the slider, the
// spin box and programmatic style application so they can never disagree).
static QString depthHintText(int percent) {
    if (percent <= 8) return QStringLiteral("极浅");
    if (percent <= 12) return QStringLiteral("偏浅");
    if (percent <= 25) return QStringLiteral("适中");
    if (percent <= 40) return QStringLiteral("偏深");
    return QStringLiteral("深色");
}

// ── WatermarkRow ────────────────────────────────────────────────────────────
WatermarkRow::WatermarkRow(const QString& initialText, int index, QWidget* parent)
    : QWidget(parent), index_(index) {
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);

    checkBox_ = new QCheckBox(this);
    checkBox_->setChecked(true);
    checkBox_->setToolTip("勾选：生成当前 PDF 时包含此水印；取消勾选：跳过此水印");
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
    onWatermarkModeChanged();
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
    addFilesBtn->setStyleSheet("padding: 6px 14px; font-weight: bold;");
    addFolderBtn->setStyleSheet("padding: 6px 14px;");
    clearBtn->setStyleSheet("padding: 6px 14px;");
    removeSelBtn->setStyleSheet("padding: 6px 14px; color: #cc0000;");

    topLayout->addWidget(addFilesBtn);
    topLayout->addWidget(addFolderBtn);
    topLayout->addWidget(clearBtn);
    topLayout->addWidget(removeSelBtn);
    topLayout->addStretch();
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


    // ── Header: mode switch (segmented control) + scope banner ───────────
    // "模板"    : one global template drives every PDF. Nothing per file.
    // "自定义"  : edit the watermark rows/style of the currently selected PDF.
    auto* headRow = new QHBoxLayout();
    headRow->setSpacing(8);

    modeTemplateBtn_ = new QPushButton("模板", rightContainer);
    modeCustomBtn_ = new QPushButton("自定义", rightContainer);
    for (QPushButton* b : {modeTemplateBtn_, modeCustomBtn_}) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(
            "QPushButton{border:1px solid #c8d0da;background:#ffffff;padding:5px 18px;}"
            "QPushButton:checked{background:#0078d4;color:#ffffff;border-color:#0078d4;font-weight:bold;}"
            "QPushButton:disabled{color:#aaaaaa;background:#f2f4f7;}");
    }
    modeTemplateBtn_->setChecked(true);
    modeTemplateBtn_->setToolTip("用一个全局模板生成：选好模板后点生成即可，无需逐个文件配置");
    modeCustomBtn_->setToolTip("只为当前选中的 PDF 单独编辑水印文字与样式");
    modeButtonGroup_ = new QButtonGroup(this);
    modeButtonGroup_->setExclusive(true);
    modeButtonGroup_->addButton(modeTemplateBtn_);
    modeButtonGroup_->addButton(modeCustomBtn_);
    headRow->addWidget(modeTemplateBtn_);
    headRow->addWidget(modeCustomBtn_);

    scopeLabel_ = new QLabel(rightContainer);
    scopeLabel_->setStyleSheet("color:#666666; font-size:12px;");
    scopeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    scopeLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headRow->addWidget(scopeLabel_, 1);
    rightLayout->addLayout(headRow);

    watermarkModeStack_ = new QStackedWidget(rightContainer);

    // ── Page 0: template mode ─────────────────────────────────────────────
    auto* tplPage = new QWidget(watermarkModeStack_);
    auto* tplPageLayout = new QVBoxLayout(tplPage);
    tplPageLayout->setContentsMargins(0, 0, 0, 0);
    tplPageLayout->setSpacing(8);

    auto* tplGroup = new QGroupBox("水印模板", tplPage);
    auto* tplLayout = new QVBoxLayout(tplGroup);
    tplLayout->setContentsMargins(10, 12, 10, 10);
    tplLayout->setSpacing(6);

    auto* tplRow1 = new QHBoxLayout();
    tplRow1->setSpacing(6);
    tplRow1->addWidget(new QLabel("模板:", tplGroup));
    templateCombo_ = new QComboBox(tplGroup);
    templateCombo_->setMinimumWidth(140);
    templateCombo_->setToolTip("选择一个已保存的全局模板（多行文字 + 一套样式）");
    tplRow1->addWidget(templateCombo_, 1);
    manageTemplateBtn_ = new QPushButton("管理...", tplGroup);
    manageTemplateBtn_->setToolTip("重命名或删除已保存的水印模板");
    tplRow1->addWidget(manageTemplateBtn_);
    tplLayout->addLayout(tplRow1);

    tplPreviewLabel_ = new QLabel(tplGroup);
    tplPreviewLabel_->setWordWrap(true);
    tplPreviewLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tplPreviewLabel_->setStyleSheet(
        "color:#3a3a3a; background:#f7f9fc; border:1px solid #dde3ea; "
        "border-radius:4px; padding:8px;");
    tplLayout->addWidget(tplPreviewLabel_);

    // Empty-state action: there is nothing to generate with until a template
    // exists, so offer the one step that fixes it.
    gotoCustomBtn_ = new QPushButton("去「自定义」创建模板", tplGroup);
    gotoCustomBtn_->setCursor(Qt::PointingHandCursor);
    gotoCustomBtn_->setStyleSheet("padding:5px 12px;");
    gotoCustomBtn_->setToolTip("在自定义模式里编辑水印文字与样式，再点「存为模板...」即可生成全局模板");
    tplLayout->addWidget(gotoCustomBtn_, 0, Qt::AlignLeft);
    tplPageLayout->addWidget(tplGroup);
    tplPageLayout->addStretch();

    // ── Page 1: custom mode (per-PDF editing) ─────────────────────────────
    auto* customPage = new QWidget(watermarkModeStack_);
    auto* customLayout = new QVBoxLayout(customPage);
    customLayout->setContentsMargins(0, 0, 0, 0);
    customLayout->setSpacing(8);

    customHintLabel_ = new QLabel(customPage);
    customHintLabel_->setWordWrap(true);
    customHintLabel_->setStyleSheet("color:#a06000; background:#fff8e6; "
                                    "border:1px solid #f0dca8; border-radius:4px; padding:6px;");
    customHintLabel_->setVisible(false);
    customLayout->addWidget(customHintLabel_);

    // Watermark rows scroll area
    watermarkScrollArea_ = new QScrollArea(customPage);
    watermarkScrollArea_->setWidgetResizable(true);
    watermarkScrollArea_->setFrameShape(QFrame::NoFrame);
    watermarkContainer_ = new QWidget(watermarkScrollArea_);
    watermarkLayout_ = new QVBoxLayout(watermarkContainer_);
    watermarkLayout_->setContentsMargins(0, 0, 0, 0);
    watermarkLayout_->setSpacing(4);
    watermarkLayout_->addStretch();
    watermarkScrollArea_->setWidget(watermarkContainer_);
    customLayout->addWidget(watermarkScrollArea_, 1);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(6);
    loadTemplateBtn_ = new QPushButton("从模板载入", customPage);
    loadTemplateBtn_->setStyleSheet("padding: 6px 14px;");
    loadTemplateBtn_->setToolTip("选择一个模板，把它的文字与样式填入当前 PDF 的编辑区（不生成），之后可自由微调");
    loadTemplateBtn_->setMenu(new QMenu(loadTemplateBtn_));
    addWatermarkBtn_ = new QPushButton("+ 添加水印", customPage);
    addWatermarkBtn_->setStyleSheet("padding: 6px 14px; font-weight: bold;");
    // "存为模板" lives with the watermark rows it saves, and always produces a
    // GLOBAL template (templates are never bound to a single PDF).
    saveTemplateBtn_ = new QPushButton("存为模板...", customPage);
    saveTemplateBtn_->setStyleSheet("padding: 6px 14px;");
    saveTemplateBtn_->setToolTip("把当前编辑的水印文字与样式保存为全局模板（可在「模板」模式套用到任意 PDF）");
    btnRow->addWidget(addWatermarkBtn_);
    btnRow->addWidget(loadTemplateBtn_);
    btnRow->addWidget(saveTemplateBtn_);
    btnRow->addStretch();
    customLayout->addLayout(btnRow);

    // Watermark style (per-PDF in custom mode)
    auto* paramGroup = new QGroupBox("水印样式设置", customPage);
    auto* grid = new QGridLayout(paramGroup);
    grid->setContentsMargins(10, 12, 10, 10);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    int row = 0;
    grid->addWidget(new QLabel("颜色深浅:"), row, 0);
    auto* depthBox = new QWidget(paramGroup);
    auto* depthLayout = new QHBoxLayout(depthBox);
    depthLayout->setContentsMargins(0, 0, 0, 0);
    depthLayout->setSpacing(8);
    depthSlider_ = new QSlider(Qt::Horizontal, depthBox);
    depthSlider_->setRange(5, 60);
    depthSlider_->setValue(15);
    depthSpin_ = new QSpinBox(depthBox);
    depthSpin_->setRange(5, 60);
    depthSpin_->setValue(15);
    depthSpin_->setSuffix(" %");
    depthSpin_->setToolTip("可直接键入精确数值（模板里保存的就是这个百分比）");
    depthValueLabel_ = new QLabel("适中", depthBox);
    depthValueLabel_->setStyleSheet("color:#777777; font-size:11px;");
    depthValueLabel_->setFixedWidth(30);
    depthLayout->addWidget(depthSlider_, 1);
    depthLayout->addWidget(depthSpin_);
    depthLayout->addWidget(depthValueLabel_);
    grid->addWidget(depthBox, row, 1, 1, 2);
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

    customLayout->addWidget(paramGroup);

    watermarkModeStack_->addWidget(tplPage);
    watermarkModeStack_->addWidget(customPage);
    rightLayout->addWidget(watermarkModeStack_, 1);

    // ── Output settings (shared by both modes) ───────────────────────────
    auto* outGroup = new QGroupBox("输出", rightContainer);
    auto* outGrid = new QGridLayout(outGroup);
    outGrid->setContentsMargins(10, 12, 10, 10);
    outGrid->setHorizontalSpacing(10);
    outGrid->setVerticalSpacing(10);

    outGrid->addWidget(new QLabel("输出目录:"), 0, 0);
    outputDirEdit_ = new QLineEdit(outGroup);
    outputDirEdit_->setPlaceholderText("默认与源文件相同目录");
    auto* browseBtn = new QPushButton("选择...", outGroup);
    browseBtn->setStyleSheet("padding: 4px 10px;");
    outGrid->addWidget(outputDirEdit_, 0, 1);
    outGrid->addWidget(browseBtn, 0, 2);
    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::onSelectOutputDir);

    // "打开生成文件夹" belongs with the output settings, not the file list.
    openFolderBtn_ = new QPushButton("打开生成文件夹", outGroup);
    openFolderBtn_->setEnabled(false);
    openFolderBtn_->setStyleSheet("padding: 4px 10px;");
    openFolderBtn_->setToolTip("在 Finder / 资源管理器中打开上次生成的 PDF 所在目录");
    connect(openFolderBtn_, &QPushButton::clicked, this, &MainWindow::onOpenOutputFolder);
    outGrid->addWidget(openFolderBtn_, 0, 3);
    rightLayout->addWidget(outGroup);

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
    progRow->addWidget(new QLabel("总体进度:"));
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

    // 性能模式紧挨着生成按钮，运行时最常调的开关就在这里
    // 预览提升为全局动作：模板模式预览模板，自定义模式预览当前编辑内容
    previewBtn_ = new QPushButton("预览", statusCard);
    previewBtn_->setStyleSheet("padding: 5px 12px;");
    previewBtn_->setToolTip("预览水印在 PDF 页面上的实际效果");
    connect(previewBtn_, &QPushButton::clicked, this, &MainWindow::onPreviewWatermark);
    ctlRow->addWidget(previewBtn_);
    ctlRow->addSpacing(8);

    ctlRow->addWidget(new QLabel("性能:", statusCard));
    perfCombo_ = new QComboBox(statusCard);
    perfCombo_->addItem("保守（1 个文件）", static_cast<int>(PerformanceMode::Low));
    perfCombo_->addItem("日常推荐（2 个文件）", static_cast<int>(PerformanceMode::Normal));
    perfCombo_->addItem("火力全开（最多 4 个）", static_cast<int>(PerformanceMode::High));
    perfCombo_->setCurrentIndex(1);
    perfCombo_->setMinimumWidth(150);
    perfCombo_->setToolTip("引擎限制：PDFium 非线程安全，页面解析与光栅化已全局串行；\n"
                           "并发只用于重叠水印绘制、JPEG 编码与磁盘 I/O。\n"
                           "保守：同一时刻只处理 1 个文件，最省内存\n"
                           "日常推荐：2 个文件并行，速度与内存均衡\n"
                           "火力全开：最多 4 个文件并行，大页/扫描件更快，内存占用更高");
    ctlRow->addWidget(perfCombo_);
    ctlRow->addSpacing(8);

    cancelBtn_ = new QPushButton("取消", statusCard);
    cancelBtn_->setEnabled(false);
    cancelBtn_->setStyleSheet("padding: 5px 12px;");
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::onCancelClicked);
    ctlRow->addWidget(cancelBtn_);

    startSelectedBtn_ = new QPushButton("生成当前 PDF", statusCard);
    startSelectedBtn_->setEnabled(false);
    startSelectedBtn_->setToolTip("生成当前 PDF（自定义模式下只生成已勾选的水印）");
    startSelectedBtn_->setStyleSheet(
        "QPushButton { background-color: #107c10; color: white; font-weight: bold; "
        "padding: 7px 16px; border-radius: 4px; font-size: 14px; }"
        "QPushButton:hover { background-color: #0b5c0b; }"
        "QPushButton:pressed { background-color: #094509; }"
        "QPushButton:disabled { background-color: #cccccc; color: #888888; }");
    connect(startSelectedBtn_, &QPushButton::clicked, this, &MainWindow::onStartSelectedClicked);
    ctlRow->addWidget(startSelectedBtn_);

    startAllBtn_ = new QPushButton("生成全部 PDF", statusCard);
    startAllBtn_->setToolTip("忽略勾选，生成列表中所有 PDF 的所有水印");
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
        if (depthSpin_->value() != val) {
            QSignalBlocker block(depthSpin_);
            depthSpin_->setValue(val);
        }
        depthValueLabel_->setText(depthHintText(val));
    });
    connect(depthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (depthSlider_->value() != val) depthSlider_->setValue(val);
    });

    // Keyboard shortcuts
    auto* openSc = new QShortcut(QKeySequence::Open, this);
    connect(openSc, &QShortcut::activated, this, &MainWindow::onAddFiles);
    auto* genSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(genSc, &QShortcut::activated, this, &MainWindow::onStartAllClicked);
    auto* escSc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escSc, &QShortcut::activated, this, [this]() {
        if (taskManager_.isRunning()) onCancelClicked();
    });

    // File selection change
    connect(fileTable_, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::onFileSelectionChanged);

    // Add watermark row button
    connect(addWatermarkBtn_, &QPushButton::clicked, this, &MainWindow::addWatermarkRow);

    // Watermark mode switch (segmented control)
    connect(modeTemplateBtn_, &QPushButton::toggled, this, &MainWindow::onWatermarkModeChanged);
    connect(modeCustomBtn_, &QPushButton::toggled, this, &MainWindow::onWatermarkModeChanged);
    connect(gotoCustomBtn_, &QPushButton::clicked, this, [this]() {
        modeCustomBtn_->setChecked(true);   // toggled -> onWatermarkModeChanged
    });

    // Watermark template controls
    connect(loadTemplateBtn_->menu(), &QMenu::aboutToShow, this, [this]() {
        QMenu* menu = loadTemplateBtn_->menu();
        menu->clear();
        for (const auto& tpl : templateStore_.templates()) {
            const QString name = QString::fromUtf8(tpl.name.c_str());
            QAction* act = menu->addAction(name);
            connect(act, &QAction::triggered, this, [this, name]() { loadTemplateByName(name); });
        }
        if (menu->isEmpty()) {
            QAction* none = menu->addAction("（暂无模板）");
            none->setEnabled(false);
        }
    });
    connect(saveTemplateBtn_, &QPushButton::clicked, this, &MainWindow::onSaveAsTemplate);
    connect(manageTemplateBtn_, &QPushButton::clicked, this, &MainWindow::onManageTemplates);
    connect(templateCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshTemplatePreview();
        updateUiState(taskManager_.isRunning());
    });

    // TaskManager signals
    connect(&taskManager_, &TaskManager::fileStarted, this, &MainWindow::onFileStarted);
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

    const int percent = static_cast<int>(style.opacity * 100.0 + 0.5);
    {
        QSignalBlocker blockSlider(depthSlider_);
        QSignalBlocker blockSpin(depthSpin_);
        depthSlider_->setValue(percent);
        depthSpin_->setValue(percent);
    }
    depthValueLabel_->setText(depthHintText(percent));
}

WatermarkConfig MainWindow::currentConfig() const {
    WatermarkConfig cfg = styleFromUi();
    // Prefer the first checked, non-empty row so the preview matches what
    // generating the current PDF will actually produce; else fall back to the
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
    refreshScopeLabel();
    refreshActionLabels();
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
    if (fileTable_->rowCount() > 0) {
        const auto answer = QMessageBox::question(
            this, "清空列表",
            QString("确定清空列表中的 %1 个 PDF 吗？\n\n"
                    "只会移除列表项，不会删除任何文件；已配置的水印也会一并清除。")
                .arg(fileTable_->rowCount()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }
    fileTable_->setRowCount(0);
    fileWatermarkConfigs_.clear();
    perPdfConfigMap_.clear();
    currentSelectedFile_.clear();
    fileNameToRow_.clear();
    taskManager_.clear();
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

    const bool hasTemplate = templateCombo_ && !templateCombo_->currentData().toString().isEmpty();
    const bool hasFile = !currentSelectedFile_.isEmpty();
    const bool hasRows = fileTable_->rowCount() > 0;

    // "生成当前 PDF" in custom mode needs at least one checked, non-empty watermark.
    // In template mode the template is applied automatically, so having a
    // template is enough.
    bool hasCheckedWatermark = false;
    auto itChecked = fileWatermarkConfigs_.find(currentSelectedFile_);
    if (hasFile && itChecked != fileWatermarkConfigs_.end()) {
        for (const auto& cfg : itChecked->second) {
            if (cfg.selected && !cfg.text.empty()) {
                hasCheckedWatermark = true;
                break;
            }
        }
    }

    const bool tplMode = isTemplateMode();
    if (tplMode) {
        startAllBtn_->setEnabled(!running && hasRows && hasTemplate);
        startSelectedBtn_->setEnabled(!running && hasFile && hasTemplate);
    } else {
        startAllBtn_->setEnabled(!running && hasRows);
        startSelectedBtn_->setEnabled(!running && hasCheckedWatermark);
    }

    // Mode switch
    modeTemplateBtn_->setEnabled(!running);
    modeCustomBtn_->setEnabled(!running);

    // Template-mode page
    templateCombo_->setEnabled(!running);
    manageTemplateBtn_->setEnabled(!running);
    gotoCustomBtn_->setEnabled(!running);

    // Custom-mode page (per-PDF editing)
    addWatermarkBtn_->setEnabled(!running && hasFile);
    loadTemplateBtn_->setEnabled(!running && hasFile && !templateStore_.templates().empty());
    saveTemplateBtn_->setEnabled(!running && hasFile);
    depthSlider_->setEnabled(!running);
    depthSpin_->setEnabled(!running);
    rotationSpin_->setEnabled(!running);
    fontCombo_->setEnabled(!running);
    boldCheck_->setEnabled(!running);
    italicCheck_->setEnabled(!running);

    // Shared
    previewBtn_->setEnabled(!running);
    perfCombo_->setEnabled(!running);
    outputDirEdit_->setEnabled(!running);
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

    if (QMessageBox::question(
            this, "删除选中文件",
            QString("确定从列表中移除选中的 %1 个文件吗？\n\n"
                    "只会移除列表项，不会删除磁盘上的 PDF，也不会删除已生成的输出。")
                .arg(selectedRows.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

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

bool MainWindow::confirmBatchRun(const std::vector<TaskManager::FileSubtask>& subtasks,
                                 bool onlySelected) {
    const WorkloadEstimate est = estimateCurrentWorkload(subtasks);
    const PerformanceMode mode =
        static_cast<PerformanceMode>(perfCombo_->currentData().toInt());
    const int cpuCap = std::max(1, WorkerPool::idealWorkerCount(mode));
    const int recommended = std::max(1, est.recommendedConcurrentDocs);
    const bool bigBatch = est.fileCount > 2 * recommended;
    const bool risky = needsPreflightWarning(est);

    auto applyPlan = [this](int docs, bool throttle) {
        taskManager_.setMaxConcurrentDocuments(docs);
        taskManager_.setThrottleMs(throttle ? kThrottleMs : 0);
    };
    auto mb = [](int64_t bytes) {
        return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 0);
    };

    if (suppressBatchConfirm_) {
        applyPlan(recommended, bigBatch);
        return true;
    }

    // Writing files cannot be undone, so always state exactly what is about to
    // happen and where it lands.
    const QString outDir = outputDirEdit_->text().trimmed().isEmpty()
        ? QStringLiteral("与源文件相同目录（按水印文字自动建子目录）")
        : outputDirEdit_->text().trimmed();

    QString sourceDesc;
    if (isTemplateMode()) {
        const WatermarkTemplate tpl = selectedTemplate();
        sourceDesc = QString("模板「%1」").arg(QString::fromUtf8(tpl.name.c_str()));
    } else if (onlySelected) {
        sourceDesc = QStringLiteral("当前 PDF 中已勾选的水印");
    } else {
        sourceDesc = QStringLiteral("各文件自己的水印配置");
    }

    QString detail = QString(
        "将生成 %1 个文件（共 %2 个 PDF）\n"
        "水印来源：%3\n"
        "输出目录：%4")
        .arg(subtasks.size())
        .arg(est.fileCount)
        .arg(sourceDesc)
        .arg(outDir);

    if (risky) {
        detail += QString(
            "\n\n资源评估：\n"
            "  • 处理页数：约 %1 页（页数未知的按 10 页估算：%2）\n"
            "  • 单文件峰值内存：约 %3 MB\n"
            "  • 推荐并发：%4 个文件（当前性能模式为 %5 个）\n"
            "  • 预计峰值内存：约 %6 MB，可用约 %7 MB\n"
            "  • 内存风险：%8")
            .arg(est.estimatedPages)
            .arg(est.pagesEstimated ? "是" : "否")
            .arg(mb(est.perDocPeakBytes))
            .arg(recommended)
            .arg(cpuCap)
            .arg(mb(std::max(est.peakBytes, est.perDocPeakBytes)))
            .arg(mb(est.usableRamBytes))
            .arg(QString::fromUtf8(workloadRiskLabel(est.risk)));
    }

    QMessageBox box(this);
    box.setWindowTitle(risky ? "开始前的资源检查" : "确认生成");
    box.setIcon(est.risk == WorkloadRisk::Risky ? QMessageBox::Warning
                                                : QMessageBox::Information);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    box.setText(detail);
    if (risky) {
        box.setInformativeText(est.risk == WorkloadRisk::Risky
            ? "按当前设置运行很可能因内存不足而崩溃或被系统终止，建议使用推荐设置。"
            : "建议使用推荐设置：程序会自动分批处理，并在必要时降速，以避免卡顿或崩溃。");
    }

    QPushButton* primaryBtn = nullptr;
    QPushButton* currentBtn = nullptr;
    QPushButton* cancelBtn = nullptr;
    if (risky) {
        primaryBtn = box.addButton("按推荐设置继续", QMessageBox::AcceptRole);
        currentBtn = box.addButton("按当前设置继续", QMessageBox::DestructiveRole);
        cancelBtn = box.addButton("取消", QMessageBox::RejectRole);
    } else {
        primaryBtn = box.addButton("开始生成", QMessageBox::AcceptRole);
        cancelBtn = box.addButton("取消", QMessageBox::RejectRole);
    }

    QCheckBox* dontAsk = new QCheckBox(risky ? "本次会话不再提示资源检查"
                                             : "本次会话不再提示生成确认", &box);
    box.setCheckBox(dontAsk);
    box.setDefaultButton(est.risk == WorkloadRisk::Risky ? cancelBtn : primaryBtn);

    box.exec();
    if (dontAsk->isChecked()) {
        if (risky) preflightSuppressed_ = true;
        else suppressBatchConfirm_ = true;
    }

    if (box.clickedButton() == cancelBtn) {
        statusLabel_->setText("已取消生成。");
        return false;
    }

    if (risky && box.clickedButton() == currentBtn) {
        if (est.risk == WorkloadRisk::Risky) {
            const auto answer = QMessageBox::warning(
                this, "确认风险",
                "预计峰值内存将超过当前可用内存，程序可能崩溃或被系统终止。\n\n"
                "确定仍按当前设置继续吗？",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                statusLabel_->setText("已取消生成。");
                return false;
            }
        }
        applyPlan(cpuCap, bigBatch);
        statusLabel_->setText(QString("按当前设置运行（并发 %1）。").arg(cpuCap));
        return true;
    }

    applyPlan(recommended, bigBatch);
    statusLabel_->setText(QString("开始生成（并发 %1%2）。")
        .arg(recommended)
        .arg(bigBatch ? QStringLiteral("，分批+限速") : QString()));
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

    // Template mode: the selected global template is applied automatically, so
    // the user never has to press "apply" first (and a stale apply can never be
    // used by accident).
    if (isTemplateMode()) {
        const WatermarkTemplate tpl = selectedTemplate();
        if (!tpl.isValid()) {
            QMessageBox::warning(this, "提示",
                "请先选择一个水印模板。\n\n"
                "还没有模板？点上面的「去「自定义」创建模板」。");
            return;
        }
        if (onlySelected) {
            if (currentSelectedFile_.isEmpty()) {
                QMessageBox::warning(this, "提示", "请先在左侧列表选择一个 PDF 文件。");
                return;
            }
            applyTemplateToCurrentUi(tpl);
        } else {
            applyTemplateToAllFiles(tpl);
        }
    }

    auto subtasks = buildAllConfigs(onlySelected);
    if (subtasks.empty()) {
        if (onlySelected) {
            QMessageBox::warning(this, "提示",
                "当前 PDF 没有已勾选的水印。\n\n"
                "操作：在「自定义」模式下勾选至少一条水印文字，再点「生成当前 PDF」。");
        } else {
            QMessageBox::warning(this, "提示",
                "列表中的文件还没有可用的水印。\n\n"
                "操作：在「自定义」模式下为文件添加水印文字，"
                "或用「存为模板...」存成模板后在「模板」模式下生成。");
        }
        return;
    }

    // Pre-flight memory/concurrency guard: may warn, may cancel, and always
    // caps concurrency so a huge batch cannot exhaust RAM.
    if (!confirmBatchRun(subtasks, onlySelected)) {
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
    WatermarkConfig cfg;
    if (isTemplateMode()) {
        const WatermarkTemplate tpl = selectedTemplate();
        if (!tpl.isValid()) {
            QMessageBox::information(this, "提示",
                "请先选择一个水印模板再预览；或在「自定义」中编辑水印后预览。");
            return;
        }
        cfg = tpl.style();
    } else {
        cfg = currentConfig();
    }
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

    const QString modeTag = isTemplateMode() ? QStringLiteral("模板预览 · ") : QString();
    auto* infoLabel = new QLabel(
        modeTag + QString("水印文字：%1   倾斜：%2°   字体：%3   深浅：%4%")
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

    refreshTemplatePreview();
    refreshScopeLabel();
    refreshActionLabels();

    // Enablement of the template buttons depends on whether a template is
    // selected, so refresh it here as well.
    updateUiState(taskManager_.isRunning());
}

bool MainWindow::isTemplateMode() const {
    return modeTemplateBtn_ && modeTemplateBtn_->isChecked();
}

void MainWindow::onWatermarkModeChanged() {
    if (!watermarkModeStack_) return;
    watermarkModeStack_->setCurrentIndex(isTemplateMode() ? 0 : 1);
    if (isTemplateMode()) refreshTemplatePreview();
    refreshScopeLabel();
    refreshActionLabels();
    updateUiState(taskManager_.isRunning());
}

void MainWindow::refreshActionLabels() {
    if (!startAllBtn_ || !startSelectedBtn_) return;
    int count = 0;
    if (isTemplateMode()) {
        const WatermarkTemplate tpl = selectedTemplate();
        for (const auto& wm : tpl.watermarks) {
            if (!wm.text.empty()) count++;
        }
        startAllBtn_->setText(count > 0
            ? QString("生成全部 PDF（每份 %1 条）").arg(count) : QString("生成全部 PDF"));
        startSelectedBtn_->setText(count > 0
            ? QString("生成当前 PDF（%1 条）").arg(count) : QString("生成当前 PDF"));
        startAllBtn_->setToolTip("把所选模板套用到列表中所有 PDF 并生成");
        startSelectedBtn_->setToolTip("把所选模板套用到当前 PDF 并生成");
    } else {
        auto it = fileWatermarkConfigs_.find(currentSelectedFile_);
        if (!currentSelectedFile_.isEmpty() && it != fileWatermarkConfigs_.end()) {
            for (const auto& cfg : it->second) {
                if (cfg.selected && !cfg.text.empty()) count++;
            }
        }
        startSelectedBtn_->setText(count > 0
            ? QString("生成当前 PDF（%1 条勾选）").arg(count) : QString("生成当前 PDF"));
        startAllBtn_->setText("生成全部 PDF");
        startAllBtn_->setToolTip("忽略勾选，生成列表中所有 PDF 的所有水印");
        startSelectedBtn_->setToolTip("生成当前 PDF 中已勾选的水印");
    }
}

void MainWindow::refreshScopeLabel() {
    if (!scopeLabel_) return;

    if (!isTemplateMode()) {
        const bool noFile = currentSelectedFile_.isEmpty();
        customHintLabel_->setVisible(noFile);
        if (noFile) {
            customHintLabel_->setText("请先在左侧选择一个 PDF 文件，再为它编辑水印文字与样式。");
        }
    } else {
        customHintLabel_->setVisible(false);
    }

    if (isTemplateMode()) {
        const WatermarkTemplate tpl = selectedTemplate();
        scopeLabel_->setText(QString("作用域：全部 %1 个 PDF · 模板「%2」")
            .arg(fileTable_->rowCount())
            .arg(tpl.isValid() ? QString::fromUtf8(tpl.name.c_str()) : QStringLiteral("未选择")));
        scopeLabel_->setStyleSheet(tpl.isValid()
            ? "color:#666666; font-size:12px;"
            : "color:#b26a00; font-size:12px; font-weight:bold;");
    } else if (currentSelectedFile_.isEmpty()) {
        scopeLabel_->setText("作用域：当前 PDF · 未选择文件");
        scopeLabel_->setStyleSheet("color:#b26a00; font-size:12px; font-weight:bold;");
    } else {
        scopeLabel_->setText(QString("作用域：当前 PDF · %1").arg(QFileInfo(currentSelectedFile_).fileName()));
        scopeLabel_->setStyleSheet("color:#666666; font-size:12px;");
    }
}

WatermarkTemplate MainWindow::selectedTemplate() const {
    const QString name = templateCombo_ ? templateCombo_->currentData().toString() : QString();
    if (name.isEmpty()) return WatermarkTemplate{};
    const WatermarkTemplate* found = templateStore_.find(name);
    return found ? *found : WatermarkTemplate{};
}

void MainWindow::refreshTemplatePreview() {
    if (!tplPreviewLabel_) return;

    const WatermarkTemplate tpl = selectedTemplate();
    if (gotoCustomBtn_) gotoCustomBtn_->setVisible(!tpl.isValid());
    if (!tpl.isValid()) {
        tplPreviewLabel_->setText(
            "还没有可用模板。\n\n"
            "模板是全局的（与具体 PDF 无关）：在「自定义」里写好水印文字与样式，"
            "点「存为模板...」即可保存，之后在这里选用。");
        refreshScopeLabel();
        refreshActionLabels();
        return;
    }

    QStringList lines;
    for (const auto& wm : tpl.watermarks) {
        if (wm.text.empty()) continue;
        lines << QString("  • %1%2")
            .arg(QString::fromUtf8(wm.text.c_str()))
            .arg(wm.selected ? QString() : QStringLiteral("（未勾选）"));
    }
    const WatermarkConfig st = tpl.style();
    tplPreviewLabel_->setText(
        QString("共 %1 条水印：\n%2\n\n样式：字号 %3pt · 倾斜 %4° · 深浅 %5% · 字体 %6%7%8")
            .arg(lines.size())
            .arg(lines.join("\n"))
            .arg(st.fontSizePt)
            .arg(st.rotationDegrees)
            .arg(static_cast<int>(st.opacity * 100.0 + 0.5))
            .arg(QString::fromUtf8(st.fontFamily.c_str()))
            .arg(st.fontBold ? QStringLiteral(" · 加粗") : QString())
            .arg(st.fontItalic ? QStringLiteral(" · 斜体") : QString()));
    refreshScopeLabel();
    refreshActionLabels();
}

int MainWindow::applyTemplateToAllFiles(const WatermarkTemplate& tpl) {
    int applied = 0;
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        QTableWidgetItem* item = fileTable_->item(r, 3);
        if (!item) continue;
        const QString path = item->text();
        if (path.isEmpty()) continue;
        applyTemplateToFile(tpl, path);
        ++applied;
    }
    if (!currentSelectedFile_.isEmpty()) {
        loadWatermarksForSelectedFile();
    }
    return applied;
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

void MainWindow::loadTemplateByName(const QString& name) {
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
    statusLabel_->setText(QString("已把模板「%1」载入当前 PDF 的编辑区，可继续微调。").arg(name));
}

void MainWindow::onSaveAsTemplate() {
    if (currentSelectedFile_.isEmpty()) {
        QMessageBox::warning(this, "提示",
            "请先在左侧选择一个 PDF 文件，并在「自定义模式」中编辑要保存的水印文字。");
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
    statusLabel_->setText(QString("模板「%1」已保存（全局模板，可在「模板模式」中套用）。").arg(name));
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