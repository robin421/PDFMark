// PDFMark - Modal editor for a single watermark template.
#include "ui/TemplateEditDialog.h"
#include "watermark/WatermarkRenderer.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <algorithm>

namespace pdfmark {

namespace {

QString depthHintText(int percent) {
    if (percent <= 8) return QStringLiteral("极浅");
    if (percent <= 12) return QStringLiteral("偏浅");
    if (percent <= 25) return QStringLiteral("适中");
    if (percent <= 40) return QStringLiteral("偏深");
    return QStringLiteral("深色");
}

} // namespace

TemplateEditDialog::TemplateEditDialog(const WatermarkTemplate& initial, bool creating,
                                       QWidget* parent)
    : QDialog(parent), result_(initial) {
    style0_ = initial.watermarks.empty() ? WatermarkConfig{} : initial.watermarks.front();
    buildUi(creating);
    applyStyleToUi(style0_);

    if (!initial.watermarks.empty()) {
        for (const auto& wm : initial.watermarks) {
            addTextRow(QString::fromUtf8(wm.text.c_str()));
        }
    } else {
        addTextRow(QString());
    }
    if (!initial.name.empty()) {
        nameEdit_->setText(QString::fromUtf8(initial.name.c_str()));
    }
    refreshPreview();
}

void TemplateEditDialog::buildUi(bool creating) {
    setWindowTitle(creating ? QStringLiteral("新建水印模板") : QStringLiteral("编辑水印模板"));
    setMinimumSize(760, 620);
    setSizeGripEnabled(true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* body = new QHBoxLayout();
    body->setSpacing(12);

    // ── Left: name + text lines + style ─────────────────────────────────
    auto* left = new QVBoxLayout();
    left->setSpacing(8);

    auto* nameRow = new QHBoxLayout();
    nameRow->setSpacing(8);
    nameRow->addWidget(new QLabel(QStringLiteral("模板名称:"), this));
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText(QStringLiteral("留空则自动使用水印文字（超出 6 字截断）"));
    nameEdit_->setToolTip(QStringLiteral("模板名会作为输出子目录名（例如用公司名）"));
    nameRow->addWidget(nameEdit_, 1);
    left->addLayout(nameRow);

    auto* textGroup = new QGroupBox(QStringLiteral("水印文字（同一模板内可多行，共用一套样式）"), this);
    auto* textLayout = new QVBoxLayout(textGroup);
    textLayout->setContentsMargins(10, 12, 10, 10);
    textLayout->setSpacing(6);

    auto* scroll = new QScrollArea(textGroup);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(120);
    rowsContainer_ = new QWidget(scroll);
    rowsLayout_ = new QVBoxLayout(rowsContainer_);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(4);
    rowsLayout_->addStretch();
    scroll->setWidget(rowsContainer_);
    textLayout->addWidget(scroll, 1);

    auto* addRow = new QPushButton(QStringLiteral("+ 添加一行"), textGroup);
    addRow->setStyleSheet("padding:5px 12px;");
    connect(addRow, &QPushButton::clicked, this, [this]() { addTextRow(QString()); });
    textLayout->addWidget(addRow, 0, Qt::AlignLeft);
    left->addWidget(textGroup, 1);

    auto* styleGroup = new QGroupBox(QStringLiteral("水印样式"), this);
    auto* grid = new QGridLayout(styleGroup);
    grid->setContentsMargins(10, 12, 10, 10);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    int r = 0;
    grid->addWidget(new QLabel(QStringLiteral("颜色深浅:"), styleGroup), r, 0);
    auto* depthBox = new QWidget(styleGroup);
    auto* depthLayout = new QHBoxLayout(depthBox);
    depthLayout->setContentsMargins(0, 0, 0, 0);
    depthLayout->setSpacing(8);
    depthSlider_ = new QSlider(Qt::Horizontal, depthBox);
    depthSlider_->setRange(5, 60);
    depthSpin_ = new QSpinBox(depthBox);
    depthSpin_->setRange(5, 60);
    depthSpin_->setSuffix(" %");
    depthHint_ = new QLabel(depthBox);
    depthHint_->setStyleSheet("color:#777777; font-size:11px;");
    depthHint_->setFixedWidth(30);
    depthLayout->addWidget(depthSlider_, 1);
    depthLayout->addWidget(depthSpin_);
    depthLayout->addWidget(depthHint_);
    grid->addWidget(depthBox, r, 1);
    r++;

    grid->addWidget(new QLabel(QStringLiteral("倾斜角度:"), styleGroup), r, 0);
    rotationSpin_ = new QDoubleSpinBox(styleGroup);
    rotationSpin_->setRange(-90.0, 90.0);
    rotationSpin_->setSingleStep(5.0);
    rotationSpin_->setSuffix("°");
    grid->addWidget(rotationSpin_, r, 1);
    r++;

    grid->addWidget(new QLabel(QStringLiteral("水印字体:"), styleGroup), r, 0);
    fontCombo_ = new QFontComboBox(styleGroup);
    grid->addWidget(fontCombo_, r, 1);
    r++;

    grid->addWidget(new QLabel(QStringLiteral("字体样式:"), styleGroup), r, 0);
    auto* styleBox = new QWidget(styleGroup);
    auto* styleBoxLayout = new QHBoxLayout(styleBox);
    styleBoxLayout->setContentsMargins(0, 0, 0, 0);
    styleBoxLayout->setSpacing(12);
    boldCheck_ = new QCheckBox(QStringLiteral("加粗"), styleBox);
    italicCheck_ = new QCheckBox(QStringLiteral("斜体"), styleBox);
    styleBoxLayout->addWidget(boldCheck_);
    styleBoxLayout->addWidget(italicCheck_);
    styleBoxLayout->addStretch();
    grid->addWidget(styleBox, r, 1);
    r++;

    left->addWidget(styleGroup);
    body->addLayout(left, 3);

    // ── Right: live preview ─────────────────────────────────────────────
    auto* previewGroup = new QGroupBox(QStringLiteral("实时预览"), this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    previewLayout->setContentsMargins(10, 12, 10, 10);
    previewLabel_ = new QLabel(previewGroup);
    previewLabel_->setAlignment(Qt::AlignCenter);
    previewLabel_->setStyleSheet("background:white; border:1px solid #dde3ea;");
    previewLabel_->setMinimumWidth(300);
    previewLayout->addWidget(previewLabel_, 1);
    body->addWidget(previewGroup, 2);

    root->addLayout(body, 1);

    // ── Buttons ─────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    cancelBtn->setStyleSheet("padding:6px 20px;");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    auto* okBtn = new QPushButton(creating ? QStringLiteral("创建模板") : QStringLiteral("保存"),
                                  this);
    okBtn->setDefault(true);
    okBtn->setStyleSheet(
        "QPushButton{background:#0078d4;color:white;font-weight:bold;padding:6px 22px;"
        "border-radius:4px;} QPushButton:hover{background:#106ebe;}");
    connect(okBtn, &QPushButton::clicked, this, &TemplateEditDialog::accept);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    root->addLayout(btnRow);

    // ── Live preview wiring ─────────────────────────────────────────────
    connect(depthSlider_, &QSlider::valueChanged, this, [this](int v) {
        if (depthSpin_->value() != v) {
            QSignalBlocker block(depthSpin_);
            depthSpin_->setValue(v);
        }
        depthHint_->setText(depthHintText(v));
        refreshPreview();
    });
    connect(depthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (depthSlider_->value() != v) depthSlider_->setValue(v);
    });
    connect(rotationSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { refreshPreview(); });
    connect(fontCombo_, &QFontComboBox::currentFontChanged, this, [this](const QFont&) {
        refreshPreview();
    });
    connect(boldCheck_, &QCheckBox::toggled, this, [this](bool) { refreshPreview(); });
    connect(italicCheck_, &QCheckBox::toggled, this, [this](bool) { refreshPreview(); });
}

void TemplateEditDialog::addTextRow(const QString& text, int insertAt) {
    auto* row = new QWidget(rowsContainer_);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);

