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
};

QTEST_MAIN(AutoUpdaterTest)
#include "AutoUpdaterTest.moc"
