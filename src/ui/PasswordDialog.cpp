// PDFMark - PasswordDialog implementation.
#include "ui/PasswordDialog.h"
#include "ui/Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace pdfmark {

PasswordDialog::PasswordDialog(const QString& fileName, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("输入 PDF 密码");
    setModal(true);
    setMinimumWidth(400);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);

    auto* tip = new QLabel(
        QString("<span style='color:%1'>文件</span> <b>%2</b> "
                "<span style='color:%1'>已加密，请输入打开密码：</span>")
            .arg(QLatin1String(theme::kTextSecondary), fileName.toHtmlEscaped()), this
    );
    tip->setWordWrap(true);
    layout->addWidget(tip);

    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText("请输入文档密码...");
    layout->addWidget(passwordEdit_);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton("跳过", this);
    cancelBtn->setObjectName("ghost");
    auto* okBtn = new QPushButton("确认", this);
    okBtn->setObjectName("primary");
    okBtn->setDefault(true);

    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);

    layout->addLayout(btnLayout);
}

QString PasswordDialog::password() const {
    return passwordEdit_ ? passwordEdit_->text() : QString();
}

} // namespace pdfmark