    auto* edit = new QLineEdit(row);
    edit->setText(text);
    edit->setPlaceholderText(QStringLiteral("水印文字（例如：甲公司 机密）"));
    h->addWidget(edit, 1);

    auto* remove = new QPushButton("×", row);
    remove->setFixedSize(26, 26);
    remove->setStyleSheet("color:#cc0000; font-weight:bold; font-size:15px;");
    remove->setToolTip(QStringLiteral("删除这一行"));
    h->addWidget(remove);

    connect(edit, &QLineEdit::textChanged, this, [this](const QString&) { refreshPreview(); });
    connect(remove, &QPushButton::clicked, this, [this, row]() { removeTextRow(row); });

    const int count = rowsLayout_->count();
    const int index = (insertAt >= 0 && insertAt <= count) ? insertAt : count - 1;
    rowsLayout_->insertWidget(std::max(0, index), row);
}

void TemplateEditDialog::removeTextRow(QWidget* row) {
    row->deleteLater();
    refreshPreview();
}

std::vector<WatermarkConfig> TemplateEditDialog::linesFromUi() const {
    std::vector<WatermarkConfig> out;
    const WatermarkConfig style = styleFromUi();
    for (int i = 0; i < rowsLayout_->count(); ++i) {
        auto* item = rowsLayout_->itemAt(i);
        if (!item) continue;
        auto* row = qobject_cast<QWidget*>(item->widget());
        if (!row) continue;
        auto* edit = row->findChild<QLineEdit*>();
        if (!edit) continue;
        const QString t = edit->text().trimmed();
        if (t.isEmpty()) continue;
        WatermarkConfig cfg = style;
        cfg.text = t.toStdString();
        out.push_back(cfg);
    }
    return out;
}

