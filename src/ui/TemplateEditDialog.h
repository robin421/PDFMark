// PDFMark - Modal editor for a single watermark template.
//
// This is the ONLY place watermarks are authored: templates are global, they are
// not tied to any PDF, so creating one must not require loading a file.
#pragma once

#include "watermark/WatermarkTemplate.h"
#include <QDialog>
#include <QLineEdit>
#include <QStringList>
#include <QVBoxLayout>
#include <vector>

class QSpinBox;
class QSlider;
class QDoubleSpinBox;
class QFontComboBox;
class QCheckBox;
class QComboBox;
class QScrollArea;
class QLabel;
class QPushButton;
class QTimer;

namespace pdfmark {

class WatermarkTemplateStore;

class TemplateEditDialog : public QDialog {
    Q_OBJECT
public:
    // `initial` may be an empty template; set `creating` to change the title.
    //
    // `takenNames` are the names already used by OTHER templates: a collision
    // makes this dialog save a sibling ("机密 (2)") instead of overwriting, and
    // when the user left 输出文件夹 empty it also pins the output folder to the
    // original name so all styles of one watermark text share one folder.
    // `knownFolders` are the folders that already exist, offered as a picker.
    //
    // `store`, when non-null, provides the two GLOBAL style presets (样式1 /
    // 样式2) and receives every style edit immediately ("auto save"). With a
    // null store the built-in presets are used and edits stay in memory, which
    // keeps the dialog testable without touching the user's real file.
    TemplateEditDialog(const WatermarkTemplate& initial, bool creating,
                       const QStringList& takenNames = QStringList(),
                       const QStringList& knownFolders = QStringList(),
                       WatermarkTemplateStore* store = nullptr,
                       QWidget* parent = nullptr);

    // Valid only after the dialog was accepted.
    const WatermarkTemplate& result() const { return result_; }

    // Write a style edit that is still inside the debounce window. Called on
    // accept/reject and by tests so nothing is lost and no timing is relied on.
    void flushPendingStyleSave();

private:
    void buildUi(bool creating);
    void addTextRow(const QString& text, int insertAt = -1);
    void removeTextRow(QWidget* row);
    std::vector<WatermarkConfig> linesFromUi() const;
    WatermarkConfig styleFromUi() const;
    void refreshPreview();
    void refreshOutputHint();
    // Grow/shrink the text list with its content (keeps a one-line template from
    // showing an empty 120px field).
    void updateTextAreaHeight();
    void applyStyleToUi(const WatermarkConfig& style);
    void accept() override;
    void reject() override;

    // ── Style presets ───────────────────────────────────────────────────
    WatermarkConfig presetAt(int index) const;
    // Dropdown changed: load that preset into the controls WITHOUT treating the
    // load itself as an edit (which would immediately re-save it).
    void onStylePresetChanged(int index);
    // Any style widget changed: the selected preset is re-saved, debounced.
    void onStyleEdited();
    void scheduleStyleSave();
    void persistSelectedStyle();

    QString autoName() const;
    // Naming decision shared by the live hint and accept().
    TemplateNaming resolveNaming() const;
    // Final template name: the typed name (or the auto name), made unique against
    // takenNames_ by appending " (2)", " (3)", ...
    QString resolvedName() const;
    // Value for WatermarkTemplate::outputFolder: the explicit pick/typing, or the
    // original (pre-deduplication) name, or empty == "use the template name".
    std::string resolvedFolder() const;

    QLineEdit* variantEdit_ = nullptr;
    QComboBox* folderCombo_ = nullptr;
    QLabel* outputHint_ = nullptr;
    QScrollArea* rowsScroll_ = nullptr;
    QWidget* rowsContainer_ = nullptr;
    QVBoxLayout* rowsLayout_ = nullptr;
    QComboBox* styleCombo_ = nullptr;
    QSpinBox* depthSpin_ = nullptr;
    QSlider* depthSlider_ = nullptr;
    QLabel* depthHint_ = nullptr;
    QDoubleSpinBox* rotationSpin_ = nullptr;
    QFontComboBox* fontCombo_ = nullptr;
    QCheckBox* boldCheck_ = nullptr;
    QCheckBox* italicCheck_ = nullptr;
    QLabel* previewLabel_ = nullptr;

    WatermarkTemplate result_;   // declared before the naming inputs (init order)
    QStringList takenNames_;
    QStringList knownFolders_;
    WatermarkTemplateStore* store_ = nullptr;
    QTimer* styleSaveTimer_ = nullptr;
    int styleIndex_ = 0;             // selected global preset
    WatermarkConfig currentStyle_;   // style currently shown in the widgets
};

} // namespace pdfmark
