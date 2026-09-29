// PDFMark - Main GUI Window implementation.
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/TemplateListDelegate.h"
#include "common/Common.h"
#include "ui/TemplateEditDialog.h"
#include "ui/FileTableSelection.h"
#include "ui/PasswordDialog.h"
#include "diagnostics/Diagnostics.h"
#include "task/WorkerPool.h"
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
#include <QInputDialog>
#include <QCheckBox>
#include <QMap>
#include <QStackedWidget>
#include <QIcon>
#include <QMenu>
#include <QAction>
#include <QRadioButton>
#include <QStackedWidget>
#include <QThread>
#include <QSignalBlocker>
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

// File table layout. Only two columns are visible; everything else the logic
// needs is stored in data roles on the name item.

namespace {

// ── small builders so setupUi() reads like the layout it produces ──────────
QFrame* makeCard(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName("card");
    card->setFrameShape(QFrame::NoFrame);
    return card;
}

QLabel* makeSectionTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName("sectionTitle");
    return label;
}

QLabel* makeCountLabel(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setObjectName("chip");
    label->setTextFormat(Qt::RichText);
    return label;
}

QPushButton* makeButton(const QString& text, const char* kind, const QString& tip, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    if (kind && *kind) button->setObjectName(kind);
    if (!tip.isEmpty()) button->setToolTip(tip);
    return button;
}

// Centered hint shown instead of an empty list/table.
QWidget* makeEmptyPage(const QString& title, const QString& subtitle, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(6);
    layout->addStretch();
    auto* heading = new QLabel(title, page);
    heading->setObjectName("emptyTitle");
    heading->setAlignment(Qt::AlignCenter);
    auto* hint = new QLabel(subtitle, page);
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    hint->setObjectName("hint");
    layout->addWidget(heading);
    layout->addWidget(hint);
    layout->addStretch();
    return page;
}

} // namespace

static constexpr int kCheckColumn = 0;
static constexpr int kNameColumn  = 1;
static constexpr int kPathRole    = Qt::UserRole;
static constexpr int kPagesRole   = Qt::UserRole + 1;
static constexpr int kStatusRole  = Qt::UserRole + 2;


MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setupUi();
    setupConnections();
    setWindowTitle("PDFMark — PDF 水印固化工具");
    setMinimumSize(940, 620);
    resize(1180, 780);

    templateStore_.load();
    refreshTemplateList();
    updateUiState(false);

    // Auto-update: create the updater; the Help menu lives in setupUi().
    autoUpdater_ = new AutoUpdater(this);
    // Background silent check 2 seconds after startup
    QTimer::singleShot(2000, this, [this]() {
        connect(autoUpdater_, &AutoUpdater::updateAvailable,
                this, &MainWindow::onSilentUpdateAvailable);
        autoUpdater_->checkForUpdates(true);
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    // ── menu bar (in-window on Windows, in the system bar on macOS) ────────
    QMenu* fileMenu = menuBar()->addMenu("文件(&F)");
    fileMenu->addAction("添加 PDF 文件...(&O)", QKeySequence::Open, this, &MainWindow::onAddFiles);
    fileMenu->addAction("添加文件夹...(&D)", this, &MainWindow::onAddFolder);
    fileMenu->addSeparator();
    fileMenu->addAction("清空文件列表(&C)", this, &MainWindow::onClearFiles);
    fileMenu->addSeparator();
    fileMenu->addAction("退出(&X)", QKeySequence::Quit, this, &QWidget::close);

    QMenu* helpMenu = menuBar()->addMenu("帮助(&H)");
    helpMenu->addAction("检查更新(&U)...", this, &MainWindow::onCheckForUpdates);
    helpMenu->addAction("打开日志目录(&L)...", this, []() {
        const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
    });

    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);

    // ── header: product name + one-line hint ──────────────────────────────
    auto* header = new QHBoxLayout();
    header->setSpacing(8);
    auto* mark = new QLabel(central);
    mark->setPixmap(QIcon(":/icons/app_icon.png").pixmap(20, 20));
    auto* title = new QLabel(QString(
        "<span style='font-size:15px;font-weight:600;color:%1'>PDFMark</span>"
        "<span style='font-size:12px;color:%2'>&nbsp;&nbsp;v%3&nbsp;·&nbsp;PDF 水印固化工具</span>")
        .arg(QLatin1String(theme::kText), QLatin1String(theme::kTextMuted), PDFMARK_VERSION), central);
    auto* dragHint = new QLabel("拖拽 PDF 文件或文件夹到窗口即可添加", central);
    dragHint->setObjectName("hint");
    header->addWidget(mark);
    header->addWidget(title);
    header->addStretch();
    header->addWidget(dragHint);
    root->addLayout(header);

    // ── middle: file list (left) + templates & output (right) ─────────────
    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(12);

    // ── 待处理 PDF ────────────────────────────────────────────────────────
    auto* filesCard = makeCard(splitter);
    auto* filesLayout = new QVBoxLayout(filesCard);
    filesLayout->setContentsMargins(14, 12, 14, 12);
    filesLayout->setSpacing(10);

    auto* filesHead = new QHBoxLayout();
    filesHead->setSpacing(8);
    filesHead->addWidget(makeSectionTitle("待处理 PDF", filesCard));
    fileCountLabel_ = makeCountLabel(filesCard);
    filesHead->addWidget(fileCountLabel_);
    filesHead->addStretch();
    selectAllBtn_ = makeButton("全选", "ghost", "勾选列表中的所有 PDF", filesCard);
    selectNoneBtn_ = makeButton("全不选", "ghost", "取消勾选所有 PDF", filesCard);
    filesHead->addWidget(selectAllBtn_);
    filesHead->addWidget(selectNoneBtn_);
    filesLayout->addLayout(filesHead);

    auto* filesTools = new QHBoxLayout();
    filesTools->setSpacing(8);
    addFilesBtn_ = makeButton("添加 PDF 文件...", "", "选择一个或多个 PDF 文件", filesCard);
    addFolderBtn_ = makeButton("添加文件夹...", "", "添加整个文件夹中的 PDF", filesCard);
    removeSelBtn_ = makeButton("移除选中", "ghost", "仅从列表移除，不会删除磁盘上的文件", filesCard);
    clearFilesBtn_ = makeButton("清空", "ghost", "清空列表（不会删除任何文件）", filesCard);
    filesTools->addWidget(addFilesBtn_);
    filesTools->addWidget(addFolderBtn_);
    filesTools->addSpacing(6);
    filesTools->addWidget(removeSelBtn_);
    filesTools->addWidget(clearFilesBtn_);
    filesTools->addStretch();
    filesLayout->addLayout(filesTools);

    fileTable_ = new QTableWidget(0, 2, filesCard);
    fileTable_->setHorizontalHeaderLabels({"", "文件名"});
    if (auto* nameHeader = fileTable_->horizontalHeaderItem(kNameColumn)) {
        nameHeader->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    fileTable_->horizontalHeader()->setSectionResizeMode(kCheckColumn, QHeaderView::Fixed);
    fileTable_->horizontalHeader()->resizeSection(kCheckColumn, 40);
    fileTable_->horizontalHeader()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    fileTable_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fileTable_->horizontalHeader()->setHighlightSections(false);
    fileTable_->verticalHeader()->setVisible(false);
    fileTable_->verticalHeader()->setDefaultSectionSize(34);
    fileTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fileTable_->setShowGrid(false);
    fileTable_->setWordWrap(false);
    fileTable_->setFrameShape(QFrame::NoFrame);
    fileTable_->setAlternatingRowColors(false);
    fileTable_->setTextElideMode(Qt::ElideMiddle);
    fileTable_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    fileStack_ = new QStackedWidget(filesCard);
    fileStack_->addWidget(makeEmptyPage("还没有添加 PDF",
                                       "拖拽文件到窗口，或点击「添加 PDF 文件...」", filesCard));
    fileStack_->addWidget(fileTable_);
    filesLayout->addWidget(fileStack_, 1);
    splitter->addWidget(filesCard);

    auto* rightColumn = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(10);

    // ── 水印模板 ──────────────────────────────────────────────────────────
    // Watermarks exist only as global templates: one can be created without
    // loading any PDF.
    auto* tplCard = makeCard(rightColumn);
    auto* tplLayout = new QVBoxLayout(tplCard);
    tplLayout->setContentsMargins(14, 12, 14, 12);
    tplLayout->setSpacing(10);

    auto* tplHead = new QHBoxLayout();
    tplHead->setSpacing(8);
    tplHead->addWidget(makeSectionTitle("水印模板", tplCard));
    templateCountLabel_ = makeCountLabel(tplCard);
    tplHead->addWidget(templateCountLabel_);
    tplHead->addStretch();
    selectAllTemplatesBtn_ = makeButton("全选", "ghost", "勾选全部模板", tplCard);
    selectNoneTemplatesBtn_ = makeButton("全不选", "ghost", "取消勾选全部模板", tplCard);
    tplHead->addWidget(selectAllTemplatesBtn_);
    tplHead->addWidget(selectNoneTemplatesBtn_);
    tplLayout->addLayout(tplHead);

    templateList_ = new QListWidget(tplCard);
    templateList_->setSelectionMode(QAbstractItemView::SingleSelection);
    templateList_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    templateList_->setItemDelegate(new TemplateListDelegate(templateList_));
    templateList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    templateList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    templateList_->setToolTip("勾选的模板参与生成；双击可编辑");

    templateStack_ = new QStackedWidget(tplCard);
    templateStack_->addWidget(makeEmptyPage("还没有水印模板",
                                            "点击「新建模板...」创建（不需要先添加 PDF）", tplCard));
    templateStack_->addWidget(templateList_);
    tplLayout->addWidget(templateStack_, 1);

    auto* tplButtons = new QHBoxLayout();
    tplButtons->setSpacing(8);
    newTemplateBtn_ = makeButton("新建模板...", "", "新建一个全局水印模板（无需先添加 PDF）", tplCard);
    editTemplateBtn_ = makeButton("编辑...", "", "修改选中模板的名称 / 文字 / 样式 / 输出命名", tplCard);
    previewTemplateBtn_ = makeButton("预览", "ghost", "预览选中模板在 PDF 页面上的实际效果", tplCard);
    deleteTemplateBtn_ = makeButton("删除", "danger", "删除选中的模板（不会删除已生成的输出）", tplCard);
    tplButtons->addWidget(newTemplateBtn_);
    tplButtons->addWidget(editTemplateBtn_);
    tplButtons->addStretch();
    tplButtons->addWidget(previewTemplateBtn_);
    tplButtons->addWidget(deleteTemplateBtn_);
    tplLayout->addLayout(tplButtons);
    rightLayout->addWidget(tplCard, 1);

    // ── 输出 ──────────────────────────────────────────────────────────────
    auto* outCard = makeCard(rightColumn);
    auto* outLayout = new QVBoxLayout(outCard);
    outLayout->setContentsMargins(14, 12, 14, 12);
    outLayout->setSpacing(8);
    outLayout->addWidget(makeSectionTitle("输出", outCard));

    auto* outRow = new QHBoxLayout();
    outRow->setSpacing(8);
    outputDirEdit_ = new QLineEdit(outCard);
    outputDirEdit_->setPlaceholderText("默认与源 PDF 相同目录");
    auto* browseBtn = makeButton("选择...", "", "选择一个统一的输出目录", outCard);
    openFolderBtn_ = makeButton("打开生成文件夹", "ghost",
                                "在 Finder / 资源管理器中打开上次生成的目录", outCard);
    openFolderBtn_->setEnabled(false);
    outRow->addWidget(outputDirEdit_, 1);
    outRow->addWidget(browseBtn);
    outRow->addWidget(openFolderBtn_);
    outLayout->addLayout(outRow);

    auto* outHint = new QLabel("留空 = 写到源 PDF 所在目录；每个模板使用自己的子目录", outCard);
    outHint->setObjectName("hint");
    outLayout->addWidget(outHint);
    rightLayout->addWidget(outCard);

    splitter->addWidget(rightColumn);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({700, 460});
    root->addWidget(splitter, 1);

    // ── bottom action bar ─────────────────────────────────────────────────
    auto* actionBar = makeCard(central);
    auto* actionLayout = new QVBoxLayout(actionBar);
    actionLayout->setContentsMargins(14, 12, 14, 12);
    actionLayout->setSpacing(10);

    auto* actionRow = new QHBoxLayout();
    actionRow->setSpacing(10);
    summaryLabel_ = new QLabel(actionBar);
    summaryLabel_->setObjectName("chip");
    summaryLabel_->setTextFormat(Qt::RichText);
    summaryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cancelBtn_ = makeButton("取消", "ghost", "停止当前批次（已完成的文件会保留）", actionBar);
    cancelBtn_->setEnabled(false);
    startCheckedBtn_ = makeButton("生成勾选的 PDF", "", "只生成左侧已勾选的 PDF", actionBar);
    startCheckedBtn_->setEnabled(false);
    startAllBtn_ = makeButton("生成全部 PDF", "primary", "生成列表中的所有 PDF", actionBar);
    startAllBtn_->setEnabled(false);
    actionRow->addWidget(summaryLabel_, 1);
    actionRow->addWidget(cancelBtn_);
    actionRow->addWidget(startCheckedBtn_);
    actionRow->addWidget(startAllBtn_);
    actionLayout->addLayout(actionRow);

    auto* progressRow = new QHBoxLayout();
    progressRow->setSpacing(12);
    statusLabel_ = new QLabel("就绪。添加 PDF 并勾选模板后即可开始。", actionBar);
    statusLabel_->setObjectName("hint");
    totalProgressBar_ = new QProgressBar(actionBar);
    totalProgressBar_->setRange(0, 100);
    totalProgressBar_->setValue(0);
    totalProgressBar_->setTextVisible(true);
    totalProgressBar_->setFixedWidth(240);
    progressRow->addWidget(statusLabel_, 1);
    progressRow->addWidget(totalProgressBar_);
    actionLayout->addLayout(progressRow);
    root->addWidget(actionBar);

    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::onSelectOutputDir);
}

