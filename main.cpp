#include <QApplication>
#include <QIcon>
#include "ui/MainWindow.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <intrin.h>  // __cpuid for CPU feature detection
#endif
#include "diagnostics/CrashReporter.h"

#include <QDebug>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QMessageLogContext>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
#ifdef _WIN32
QFile* g_logFile = nullptr;

// Returns true if the system is missing the platform update that adds
// CreateDXGIFactory2 to dxgi.dll, shows a user-friendly dialog, and exits.
// CreateDXGIFactory2 exists only on Win8.1+ or Win7 with KB2670838.
// We probe dxgi.dll directly instead of checking the OS version so this works
// with any compiler default (_WIN32_WINNT) and avoids VER_* macro dependency.
bool checkWin7DXGI() {
    HMODULE hDxgi = LoadLibraryW(L"dxgi.dll");
    if (hDxgi) {
        FARPROC sym = GetProcAddress(hDxgi, "CreateDXGIFactory2");
        FreeLibrary(hDxgi);
        if (sym) return false; // API present — nothing to do
    }

    // Missing: this is Win7 without KB2670838 (or a broken dxgi). Explain and exit.
    const wchar_t* msg =
        L"PDFMark 无法在当前系统上启动。\n\n"
        L"原因：系统缺少必需的更新组件（CreateDXGIFactory2）。\n\n"
        L"解决方法：如果您在使用 Windows 7，请安装以下补丁后重新启动，再双击 PdfMark.exe：\n\n"
        L"  KB2670838（DirectX 11 软件光栅器更新）\n"
        L"  https://www.microsoft.com/zh-cn/download/details.aspx?id=36843\n\n"
        L"  如安装后仍报错，请同时安装：\n"
        L"  KB2999226（Universal C Runtime）\n"
        L"  https://www.microsoft.com/zh-cn/download/details.aspx?id=49077\n\n"
        L"安装完成后重启电脑，再运行本程序。\n\n"
        L"技术支持：https://github.com/robin421/PDFMark/issues";
    MessageBoxW(nullptr, msg, L"PDFMark — 兼容性提示",
                MB_ICONINFORMATION | MB_OK | MB_TOPMOST);
    return true;
}

// Returns true (and shows a dialog) if the CPU lacks SSSE3, which is required
// by the pdfium prebuilt binary.  Without SSSE3, pdfium.dll will crash with
// EXCEPTION_ILLEGAL_INSTRUCTION (0xC000001D) during DLL initialization.
bool checkCpuFeatures() {
    // CPUID leaf 1, ECX bit 9 = SSSE3
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 1);
    bool hasSSSE3 = (cpuInfo[2] & (1 << 9)) != 0;
    if (hasSSSE3) return false; // OK

    const wchar_t* msg =
        L"PDFMark 无法在当前 CPU 上运行。\n\n"
        L"原因：您的 CPU 不支持 SSSE3 指令集，而 PDFMark 依赖的 PDF 渲染引擎 (pdfium) 需要该指令集。\n\n"
        L"这通常发生在以下 CPU 上：\n"
        L"  • 较老的 AMD Athlon 64 / Sempron / Phenom I 系列\n"
        L"  • 早期 Intel Atom (Bonnell 架构)\n"
        L"  • 2006 年以前的 Intel Core 处理器\n\n"
        L"解决方法：请在支持 SSSE3 的较新计算机上运行本程序。\n\n"
        L"技术支持：https://github.com/robin421/PDFMark/issues";
    MessageBoxW(nullptr, msg, L"PDFMark — CPU 兼容性检查",
                MB_ICONERROR | MB_OK | MB_TOPMOST);
    return true;
}

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
} // anonymous namespace

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Win7 pre-check: if unpatched, show dialog and exit before any DLL loads.
    // This must run before SetUnhandledExceptionFilter/qInstallMessageHandler
    // to avoid touching Qt / dxgi in an unpatched Win7 environment.
    if (checkWin7DXGI())
        return 1;

    // CPU feature pre-check: pdfium requires SSSE3.  On CPUs without it,
    // pdfium.dll crashes with EXCEPTION_ILLEGAL_INSTRUCTION (0xC000001D)
    // during DLL init.  Catch this before any pdfium code runs.
    if (checkCpuFeatures())
        return 1;

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

        qRegisterMetaType<pdfmark::FileResult>("pdfmark::FileResult");
        qRegisterMetaType<std::vector<pdfmark::FileResult>>("std::vector<pdfmark::FileResult>");

        pdfmark::MainWindow window;
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