WatermarkConfig TemplateEditDialog::styleFromUi() const {
    WatermarkConfig cfg = style0_;
    cfg.text.clear();
    cfg.fontSizePt = 24;
    cfg.dpi = 200;
    cfg.jpegQuality = 85;
    cfg.colorHex = "#808080";
    cfg.rotationDegrees = rotationSpin_->value();
    cfg.fontFamily = fontCombo_->currentFont().family().toStdString();
    cfg.fontBold = boldCheck_->isChecked();
    cfg.fontItalic = italicCheck_->isChecked();
    cfg.opacity = depthSlider_->value() / 100.0;
    return cfg;
}

void TemplateEditDialog::applyStyleToUi(const WatermarkConfig& style) {
    rotationSpin_->setValue(style.rotationDegrees);
    fontCombo_->setCurrentFont(QFont(style.fontFamily.empty()
        ? QStringLiteral("Arial") : QString::fromUtf8(style.fontFamily.c_str())));
    boldCheck_->setChecked(style.fontBold);
    italicCheck_->setChecked(style.fontItalic);

    const int percent = static_cast<int>(style.opacity * 100.0 + 0.5);
    {
        QSignalBlocker blockSlider(depthSlider_);
        QSignalBlocker blockSpin(depthSpin_);
        depthSlider_->setValue(percent);
        depthSpin_->setValue(percent);
    }
    depthHint_->setText(depthHintText(percent));
}

void TemplateEditDialog::refreshPreview() {
    if (!previewLabel_) return;
    const std::vector<WatermarkConfig> lines = linesFromUi();
    if (lines.empty()) {
        previewLabel_->setPixmap(QPixmap());
        previewLabel_->setText(QStringLiteral("（请先填写水印文字）"));
        return;
    }
    const QImage img = WatermarkRenderer::renderPreview(320, 452, lines);
    previewLabel_->setText(QString());
    previewLabel_->setPixmap(QPixmap::fromImage(img));
}

QString TemplateEditDialog::autoName() const {
    const std::vector<WatermarkConfig> lines = linesFromUi();
    const std::string n = templateNameFromConfigs(lines);
    return QString::fromUtf8(n.c_str());
}

void TemplateEditDialog::accept() {
    WatermarkTemplate tpl;
    tpl.name = nameEdit_->text().trimmed().toUtf8().toStdString();
    tpl.watermarks = linesFromUi();

    if (!normalizeTemplate(tpl)) {
        QMessageBox::warning(this, QStringLiteral("提示"),
            QStringLiteral("请至少填写一行水印文字。"));
        return;
    }
    result_ = tpl;
    QDialog::accept();
}

} // namespace pdfmark