void MainWindow::setupConnections() {
    // Keyboard shortcuts
    auto* openSc = new QShortcut(QKeySequence::Open, this);
    connect(openSc, &QShortcut::activated, this, &MainWindow::onAddFiles);
    // 新建模板是主操作，给一个快捷键（Cmd/Ctrl+N）
    auto* newTplSc = new QShortcut(QKeySequence::New, this);
    connect(newTplSc, &QShortcut::activated, this, &MainWindow::onNewTemplate);
    auto* genSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(genSc, &QShortcut::activated, this, &MainWindow::onStartAllClicked);
    auto* escSc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escSc, &QShortcut::activated, this, [this]() {
        if (taskManager_.isRunning()) onCancelClicked();
    });

    // File selection change (row highlight = which PDF the right panel edits)
    connect(fileTable_, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::onFileSelectionChanged);

    // Checkbox toggles only change the batch scope, not the edit focus.
    connect(fileTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item && item->column() == kCheckColumn) {
            refreshActionLabels();
            updateUiState(taskManager_.isRunning());
        }
    });

    connect(addFilesBtn_, &QPushButton::clicked, this, &MainWindow::onAddFiles);
    connect(addFolderBtn_, &QPushButton::clicked, this, &MainWindow::onAddFolder);
    connect(removeSelBtn_, &QPushButton::clicked, this, &MainWindow::onRemoveSelectedFile);
    connect(clearFilesBtn_, &QPushButton::clicked, this, &MainWindow::onClearFiles);
    connect(openFolderBtn_, &QPushButton::clicked, this, &MainWindow::onOpenOutputFolder);
    connect(cancelBtn_, &QPushButton::clicked, this, &MainWindow::onCancelClicked);
    connect(startCheckedBtn_, &QPushButton::clicked, this, &MainWindow::onStartCheckedClicked);
    connect(startAllBtn_, &QPushButton::clicked, this, &MainWindow::onStartAllClicked);

    connect(selectAllBtn_, &QPushButton::clicked, this, &MainWindow::onSelectAllFiles);
    connect(selectNoneBtn_, &QPushButton::clicked, this, &MainWindow::onSelectNoFiles);

    // ── Templates ───────────────────────────────────────────────────────
    connect(newTemplateBtn_, &QPushButton::clicked, this, &MainWindow::onNewTemplate);
    connect(editTemplateBtn_, &QPushButton::clicked, this, &MainWindow::onEditTemplate);
    connect(deleteTemplateBtn_, &QPushButton::clicked, this, &MainWindow::onDeleteTemplate);
    connect(previewTemplateBtn_, &QPushButton::clicked, this, &MainWindow::onPreviewTemplate);
    connect(selectAllTemplatesBtn_, &QPushButton::clicked, this, &MainWindow::onSelectAllTemplates);
    connect(selectNoneTemplatesBtn_, &QPushButton::clicked, this, &MainWindow::onSelectNoneTemplates);
    connect(templateList_, &QListWidget::itemChanged, this, [this](QListWidgetItem*) {
        onTemplateItemChanged();
    });
    connect(templateList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        onEditTemplate();
    });
    connect(templateList_, &QListWidget::itemSelectionChanged, this, [this]() {
        refreshSummary();
        updateUiState(taskManager_.isRunning());
    });

    // TaskManager signals
    connect(&taskManager_, &TaskManager::fileStarted, this, &MainWindow::onFileStarted);
    connect(&taskManager_, &TaskManager::fileProgress, this, &MainWindow::onFileProgress);
    connect(&taskManager_, &TaskManager::fileFinished, this, &MainWindow::onFileFinished);
    connect(&taskManager_, &TaskManager::allFinished, this, &MainWindow::onAllFinished);
    connect(&taskManager_, &TaskManager::cancelled, this, &MainWindow::onCancelled);
    connect(&taskManager_, &TaskManager::concurrencyChanged, this, [this](int window) {
        statusLabel_->setText(QString("内存自适应：并发调整为 %1 个文件，继续处理…").arg(window));
    });
    connect(&taskManager_, &TaskManager::passwordRequired, this, &MainWindow::onPasswordRequired);
    connect(&taskManager_, &TaskManager::errorOccurred, this, [this](const QString& msg) {
        QMessageBox::critical(this, "错误", "任务处理发生异常：\n" + msg);
    });
}

