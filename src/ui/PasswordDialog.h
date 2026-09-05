// PDFMark - Password dialog for encrypted PDFs.
#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>

namespace pdfmark {

class PasswordDialog : public QDialog {
    Q_OBJECT
public:
    explicit PasswordDialog(const QString& fileName, QWidget* parent = nullptr);

    QString password() const;

private:
    QLineEdit* passwordEdit_ = nullptr;
};

} // namespace pdfmark