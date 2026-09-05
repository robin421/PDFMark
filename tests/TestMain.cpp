#include <QGuiApplication>
#include <iostream>
#include <string>
#include <cstring>
#include <filesystem>

namespace pdfmark {
    void testCommonUtilities();
    void testWatermarkConfig();
    void testTileLayout();
    void testWatermarkVisual(const std::filesystem::path& outputDir);
    void testMemoryModel();
    void testMultiWatermarkBatch();
}

int main(int argc, char* argv[]) {
    // Set offscreen QPA platform for headless execution
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    std::string testName = "ALL";
    for (int i = 1; i < argc; ++i) {
        if ((std::strcmp(argv[i], "--test") == 0 || std::strcmp(argv[i], "-t") == 0) && i + 1 < argc) {
            testName = argv[i + 1];
            break;
        }
    }

    std::filesystem::path visualDir = "visual_samples";

    try {
        if (testName == "ALL" || testName == "Common") {
            pdfmark::testCommonUtilities();
        }
        if (testName == "ALL" || testName == "WatermarkConfig") {
            pdfmark::testWatermarkConfig();
        }
        if (testName == "ALL" || testName == "TileLayout") {
            pdfmark::testTileLayout();
        }
        if (testName == "ALL" || testName == "WatermarkVisual") {
            pdfmark::testWatermarkVisual(visualDir);
        }
        if (testName == "ALL" || testName == "MemoryModel") {
            pdfmark::testMemoryModel();
        }
        if (testName == "ALL" || testName == "MultiWatermarkBatch") {
            pdfmark::testMultiWatermarkBatch();
        }

        std::cout << "\n==============================\n";
        std::cout << "All selected tests passed successfully!\n";
        std::cout << "==============================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n[TEST FAILED]: " << e.what() << "\n";
        return 1;
    }
}