void MainWindow::onFileSelectionChanged() {
    auto items = fileTable_->selectedItems();
    if (items.isEmpty()) {
        currentSelectedFile_.clear();
    } else {
        int row = fileTable_->row(items.first());
        currentSelectedFile_ = pathAt(row);
    }
    refreshSummary();
    updateUiState(false);
}

// One funnel for "the PDF list changed": counts, button labels, enabled state.
void MainWindow::refreshFileListUi() {
    rebuildFileIndex();
    refreshSummary();
    refreshActionLabels();
    updateUiState(taskManager_.isRunning());
}

void MainWindow::onAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(
        this, "选择 PDF 文件", QString(), "PDF 文件 (*.pdf)"
    );
    if (files.isEmpty()) return;

    bool wasEmpty = fileTable_->rowCount() == 0;
    for (const auto& file : files) {
        appendFileRow(file);
    }

    refreshFileListUi();
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
        appendFileRow(fi.absoluteFilePath());
    }
    refreshFileListUi();
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

        appendFileRow(file);
    }
    refreshFileListUi();
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
    currentSelectedFile_.clear();
    fileNameToRow_.clear();
    taskManager_.clear();
    totalProgressBar_->setValue(0);
    refreshSummary();
    updateUiState(false);
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
        const QString filePath = pathAt(row);
        if (filePath.isEmpty()) continue;
        fileTable_->removeRow(row);
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
    }

    rebuildFileIndex();
    refreshSummary();
    updateUiState(taskManager_.isRunning());
    statusLabel_->setText(QString("已移除 %1 个文件。剩余 %2 个文件。")
        .arg(selectedRows.size()).arg(remaining));
}
void MainWindow::appendFileRow(const QString& filePath) {
    try {
        taskManager_.addFile(qstringToPath(filePath));

        const int row = fileTable_->rowCount();
        fileTable_->insertRow(row);

        // Column 0: participation checkbox. Set the state BEFORE inserting so
        // construction does not emit itemChanged.
        auto* checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        checkItem->setCheckState(Qt::Checked);
        checkItem->setToolTip("勾选：参与「生成勾选的 PDF」");
        fileTable_->setItem(row, kCheckColumn, checkItem);

        // Column 1: file name; path / pages / status live in hidden data roles.
        auto* nameItem = new QTableWidgetItem(QFileInfo(filePath).fileName());
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        fileTable_->setItem(row, kNameColumn, nameItem);
        setPathAt(row, filePath);

        // Page count is needed by the memory budget, so it is always computed
        // (encrypted / unreadable files fall back to the 10-page assumption).
        int pages = 0;
        try {
            auto docRef = PdfDocument::open(qstringToPath(filePath));
            pages = PdfDocument::pageCount(docRef.first.get());
        } catch (...) {
            pages = 0;
        }
        setPagesAt(row, pages);
        setStatus(row, QStringLiteral("等待处理"));
    } catch (const std::exception& e) {
        qWarning() << "Failed to add file:" << filePath << e.what();
    } catch (...) {
        qWarning() << "Unknown error adding file:" << filePath;
    }
}

QString MainWindow::pathAt(int row) const {
    QTableWidgetItem* item = fileTable_->item(row, kNameColumn);
    return item ? item->data(kPathRole).toString() : QString();
}

void MainWindow::setPathAt(int row, const QString& path) {
    if (QTableWidgetItem* item = fileTable_->item(row, kNameColumn)) {
        item->setData(kPathRole, path);
    }
}

int MainWindow::pagesAt(int row) const {
    QTableWidgetItem* item = fileTable_->item(row, kNameColumn);
    return item ? item->data(kPagesRole).toInt() : 0;
}

void MainWindow::setPagesAt(int row, int pages) {
    if (QTableWidgetItem* item = fileTable_->item(row, kNameColumn)) {
        item->setData(kPagesRole, pages);
    }
}

void MainWindow::setStatus(int row, const QString& status, const QString& tooltip) {
    QTableWidgetItem* item = fileTable_->item(row, kNameColumn);
    if (!item) return;
    item->setData(kStatusRole, status);
    item->setToolTip(tooltip.isEmpty() ? status : tooltip);

    // Status is expressed by colour + tooltip instead of a dedicated column.
    if (status.startsWith(QStringLiteral("失败"))) {
        item->setForeground(QColor(0xC0, 0x2B, 0x2B));
    } else if (status.startsWith(QStringLiteral("完成"))) {
        item->setForeground(QColor(0x10, 0x7C, 0x10));
    } else if (status.startsWith(QStringLiteral("处理中"))) {
        item->setForeground(QColor(0x00, 0x78, 0xD4));
    } else {
        item->setData(Qt::ForegroundRole, QVariant());
    }
}

bool MainWindow::isCheckedAt(int row) const {
    QTableWidgetItem* item = fileTable_->item(row, kCheckColumn);
    return item && item->checkState() == Qt::Checked;
}

void MainWindow::setCheckedAt(int row, bool on) {
    if (QTableWidgetItem* item = fileTable_->item(row, kCheckColumn)) {
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
}

std::vector<QString> MainWindow::checkedPaths() const {
    std::vector<QString> out;
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        if (!isCheckedAt(r)) continue;
        const QString p = pathAt(r);
        if (!p.isEmpty()) out.push_back(p);
    }
    return out;
}

int MainWindow::checkedCount() const {
    int n = 0;
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        if (isCheckedAt(r)) ++n;
    }
    return n;
}

void MainWindow::onSelectAllFiles() {
    QSignalBlocker block(fileTable_);
    for (int r = 0; r < fileTable_->rowCount(); ++r) setCheckedAt(r, true);
    refreshActionLabels();
    updateUiState(taskManager_.isRunning());
}

