// PDFMark - Modal editor for a single watermark template.
//
// This is the ONLY place watermarks are authored: templates are global, they are
// not tied to any PDF, so creating one must not require loading a file.
#pragma once

#include "watermark/WatermarkTemplate.h"
#include <QDialog>
#include <QLineEdit>
#include <QVBoxLayout>
#include <vector>

class QSpinBox;
class QSlider;
class QDoubleSpinBox;
class QFontComboBox;
class QCheckBox;
class QLabel;
class QPushButton;

namespace pdfmark {

class TemplateEditDialog : public QDialog {
    Q_OBJECT
public:
    // `initial` may be an empty template; set `creating` to change the title.
    TemplateEditDialog(const WatermarkTemplate& initial, bool creating,
                       QWidget* parent = nullptr);

    // Valid only after the dialog was accepted.
    const WatermarkTemplate& result() const { return result_; }

private:
    void buildUi(bool creating);
    void addTextRow(const QString& text, int insertAt = -1);
    void removeTextRow(QWidget* row);
    std::vector<WatermarkConfig> linesFromUi() const;
    WatermarkConfig styleFromUi() const;
    void refreshPreview();
    void applyStyleToUi(const WatermarkConfig& style);
    void accept() override;

    QString autoName() const;

    QLineEdit* nameEdit_ = nullptr;
    QWidget* rowsContainer_ = nullptr;
    QVBoxLayout* rowsLayout_ = nullptr;
    QSpinBox* depthSpin_ = nullptr;
    QSlider* depthSlider_ = nullptr;
    QLabel* depthHint_ = nullptr;
    QDoubleSpinBox* rotationSpin_ = nullptr;
    QFontComboBox* fontCombo_ = nullptr;
    QCheckBox* boldCheck_ = nullptr;
    QCheckBox* italicCheck_ = nullptr;
    QLabel* previewLabel_ = nullptr;

    WatermarkTemplate result_;
    WatermarkConfig style0_;   // style of the first line, used as the shared style
};

} // namespace pdfmark
