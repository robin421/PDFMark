#include <QApplication>
#include <QIcon>
#include "ui/MainWindow.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif

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
        MessageBoxA(nullptr,
            ("PDFMark encountered a crash. Minidump written to:\n" + dumpPath).toUtf8().constData(),
            "PDFMark Crash", MB_ICONERROR | MB_OK);
    }
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

        pdfmark::MainWindow window;
        window.show();

        int ret = app.exec();

#ifdef _WIN32
        delete g_logFile;
#endif
        return ret;
    } catch (const std::exception& e) {
#ifdef _WIN32
        QString msg = QString("PDFMark 启动时捕获到异常：\n%1").arg(e.what());
        MessageBoxA(nullptr, msg.toUtf8().constData(), "PDFMark Error", MB_ICONERROR | MB_OK);
        delete g_logFile;
#else
        fprintf(stderr, "PDFMark startup exception: %s\n", e.what());
#endif
        return 1;
    } catch (...) {
#ifdef _WIN32
        MessageBoxA(nullptr, "PDFMark 启动时捕获到未知异常", "PDFMark Error", MB_ICONERROR | MB_OK);
        delete g_logFile;
#else
        fprintf(stderr, "PDFMark startup unknown exception\n");
#endif
        return 1;
    }
}

#ifdef _WIN32
// WinMain entry point for Windows GUI application (no console)
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    Q_UNUSED(hInstance); Q_UNUSED(hPrevInstance); Q_UNUSED(lpCmdLine); Q_UNUSED(nCmdShow);
    return main(qApp ? qApp->argc() : 0, qApp ? qApp->argv() : nullptr);
}
#endif