void MainWindow::onSelectNoFiles() {
    QSignalBlocker block(fileTable_);
    for (int r = 0; r < fileTable_->rowCount(); ++r) setCheckedAt(r, false);
    refreshActionLabels();
    updateUiState(taskManager_.isRunning());
}

void MainWindow::rebuildFileIndex() {
    fileNameToRow_.clear();
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        QTableWidgetItem* nameItem = fileTable_->item(r, kNameColumn);
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
    // Concurrency is decided automatically from cores + RAM + workload.
    in.cpuCap = cpuBasedConcurrency(QThread::idealThreadCount());

    // Page counts come from the file table (column 1); "待检测" => unknown (0).
    std::unordered_map<std::string, int> pagesByPath;
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        const QString p = pathAt(r);
        if (p.isEmpty()) continue;
        pagesByPath[pathToString(qstringToPath(p))] = pagesAt(r);
    }

    std::unordered_map<std::string, size_t> indexOf;
    for (const auto& st : subtasks) {
        in.maxDpi = std::max(in.maxDpi, st.tpl.style().dpi);
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
                                 BatchScope scope) {
    const WorkloadEstimate est = estimateCurrentWorkload(subtasks);
    const int cores = (std::max)(1, QThread::idealThreadCount());
    const ConcurrencyPolicy policy = planConcurrency(est, cores);
    const int recommended = policy.maxConcurrentDocs;
    const bool bigBatch = policy.throttleMs > 0;
    const bool risky = needsPreflightWarning(est);

    auto applyPlan = [this, &policy](int docs, bool throttle) {
        ConcurrencyPolicy applied = policy;
        applied.maxConcurrentDocs = docs;
        applied.poolThreads = docs;
        applied.throttleMs = throttle ? kAutoThrottleMs : 0;
        taskManager_.setConcurrencyPolicy(applied);
    };
    auto mb = [](int64_t bytes) {
        return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 0);
    };

    if (suppressBatchConfirm_) {
        applyPlan(recommended, bigBatch);
        return true;
    }

    const int64_t totalRam = MemoryProbe::totalPhysicalBytes();
    const QString autoLine = QString(
        "自动并发：%1 个文件（%2 核 · 内存 %3 GB · 内存预算 %4 MB · 最大 PDF %5 MB）")
        .arg(recommended)
        .arg(cores)
        .arg(totalRam > 0 ? QString::number(static_cast<double>(totalRam) / (1024.0 * 1024.0 * 1024.0), 'f', 0)
                          : QStringLiteral("?"))
        .arg(mb(policy.ramBudgetBytes))
        .arg(mb(est.maxDocSourceBytes));

    // Writing files cannot be undone, so always state exactly what is about to
    // happen and where it lands.
    const QString outDir = outputDirEdit_->text().trimmed().isEmpty()
        ? QStringLiteral("与源文件相同目录（按模板的输出文件夹自动建子目录）")
        : outputDirEdit_->text().trimmed();

    const std::vector<WatermarkTemplate> tpls = checkedTemplates();
    QStringList tplNames;
    for (const auto& t : tpls) tplNames << QString::fromUtf8(t.name.c_str());
    const int pdfs = (scope == BatchScope::Checked) ? checkedCount() : fileTable_->rowCount();
    const QString sourceDesc = QString("%1 个 PDF × %2 个模板 = %3 个输出文件")
        .arg(pdfs).arg(tpls.size()).arg(subtasks.size());

    QString detail = QString(
        "%1\n"
        "模板：%2\n"
        "输出目录：%3")
        .arg(sourceDesc)
        .arg(tplNames.isEmpty() ? QStringLiteral("（无）") : tplNames.join(QStringLiteral("、")))
        .arg(outDir);

    // Spell the grouping out: several styles of one text are supposed to land in
    // ONE folder, and that is much easier to trust when it is printed here.
    if (!tpls.empty()) {
        // folder -> [variant suffixes] (empty list == no variant suffix)
        QMap<QString, QStringList> byFolder;
        for (const auto& t : tpls) {
            const QString folder = QString::fromUtf8(t.folderName().c_str());
            QString label = t.variantSuffix.empty()
                ? QString::fromUtf8(t.name.c_str())
                : QStringLiteral("文件名后缀 _%1").arg(QString::fromUtf8(t.variantSuffix.c_str()));
            byFolder[folder] << label;
        }
        QStringList folderLines;
        for (auto it = byFolder.constBegin(); it != byFolder.constEnd(); ++it) {
            folderLines << QStringLiteral("  • %1/ ← %2").arg(it.key(), it.value().join(QStringLiteral("、")));
        }
        detail += QStringLiteral("\n输出文件夹分组：\n") + folderLines.join(QStringLiteral("\n"));
    }

    detail += "\n" + autoLine;
    if (risky) {
        detail += QString(
            "\n\n资源评估：\n"
            "  • 处理页数：约 %1 页（页数未知的按 10 页估算：%2）\n"
            "  • 单文件峰值内存：约 %3 MB\n"
            "  • 预计峰值内存：约 %4 MB，可用约 %5 MB\n"
            "  • 内存风险：%6\n"
            "运行中会按实际内存占用自动增减并发并限速。")
            .arg(est.estimatedPages)
            .arg(est.pagesEstimated ? "是" : "否")
            .arg(mb(est.perDocPeakBytes))
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

    QPushButton* primaryBtn = box.addButton(
        est.risk == WorkloadRisk::Risky ? "仍要生成（自动降速）" : "开始生成",
        QMessageBox::AcceptRole);
    QPushButton* cancelBtn = box.addButton("取消", QMessageBox::RejectRole);

    QCheckBox* dontAsk = new QCheckBox("本次会话不再提示生成确认", &box);
    box.setCheckBox(dontAsk);
    box.setDefaultButton(est.risk == WorkloadRisk::Risky ? cancelBtn : primaryBtn);

    box.exec();
    if (dontAsk->isChecked()) {
        suppressBatchConfirm_ = true;
    }

    if (box.clickedButton() == cancelBtn) {
        statusLabel_->setText("已取消生成。");
        return false;
    }

    applyPlan(recommended, bigBatch);
    statusLabel_->setText(QString("开始生成（并发 %1 · 自动%2）。")
        .arg(recommended)
        .arg(bigBatch ? QStringLiteral(" · 已限速") : QString()));
    return true;
}

