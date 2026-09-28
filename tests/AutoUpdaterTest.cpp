// PDFMark - Unit tests for AutoUpdater version comparison logic.
#include <QtTest>

#include "updater/AutoUpdater.h"

class AutoUpdaterTest : public QObject {
    Q_OBJECT
private slots:
    void testParseVersionNumbers() {
        auto check = [](const char* input, const QVector<int>& expected) {
            auto result = pdfmark::AutoUpdater::parseVersionNumbers(QString::fromLatin1(input));
            if (result != expected) {
                qWarning() << "parseVersionNumbers(" << input << "): got" << result
                           << "expected" << expected;
            }
            QCOMPARE(result, expected);
        };

        check("1.0.9", {1, 0, 9});
        check("v1.0.9", {1, 0, 9});
        check("V2.1.0", {2, 1, 0});
        check("1.2", {1, 2, 0});
        check("10", {10, 0, 0});
        check("1.0.0-beta", {1, 0, 0});
        check("v1.0.0-rc1", {1, 0, 0});
        check("1", {1, 0, 0});
    }

    void testCompareVersions() {
        auto cmp = [](const char* v1, const char* v2) {
            return pdfmark::AutoUpdater::compareVersions(
                QString::fromLatin1(v1), QString::fromLatin1(v2));
        };

        // Equal
        QVERIFY(cmp("v1.0.9", "v1.0.9") == 0);
        QVERIFY(cmp("1.2.3", "v1.2.3") == 0);

        // Greater
        QVERIFY(cmp("v1.1.0", "v1.0.9") > 0);
        QVERIFY(cmp("v2.0.0", "v1.9.9") > 0);
        QVERIFY(cmp("v1.0.10", "v1.0.9") > 0);
        QVERIFY(cmp("v1.1.0", "v1.0.0") > 0);
        QVERIFY(cmp("v0.9.9", "v0.9.8") > 0);

        // Less
        QVERIFY(cmp("v1.0.9", "v1.1.0") < 0);
        QVERIFY(cmp("v1.9.9", "v2.0.0") < 0);
        QVERIFY(cmp("v0.9.8", "v0.9.9") < 0);
    }

    void testUpdateFlowData() {
        // Verify UpdateInfo struct can be populated without crash
        pdfmark::UpdateInfo info;
        info.versionTag = "v1.1.0";
        info.versionName = "PDFMark v1.1.0";
        info.downloadUrl = "https://github.com/robin421/PDFMark/releases/download/v1.1.0/PDFMark-Windows-x64.zip";
        info.assetName = "PDFMark-Windows-x64.zip";
        info.assetSize = 42 * 1024 * 1024;
        info.hasUpdate = true;
        QVERIFY(!info.versionTag.isEmpty());
        QVERIFY(!info.downloadUrl.isEmpty());
        QVERIFY(info.assetSize > 0);
    }

    // Regression guard for the broken auto-update installer.
    //
    // The old script used relative paths (`xcopy . ..\..`, `start ..\PdfMark.exe`)
    // that resolve against %TEMP% and not the app folder, so the update was
    // written to the wrong place and a non-existent exe was launched, all
    // silently. These assertions pin the fixed, absolute-path behaviour.
    void testUpdateScriptTargetsAppDir() {
        const QString appDir = QStringLiteral("D:\\Tools\\PDFMark");
        const QString zip = QStringLiteral("C:\\Users\\bob\\AppData\\Local\\Temp\\PDFMark-Windows-x64.zip");
        const QString script = pdfmark::AutoUpdater::buildUpdateScript(appDir, zip);

        // The real app directory is injected and used for install + relaunch.
        QVERIFY(script.contains("set \"APP_DIR=D:\\Tools\\PDFMark\""));
        QVERIFY(script.contains("\"%APP_DIR%\\PdfMark.exe\""));
        QVERIFY(script.contains("xcopy"));
        // Must NOT use the old broken relative paths.
        QVERIFY(!script.contains("..\\.."));
        QVERIFY(!script.contains("start \"\" ..\\PdfMark.exe"));
        // Zip path is injected and quoted.
        QVERIFY(script.contains("C:\\Users\\bob\\AppData\\Local\\Temp\\PDFMark-Windows-x64.zip"));
        // Robustness essentials.
        QVERIFY(script.contains("taskkill /IM PdfMark.exe"));  // free the running exe
        QVERIFY(script.contains("rd /S /Q"));                  // recursive temp cleanup
        QVERIFY(script.contains("if errorlevel 1 goto :readonly")); // writable-dir failure path
        QVERIFY(script.contains("pause"));                    // surface failures to the user
        // Loop variable must survive verbatim (no arg() mangling).
        QVERIFY(script.contains("for /L %%i in (1,1,20) do ("));
    }
};

QTEST_MAIN(AutoUpdaterTest)
#include "AutoUpdaterTest.moc"
