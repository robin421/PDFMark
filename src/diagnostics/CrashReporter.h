// PDFMark - Crash reporting via GlitchTip (Sentry-compatible API).
#pragma once

#include "common/Common.h"
#include <QString>
#include <QJsonObject>
#include <functional>

namespace pdfmark {

class CrashReporter {
public:
    // GlitchTip project configuration
    static constexpr const char* DSN = "https://e4331f68798f4d46bffd38ff0b75c8c6@app.glitchtip.com/27585";
    static constexpr const char* STORE_URL = "https://app.glitchtip.com/api/27585/store/";
    static constexpr const char* PROJECT_KEY = "e4331f68798f4d46bffd38ff0b75c8c6";
    static constexpr const char* PROJECT_ID = "27585";

    // Initialize global exception handlers (Windows SEH + std::terminate)
    static void init();

    // Check for any unhandled crash from previous run (e.g. pending dump or crash marker)
    // and asynchronously upload it to GlitchTip in the background.
    static void checkAndReportPendingCrashes();

    // Send a crash event synchronously (used inside Windows unhandled exception filter).
    // Uses native WinHTTP on Windows to avoid relying on Qt event loop when process is terminating.
    static bool sendReportSync(const QString& exceptionType,
                               const QString& exceptionValue,
                               const QString& moduleName,
                               const QString& extraInfo,
                               const QString& logTail);

    // Send a crash event asynchronously via Qt Network (used for startup retries or non-fatal errors).
    static void sendReportAsync(const QString& exceptionType,
                                const QString& exceptionValue,
                                const QString& moduleName,
                                const QString& extraInfo,
                                const QString& logTail,
                                std::function<void(bool success)> callback = nullptr);

    // Send a test report to verify connectivity to GlitchTip
    static void sendTestReport(std::function<void(bool success)> callback = nullptr);

    // Helper: read last N lines of pdfmark.log
    static QString getRecentLogLines(int maxLines = 100);

    // Helper: build standard Sentry/GlitchTip event JSON payload
    static QByteArray buildEventJson(const QString& eventId,
                                     const QString& exceptionType,
                                     const QString& exceptionValue,
                                     const QString& moduleName,
                                     const QString& extraInfo,
                                     const QString& logTail);
};

} // namespace pdfmark