void MainWindow::onStartAllClicked() {
    runBatch(BatchScope::All);
}

void MainWindow::onStartCheckedClicked() {
    runBatch(BatchScope::Checked);
}

void MainWindow::runBatch(BatchScope scope) {
    if (fileTable_->rowCount() == 0) {
        QMessageBox::warning(this, "提示", "请先添加至少一个 PDF 文件。");
        return;
    }
    if (scope == BatchScope::Checked && checkedCount() == 0) {
        QMessageBox::warning(this, "提示",
            "请先在左侧勾选至少一个 PDF。\n\n"
            "（也可以用「全选」勾选全部，或直接点「生成全部 PDF」。）");
        return;
    }

    if (checkedTemplateCount() == 0) {
        QMessageBox::warning(this, "提示",
            "请先在上方勾选至少一个水印模板。\n\n"
            "还没有模板？点「新建模板...」，模板是全局的，不需要先添加 PDF。");
        return;
    }

    auto subtasks = buildSubtasks(scope);
    if (subtasks.empty()) {
        QMessageBox::warning(this, "提示", "没有可生成的组合，请检查勾选的 PDF 与模板。");
        return;
    }

    // Pre-flight memory/concurrency guard: may warn, may cancel, and always
    // caps concurrency so a huge batch cannot exhaust RAM.
    if (!confirmBatchRun(subtasks, scope)) {
        return;
    }

    taskManager_.clear();
    taskManager_.setSubtasks(subtasks);
    if (!outputDirEdit_->text().trimmed().isEmpty()) {
        taskManager_.setOutputDirectory(qstringToPath(outputDirEdit_->text().trimmed()));
    } else {
        taskManager_.setOutputDirectory(fs::path());
    }

    updateUiState(true);
    taskManager_.start();
}
void MainWindow::onFileStarted(const QString& fileName, int index, int total) {
    const int win = std::max(1, taskManager_.currentWindow());
    const int planned = std::max(win, taskManager_.effectiveConcurrency());
    QString pace = QString("并发 %1 · 自动").arg(win);
    if (win < planned) {
        pace += QString("（内存自适应 %1→%2）").arg(planned).arg(win);
    }
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
        setStatus(it.value(), QStringLiteral("处理中"));
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
    if (result.totalPages > 0) setPagesAt(row, result.totalPages);

    const QString wm = QString::fromUtf8(result.watermarkText.c_str());
    if (result.success) {
        setStatus(row, QStringLiteral("完成"),
                  QString("完成 · 水印：%1").arg(wm));
    } else {
        setStatus(row, QStringLiteral("失败"),
                  QString("失败 · 水印：%1\n原因：%2")
                      .arg(wm, QString::fromUtf8(result.errorMessage.c_str())));
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
// ── Templates ───────────────────────────────────────────────────────────────

void MainWindow::refreshTemplateList(const QString& select) {
    if (!templateList_) return;
    const QString previous = templateList_->currentItem()
        ? templateList_->currentItem()->data(Qt::UserRole).toString() : QString();

    QSignalBlocker block(templateList_);
    templateList_->clear();
    for (const auto& tpl : templateStore_.templates()) {
        const QString name = QString::fromUtf8(tpl.name.c_str());
        int lines = 0;
        QStringList texts;
        for (const auto& wm : tpl.watermarks) {
            if (wm.text.empty()) continue;
            ++lines;
            texts << QStringLiteral("  • %1").arg(QString::fromUtf8(wm.text.c_str()));
        }
        const WatermarkConfig st = tpl.style();

        // The delegate draws two lines: the name (line 1) plus a muted meta line
        // and the output folder chip, so long names no longer need a horizontal
        // scrollbar and the row stays readable.
        const QString folder = QString::fromUtf8(tpl.folderName().c_str());
        QStringList meta;
        meta << QStringLiteral("%1 行 · %2pt · %3% · %4°")
                    .arg(lines).arg(st.fontSizePt)
                    .arg(static_cast<int>(st.opacity * 100.0 + 0.5))
                    .arg(st.rotationDegrees);
        if (!tpl.variantSuffix.empty()) {
            meta << QStringLiteral("变体 %1").arg(QString::fromUtf8(tpl.variantSuffix.c_str()));
        }
        const bool merged = !tpl.outputFolder.empty() && tpl.outputFolder != tpl.name;
        const QString folderChip = merged ? QStringLiteral("%1/（合并）").arg(folder)
                                          : QStringLiteral("%1/").arg(folder);

        auto* item = new QListWidgetItem(name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        // Set the state before insertion so building the list emits no signals.
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, name);
        item->setData(TemplateListDelegate::MetaRole, meta.join(QStringLiteral(" · ")));
        item->setData(TemplateListDelegate::FolderRole, folderChip);
        item->setToolTip(QString("模板「%1」\n水印文字：\n%2\n\n"
                                 "勾选 = 参与生成；输出目录：%3/；文件名后缀：%4")
            .arg(name).arg(texts.join("\n"))
            .arg(folder)
            .arg(tpl.variantSuffix.empty()
                     ? QStringLiteral("（无）")
                     : QString("_%1").arg(QString::fromUtf8(tpl.variantSuffix.c_str()))));
        templateList_->addItem(item);
    }

    QString want = select.isEmpty() ? previous : select;
    int row = -1;
    for (int i = 0; i < templateList_->count(); ++i) {
        if (templateList_->item(i)->data(Qt::UserRole).toString() == want) { row = i; break; }
    }
    if (row < 0 && templateList_->count() > 0) row = 0;
    if (row >= 0) templateList_->setCurrentRow(row);
    templateList_->blockSignals(false);

    refreshSummary();
    refreshActionLabels();
}

WatermarkTemplate MainWindow::selectedTemplate() const {
    if (!templateList_ || !templateList_->currentItem()) return WatermarkTemplate{};
    const QString name = templateList_->currentItem()->data(Qt::UserRole).toString();
    const WatermarkTemplate* found = templateStore_.find(name);
    return found ? *found : WatermarkTemplate{};
}

bool MainWindow::isTemplateCheckedAt(int row) const {
    QListWidgetItem* item = templateList_ ? templateList_->item(row) : nullptr;
    return item && item->checkState() == Qt::Checked;
}

void MainWindow::setTemplateChecked(int row, bool on) {
    if (QListWidgetItem* item = templateList_ ? templateList_->item(row) : nullptr) {
        item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    }
}

std::vector<WatermarkTemplate> MainWindow::checkedTemplates() const {
    std::vector<WatermarkTemplate> out;
    if (!templateList_) return out;
    for (int i = 0; i < templateList_->count(); ++i) {
        if (!isTemplateCheckedAt(i)) continue;
        const QString name = templateList_->item(i)->data(Qt::UserRole).toString();
        const WatermarkTemplate* tpl = templateStore_.find(name);
        if (tpl && tpl->isValid()) out.push_back(*tpl);
    }
    return out;
}

int MainWindow::checkedTemplateCount() const {
    if (!templateList_) return 0;
    int n = 0;
    for (int i = 0; i < templateList_->count(); ++i) {
        if (isTemplateCheckedAt(i)) ++n;
    }
    return n;
}

void MainWindow::onTemplateItemChanged() {
    refreshSummary();
    refreshActionLabels();
    updateUiState(taskManager_.isRunning());
}

void MainWindow::onSelectAllTemplates() {
    QSignalBlocker block(templateList_);
    for (int i = 0; i < templateList_->count(); ++i) setTemplateChecked(i, true);
    onTemplateItemChanged();
}

void MainWindow::onSelectNoneTemplates() {
    QSignalBlocker block(templateList_);
    for (int i = 0; i < templateList_->count(); ++i) setTemplateChecked(i, false);
    onTemplateItemChanged();
}

void MainWindow::onNewTemplate() {
    // A colliding name no longer overwrites: the dialog renames the NEW template
    // to a sibling ("机密 (2)") and merges its output into the same folder, which
    // is exactly the "same text, different style" case.
    TemplateEditDialog dlg(WatermarkTemplate{}, true,
                           templateStore_.names(), templateStore_.folderNames(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const WatermarkTemplate tpl = dlg.result();
    const QString name = QString::fromUtf8(tpl.name.c_str());
    if (templateStore_.find(name)) return;   // defensive: must not happen

    templateStore_.addOrReplace(tpl);
    templateStore_.save();
    refreshTemplateList(name);
    updateUiState(taskManager_.isRunning());
    statusLabel_->setText(templateSavedMessage(tpl, QStringLiteral("已保存")));
}

void MainWindow::onEditTemplate() {
    const WatermarkTemplate cur = selectedTemplate();
    if (!cur.isValid()) {
        QMessageBox::information(this, "提示", "请先在列表中选择一个模板。");
        return;
    }

    // Editing in place must stay possible, so the template's own name is not in
    // the taken list; renaming onto ANOTHER template's name is de-duplicated
    // instead of overwriting it.
    QStringList taken = templateStore_.names();
    taken.removeAll(QString::fromUtf8(cur.name.c_str()));
    TemplateEditDialog dlg(cur, false, taken, templateStore_.folderNames(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const WatermarkTemplate edited = dlg.result();
    const QString oldName = QString::fromUtf8(cur.name.c_str());
    const QString newName = QString::fromUtf8(edited.name.c_str());

    if (newName != oldName) {
        templateStore_.remove(oldName);
    }
    templateStore_.addOrReplace(edited);
    templateStore_.save();
    refreshTemplateList(newName);
    updateUiState(taskManager_.isRunning());
    statusLabel_->setText(templateSavedMessage(edited, QStringLiteral("已更新")));
}

// Status-bar text after a save. When the output folder was pinned to another
// template's folder, say so: the user asked for "several styles, ONE folder",
// and this is the confirmation that it happened.
QString MainWindow::templateSavedMessage(const WatermarkTemplate& tpl, const QString& verb) const {
    const QString name = QString::fromUtf8(tpl.name.c_str());
    const QString folder = QString::fromUtf8(tpl.folderName().c_str());
    if (!tpl.outputFolder.empty() && folder != name) {
        return QString("模板「%1」%2，输出合并到「%3」文件夹。").arg(name, verb, folder);
    }
    return QString("模板「%1」%2。").arg(name, verb);
}

void MainWindow::onDeleteTemplate() {
    const WatermarkTemplate cur = selectedTemplate();
    if (!cur.isValid()) {
        QMessageBox::information(this, "提示", "请先在列表中选择一个模板。");
        return;
    }
    const QString name = QString::fromUtf8(cur.name.c_str());
    if (QMessageBox::question(this, "删除模板",
            QString("确定删除模板「%1」？\n\n已生成的输出文件不会被删除。").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    templateStore_.remove(name);
    templateStore_.save();
    refreshTemplateList();
    updateUiState(taskManager_.isRunning());
    statusLabel_->setText(QString("模板「%1」已删除。").arg(name));
}

void MainWindow::onPreviewTemplate() {
    const WatermarkTemplate tpl = selectedTemplate();
    if (!tpl.isValid()) {
        QMessageBox::information(this, "提示", "请先在列表中选择一个模板。");
        return;
    }
    const std::vector<WatermarkConfig> lines = templateToConfigs(tpl);
    const QImage previewImg = WatermarkRenderer::renderPreview(600, 800, lines);
    const WatermarkConfig st = tpl.style();

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(QString("模板预览 - %1").arg(QString::fromUtf8(tpl.name.c_str())));
    dlg->resize(640, 880);
    auto* layout = new QVBoxLayout(dlg);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    QStringList texts;
    for (const auto& line : lines) texts << QString::fromUtf8(line.text.c_str());
    auto* infoLabel = new QLabel(
        QString("模板「%1」 · %2 条水印\n%3\n样式：倾斜 %4° · 字体 %5 · 深浅 %6%")
            .arg(QString::fromUtf8(tpl.name.c_str()))
            .arg(texts.size())
            .arg(texts.join(QStringLiteral("、  ")))
            .arg(st.rotationDegrees)
            .arg(QString::fromUtf8(st.fontFamily.c_str()))
            .arg(static_cast<int>(st.opacity * 100.0 + 0.5)),
        dlg);
    infoLabel->setWordWrap(true);
    infoLabel->setObjectName("chip");
    layout->addWidget(infoLabel);

    auto* imgLabel = new QLabel(dlg);
    imgLabel->setObjectName("previewFrame");
    imgLabel->setAlignment(Qt::AlignCenter);
    imgLabel->setPixmap(QPixmap::fromImage(previewImg));
    layout->addWidget(imgLabel, 1);

    auto* closeBtn = makeButton("关闭", "", "关闭预览", dlg);
    connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::accept);
    auto* btnBox = new QHBoxLayout();
    btnBox->addStretch();
    btnBox->addWidget(closeBtn);
    layout->addLayout(btnBox);

    dlg->exec();
    delete dlg;
}

void MainWindow::refreshSummary() {
    if (!summaryLabel_) return;
    const int files = checkedCount();
    const int totalFiles = fileTable_->rowCount();
    const int tpls = checkedTemplateCount();
    const int totalTpls = templateList_ ? templateList_->count() : 0;

    // Count chips in the two cards.
    if (fileCountLabel_) {
        fileCountLabel_->setText(
            theme::chip("已勾选", QString("%1 / %2").arg(files).arg(totalFiles)));
    }
    if (templateCountLabel_) {
        templateCountLabel_->setText(
            theme::chip("已选", QString("%1 / %2").arg(tpls).arg(totalTpls)));
    }

    // Empty states: a hint page instead of a blank table/list.
    if (fileStack_) fileStack_->setCurrentIndex(totalFiles == 0 ? 0 : 1);
    if (templateStack_) templateStack_->setCurrentIndex(totalTpls == 0 ? 0 : 1);

    summaryLabel_->setText(
        theme::chip("待处理", QStringLiteral("%1 个 PDF").arg(totalFiles)) + theme::separator() +
        theme::chip("已选模板", QStringLiteral("%1 个").arg(tpls)) + theme::separator() +
        theme::chip("本次输出", QStringLiteral("%1 个文件").arg(files * tpls)));
    summaryLabel_->setToolTip(
        totalFiles == 0
            ? QStringLiteral("先添加 PDF，再勾选要使用的模板")
            : QStringLiteral("勾选的 PDF × 勾选的模板 = 每个模板输出一个子目录"));
}

void MainWindow::refreshActionLabels() {
    if (!startAllBtn_ || !startCheckedBtn_) return;
    const int files = checkedCount();
    const int allFiles = fileTable_->rowCount();
    const int tpls = checkedTemplateCount();

    startCheckedBtn_->setText(files > 0
        ? QString("生成 %1 个输出（%2 PDF × %3 模板）").arg(files * tpls).arg(files).arg(tpls)
        : QString("生成勾选的 PDF"));
    startAllBtn_->setText(allFiles > 0
        ? QString("生成全部 PDF（%1 × %2 = %3 个输出）").arg(allFiles).arg(tpls).arg(allFiles * tpls)
        : QString("生成全部 PDF"));

    startCheckedBtn_->setToolTip(QString("勾选的 %1 个 PDF × %2 个模板 → 输出 %3 个文件")
        .arg(files).arg(tpls).arg(files * tpls));
    startAllBtn_->setToolTip(QString("列表中全部 %1 个 PDF × %2 个模板 → 输出 %3 个文件")
        .arg(allFiles).arg(tpls).arg(allFiles * tpls));
}

std::vector<TaskManager::FileSubtask> MainWindow::buildSubtasks(BatchScope scope) {
    std::vector<TaskManager::FileSubtask> subtasks;
    const std::vector<WatermarkTemplate> tpls = checkedTemplates();
    if (tpls.empty()) return subtasks;

    // Many-to-many: every selected PDF is produced once per checked template.
    for (int r = 0; r < fileTable_->rowCount(); ++r) {
        if (scope == BatchScope::Checked && !isCheckedAt(r)) continue;
        const QString p = pathAt(r);
        if (p.isEmpty()) continue;
        const fs::path path = qstringToPath(p);
        for (const auto& tpl : tpls) {
            subtasks.push_back({path, tpl});
        }
    }
    return subtasks;
}

void MainWindow::updateUiState(bool running) {
    cancelBtn_->setEnabled(running);

    const int files = checkedCount();
    const bool hasRows = fileTable_->rowCount() > 0;
    const int tpls = checkedTemplateCount();
    const bool hasSel = selectedTemplate().isValid();

    startCheckedBtn_->setEnabled(!running && files > 0 && tpls > 0);
    startAllBtn_->setEnabled(!running && hasRows && tpls > 0);
    selectAllBtn_->setEnabled(!running && hasRows);
    selectNoneBtn_->setEnabled(!running && hasRows);

    addFilesBtn_->setEnabled(!running);
    addFolderBtn_->setEnabled(!running);
    removeSelBtn_->setEnabled(!running && hasRows);
    clearFilesBtn_->setEnabled(!running && hasRows);

    newTemplateBtn_->setEnabled(!running);
    editTemplateBtn_->setEnabled(!running && hasSel);
    deleteTemplateBtn_->setEnabled(!running && hasSel);
    previewTemplateBtn_->setEnabled(!running && hasSel);
    selectAllTemplatesBtn_->setEnabled(!running && templateList_->count() > 0);
    selectNoneTemplatesBtn_->setEnabled(!running && templateList_->count() > 0);
    templateList_->setEnabled(!running);

    outputDirEdit_->setEnabled(!running);
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