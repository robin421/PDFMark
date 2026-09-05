// PDFMark - PasswordDialog implementation.
#include "ui/PasswordDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace pdfmark {

PasswordDialog::PasswordDialog(const QString& fileName, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("输入 PDF 密码");
    setModal(true);
    setFixedSize(380, 160);

    auto* layout = new QVBoxLayout(this);

    auto* tip = new QLabel(
        QString("文件 <b>%1</b> 已加密，请输入打开密码：").arg(fileName), this
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
    auto* okBtn = new QPushButton("确认", this);
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