// PDFMark - Crash reporting implementation for GlitchTip.
#include "diagnostics/CrashReporter.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUuid>
#include <QDebug>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <dbghelp.h>
#endif

namespace pdfmark {

namespace {

QString pendingCrashFilePath() {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + "/crash_pending.json";
}

} // namespace

void CrashReporter::init() {
    // Platform-specific hook initialization can be performed here or in main.cpp
}

QString CrashReporter::getRecentLogLines(int maxLines) {
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/pdfmark.log";
    QFile file(logPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QStringList lines;
    while (!file.atEnd()) {
        lines.append(QString::fromUtf8(file.readLine().trimmed()));
        if (lines.size() > maxLines * 2) {
            lines.erase(lines.begin(), lines.begin() + maxLines);
        }
    }
    file.close();

    if (lines.size() > maxLines) {
        lines = lines.mid(lines.size() - maxLines);
    }
    return lines.join("\n");
}

QByteArray CrashReporter::buildEventJson(const QString& eventId,
                                         const QString& exceptionType,
                                         const QString& exceptionValue,
                                         const QString& moduleName,
                                         const QString& extraInfo,
                                         const QString& logTail) {
    QJsonObject root;
    root["event_id"] = eventId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces).toLower()
        : eventId;
    root["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root["platform"] = "native";
    root["level"] = "fatal";
    root["logger"] = "pdfmark.crash";
    root["release"] = PDFMARK_VERSION;
    root["message"] = QString("Crash in %1: %2 - %3").arg(moduleName, exceptionType, exceptionValue);

    // Tags
    QJsonObject tags;
    tags["os"] = QSysInfo::prettyProductName();
    tags["arch"] = QSysInfo::currentCpuArchitecture();
    tags["kernel"] = QSysInfo::kernelVersion();
    tags["version"] = PDFMARK_VERSION;
    root["tags"] = tags;

    // Exception
    QJsonObject exceptionVal;
    exceptionVal["type"] = exceptionType.isEmpty() ? "UnknownException" : exceptionType;
    exceptionVal["value"] = exceptionValue.isEmpty() ? "No details provided" : exceptionValue;
    if (!moduleName.isEmpty()) {
        exceptionVal["module"] = moduleName;
    }

    QJsonArray valArray;
    valArray.append(exceptionVal);
    QJsonObject excObj;
    excObj["values"] = valArray;
    root["exception"] = excObj;

    // Extra
    QJsonObject extra;
    if (!extraInfo.isEmpty()) {
        extra["details"] = extraInfo;
    }
    if (!logTail.isEmpty()) {
        extra["recent_logs"] = logTail;
    }
    root["extra"] = extra;

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool CrashReporter::sendReportSync(const QString& exceptionType,
                                   const QString& exceptionValue,
                                   const QString& moduleName,
                                   const QString& extraInfo,
                                   const QString& logTail) {
#ifdef _WIN32
    QByteArray payload = buildEventJson(QString(), exceptionType, exceptionValue, moduleName, extraInfo, logTail);

    HINTERNET hSession = WinHttpOpen(L"PDFMark-CrashReporter/1.1",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // Timeout: 3 seconds for connect, send, receive
    WinHttpSetTimeouts(hSession, 3000, 3000, 3000, 3000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"app.glitchtip.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring path = L"/api/27585/store/";
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring headers =
        L"Content-Type: application/json\r\n"
        L"X-Sentry-Auth: Sentry sentry_version=7, sentry_client=pdfmark/1.1.0, sentry_key=e4331f68798f4d46bffd38ff0b75c8c6\r\n";

    BOOL sent = WinHttpSendRequest(hRequest,
                                  headers.c_str(), static_cast<DWORD>(headers.length()),
                                  (LPVOID)payload.constData(), static_cast<DWORD>(payload.size()),
                                  static_cast<DWORD>(payload.size()), 0);

    bool ok = false;
    if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX)) {
            ok = (statusCode >= 200 && statusCode < 300);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
#else
    Q_UNUSED(exceptionType);
    Q_UNUSED(exceptionValue);
    Q_UNUSED(moduleName);
    Q_UNUSED(extraInfo);
    Q_UNUSED(logTail);
    return false;
#endif
}

void CrashReporter::sendReportAsync(const QString& exceptionType,
                                    const QString& exceptionValue,
                                    const QString& moduleName,
                                    const QString& extraInfo,
                                    const QString& logTail,
                                    std::function<void(bool success)> callback) {
    QByteArray payload = buildEventJson(QString(), exceptionType, exceptionValue, moduleName, extraInfo, logTail);

    auto* manager = new QNetworkAccessManager();
    QUrl url(STORE_URL);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("X-Sentry-Auth",
        "Sentry sentry_version=7, sentry_client=pdfmark/1.1.0, sentry_key=e4331f68798f4d46bffd38ff0b75c8c6");

    QNetworkReply* reply = manager->post(request, payload);
    QObject::connect(reply, &QNetworkReply::finished, [reply, manager, callback]() {
        bool ok = (reply->error() == QNetworkReply::NoError);
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode >= 200 && statusCode < 300) {
            ok = true;
        }
        if (callback) {
            callback(ok);
        }
        reply->deleteLater();
        manager->deleteLater();
    });
}

void CrashReporter::sendTestReport(std::function<void(bool success)> callback) {
    sendReportAsync("TestException",
                    "Manual test crash event triggered by user/tester",
                    "pdfmark_core",
                    "Diagnostics test verification",
                    getRecentLogLines(20),
                    callback);
}

void CrashReporter::checkAndReportPendingCrashes() {
    QString pendingPath = pendingCrashFilePath();
    if (!QFile::exists(pendingPath)) {
        return;
    }

    QFile file(pendingPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        QFile::remove(pendingPath);
        return;
    }

    QJsonObject obj = doc.object();
    QString exType = obj.value("type").toString("PendingCrash");
    QString exValue = obj.value("value").toString("Previous session crashed unexpectedly");
    QString modName = obj.value("module").toString("PdfMark.exe");
    QString extra = obj.value("extra").toString();
    QString logTail = obj.value("logs").toString();

    sendReportAsync(exType, exValue, modName, extra, logTail, [pendingPath](bool success) {
        if (success) {
            QFile::remove(pendingPath);
            qDebug() << "Pending crash report uploaded successfully and cleared.";
        } else {
            qWarning() << "Failed to upload pending crash report, will retry next session.";
        }
    });
}

} // namespace pdfmark
