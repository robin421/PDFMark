#include <QApplication>
#include <QIcon>
#include "ui/MainWindow.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif
#include "diagnostics/CrashReporter.h"


#include <QDebug>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QMessageLogContext>

namespace {
#ifdef _WIN32
QFile* g_logFile = nullptr;

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    if (!g_logFile) {
        QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(logDir);
        QString logPath = logDir + "/pdfmark.log";
        g_logFile = new QFile(logPath);
        g_logFile->open(QIODevice::Append | QIODevice::Text);
    }
    if (g_logFile) {
        QString prefix;
        switch (type) {
            case QtDebugMsg: prefix = "DEBUG"; break;
            case QtInfoMsg: prefix = "INFO"; break;
            case QtWarningMsg: prefix = "WARN"; break;
            case QtCriticalMsg: prefix = "ERROR"; break;
            case QtFatalMsg: prefix = "FATAL"; break;
        }
        QString line = QString("[%1] %2 (%3:%4:%5)\n")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"))
            .arg(prefix).arg(context.file).arg(context.line).arg(context.function);
        line += msg.trimmed() + "\n";
        g_logFile->write(line.toUtf8());
        g_logFile->flush();
    }
}

long WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* pExc) {
    // Create crash dump in Documents folder
    QString dumpPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/PDFMark_crash.dmp";
    HANDLE hFile = CreateFileA(dumpPath.toUtf8().constData(),
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = pExc;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
            MiniDumpNormal, &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }

    DWORD code = pExc && pExc->ExceptionRecord ? pExc->ExceptionRecord->ExceptionCode : 0;
    void* addr = pExc && pExc->ExceptionRecord ? pExc->ExceptionRecord->ExceptionAddress : nullptr;
    QString exType = QString("SEH_0x%1").arg(code, 8, 16, QChar('0')).toUpper();
    QString exValue = QString("Exception at address 0x%1").arg(reinterpret_cast<quintptr>(addr), 0, 16);
    QString modName = "unknown";

    HMODULE hMod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &hMod) && hMod) {
        char modPath[MAX_PATH] = {0};
        if (GetModuleFileNameA(hMod, modPath, MAX_PATH)) {
            modName = QFileInfo(QString::fromLocal8Bit(modPath)).fileName();
        }
    }

    QString logs = pdfmark::CrashReporter::getRecentLogLines(50);
    QString extra = QString("Dump: %1\nCode: 0x%2\nAddr: 0x%3")
        .arg(dumpPath)
        .arg(code, 8, 16, QChar('0'))
        .arg(reinterpret_cast<quintptr>(addr), 0, 16);

    bool reported = pdfmark::CrashReporter::sendReportSync(exType, exValue, modName, extra, logs);
    if (!reported) {
        QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(appData);
        QFile pendingFile(appData + "/crash_pending.json");
        if (pendingFile.open(QIODevice::WriteOnly)) {
            QJsonObject obj;
            obj["type"] = exType;
            obj["value"] = exValue;
            obj["module"] = modName;
            obj["extra"] = extra;
            obj["logs"] = logs;
            pendingFile.write(QJsonDocument(obj).toJson());
            pendingFile.close();
        }
    }

    QString alertMsg = QString("PDFMark 发生未处理异常并终止运行。\n\n"
                               "错误类型: %1\n"
                               "崩溃模块: %2\n"
                               "转储文件: %3\n"
                               "上报状态: %4")
        .arg(exType, modName, dumpPath, reported ? "已自动提交至崩溃追踪系统" : "将在下次启动时自动补报");

    MessageBoxA(nullptr, alertMsg.toLocal8Bit().constData(), "PDFMark Crash", MB_ICONERROR | MB_OK);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
// Non-Windows: just keep a simple log handler if needed
void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    QByteArray localMsg = msg.toLocal8Bit();
    const char* file = context.file ? context.file : "";
    const char* function = context.function ? context.function : "";
    switch (type) {
        case QtDebugMsg: fprintf(stderr, "DEBUG: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function); break;
        case QtInfoMsg: fprintf(stderr, "INFO: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function); break;
        case QtWarningMsg: fprintf(stderr, "WARN: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function); break;
        case QtCriticalMsg: fprintf(stderr, "ERROR: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function); break;
        case QtFatalMsg: fprintf(stderr, "FATAL: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function); abort();
    }
}
#endif
} // namespace

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Install crash handler and logging for Windows
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    qInstallMessageHandler(messageHandler);
#else
    qInstallMessageHandler(messageHandler);
#endif

    try {
        QApplication app(argc, argv);

        QApplication::setWindowIcon(QIcon(":/icons/app_icon.png"));
        QApplication::setApplicationName("PDFMark");
        QApplication::setApplicationDisplayName("PDFMark");
        QApplication::setOrganizationName("PDFMark");

        // Register meta types for cross-thread signal/slot
        qRegisterMetaType<pdfmark::FileResult>("pdfmark::FileResult");
        qRegisterMetaType<std::vector<pdfmark::FileResult>>("std::vector<pdfmark::FileResult>");

        pdfmark::MainWindow window;
        // Asynchronously check and report any pending crash from previous crash
        pdfmark::CrashReporter::checkAndReportPendingCrashes();

        window.show();

        int ret = app.exec();

#ifdef _WIN32
        delete g_logFile;
#endif
        return ret;
    } catch (const std::exception& e) {
        pdfmark::CrashReporter::sendReportSync("std::exception", e.what(), "main", "Fatal exception in main()", "");
#ifdef _WIN32
        QString msg = QString("PDFMark 启动时捕获到异常：\n%1").arg(e.what());
        MessageBoxA(nullptr, msg.toUtf8().constData(), "PDFMark Error", MB_ICONERROR | MB_OK);
        delete g_logFile;
#else
        fprintf(stderr, "PDFMark startup exception: %s\n", e.what());
#endif
        return 1;
    } catch (...) {
        pdfmark::CrashReporter::sendReportSync("UnknownException", "Unknown exception caught in main()", "main", "Fatal exception in main()", "");
#ifdef _WIN32
        MessageBoxA(nullptr, "PDFMark 启动时捕获到未知异常", "PDFMark Error", MB_ICONERROR | MB_OK);
        delete g_logFile;
#else
        fprintf(stderr, "PDFMark startup unknown exception\n");
#endif
        return 1;
    }
}
