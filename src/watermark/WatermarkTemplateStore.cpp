// PDFMark - JSON persistence for reusable watermark templates.
#include "watermark/WatermarkTemplateStore.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace pdfmark {

namespace {

constexpr int kSchemaVersion = 1;

QJsonObject configToJson(const WatermarkConfig& cfg) {
    QJsonObject o;
    o["text"] = QString::fromUtf8(cfg.text.c_str());
    o["fontSizePt"] = cfg.fontSizePt;
    o["fontFamily"] = QString::fromUtf8(cfg.fontFamily.c_str());
    o["fontBold"] = cfg.fontBold;
    o["fontItalic"] = cfg.fontItalic;
    o["opacity"] = cfg.opacity;
    o["colorHex"] = QString::fromUtf8(cfg.colorHex.c_str());
    o["rotationDegrees"] = cfg.rotationDegrees;
    o["dpi"] = cfg.dpi;
    o["jpegQuality"] = cfg.jpegQuality;
    o["selected"] = cfg.selected;
    return o;
}

WatermarkConfig configFromJson(const QJsonObject& o) {
    WatermarkConfig cfg;
    cfg.text = o.value("text").toString().toUtf8().toStdString();
    if (o.contains("fontSizePt")) cfg.fontSizePt = o.value("fontSizePt").toInt(cfg.fontSizePt);
    if (o.contains("fontFamily")) cfg.fontFamily = o.value("fontFamily").toString().toUtf8().toStdString();
    if (o.contains("fontBold")) cfg.fontBold = o.value("fontBold").toBool(cfg.fontBold);
    if (o.contains("fontItalic")) cfg.fontItalic = o.value("fontItalic").toBool(cfg.fontItalic);
    if (o.contains("opacity")) cfg.opacity = o.value("opacity").toDouble(cfg.opacity);
    if (o.contains("colorHex")) cfg.colorHex = o.value("colorHex").toString().toUtf8().toStdString();
    if (o.contains("rotationDegrees")) cfg.rotationDegrees = o.value("rotationDegrees").toDouble(cfg.rotationDegrees);
    if (o.contains("dpi")) cfg.dpi = o.value("dpi").toInt(cfg.dpi);
    if (o.contains("jpegQuality")) cfg.jpegQuality = o.value("jpegQuality").toInt(cfg.jpegQuality);
    if (o.contains("selected")) cfg.selected = o.value("selected").toBool(cfg.selected);
    return cfg;
}

} // namespace

WatermarkTemplateStore::WatermarkTemplateStore(QString filePath)
    : filePath_(filePath.isEmpty() ? defaultFilePath() : std::move(filePath)) {}

QString WatermarkTemplateStore::defaultFilePath() {
    // QStandardPaths only appends the application sub-directory when
    // QCoreApplication::applicationName() is set. Without it every Qt app would
    // share one file, so fall back to an explicit PDFMark directory.
    QString dir;
    if (!QCoreApplication::applicationName().isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    if (dir.isEmpty()) {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        dir = base.isEmpty() ? (QDir::homePath() + "/.pdfmark") : (base + "/PDFMark");
    }
    return dir + "/watermark_templates.json";
}

bool WatermarkTemplateStore::load() {
    templates_.clear();

    QFile file(filePath_);
    if (!file.exists()) {
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "WatermarkTemplateStore: cannot open" << filePath_ << file.errorString();
        return false;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "WatermarkTemplateStore: corrupt JSON in" << filePath_ << err.errorString();
        return false;
    }

    const QJsonArray arr = doc.object().value("templates").toArray();
    templates_.reserve(static_cast<size_t>(arr.size()));
    for (const auto& item : arr) {
        const QJsonObject obj = item.toObject();
        WatermarkTemplate tpl;
        tpl.name = obj.value("name").toString().toUtf8().toStdString();
        const QJsonArray wms = obj.value("watermarks").toArray();
        tpl.watermarks.reserve(static_cast<size_t>(wms.size()));
        for (const auto& w : wms) {
            tpl.watermarks.push_back(configFromJson(w.toObject()));
        }
        if (!tpl.name.empty()) {
            templates_.push_back(std::move(tpl));
        }
    }
    return true;
}

bool WatermarkTemplateStore::save() const {
    const QFileInfo info(filePath_);
    if (!info.absoluteDir().exists()) {
        QDir().mkpath(info.absolutePath());
    }

    QJsonArray arr;
    for (const auto& tpl : templates_) {
        QJsonObject obj;
        obj["name"] = QString::fromUtf8(tpl.name.c_str());
        QJsonArray wms;
        for (const auto& cfg : tpl.watermarks) {
            wms.append(configToJson(cfg));
        }
        obj["watermarks"] = wms;
        arr.append(obj);
    }

    QJsonObject root;
    root["version"] = kSchemaVersion;
    root["templates"] = arr;

    QFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "WatermarkTemplateStore: cannot write" << filePath_ << file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool WatermarkTemplateStore::addOrReplace(const WatermarkTemplate& tpl) {
    if (tpl.name.empty()) return false;
    const QString name = QString::fromUtf8(tpl.name.c_str());
    for (auto& existing : templates_) {
        if (QString::fromUtf8(existing.name.c_str()) == name) {
            existing = tpl;
            return true;
        }
    }
    templates_.push_back(tpl);
    return false;
}

bool WatermarkTemplateStore::remove(const QString& name) {
    for (auto it = templates_.begin(); it != templates_.end(); ++it) {
        if (QString::fromUtf8(it->name.c_str()) == name) {
            templates_.erase(it);
            return true;
        }
    }
    return false;
}

bool WatermarkTemplateStore::rename(const QString& oldName, const QString& newName) {
    if (newName.isEmpty()) return false;
    if (find(newName) != nullptr) return false;

    WatermarkTemplate* target = nullptr;
    for (auto& tpl : templates_) {
        if (QString::fromUtf8(tpl.name.c_str()) == oldName) {
            target = &tpl;
            break;
        }
    }
    if (!target) return false;
    target->name = newName.toUtf8().toStdString();
    return true;
}

const WatermarkTemplate* WatermarkTemplateStore::find(const QString& name) const {
    for (const auto& tpl : templates_) {
        if (QString::fromUtf8(tpl.name.c_str()) == name) {
            return &tpl;
        }
    }
    return nullptr;
}

} // namespace pdfmark
