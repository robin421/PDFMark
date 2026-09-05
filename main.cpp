#include <QApplication>
#include <QIcon>
#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Set the application icon for both the macOS Dock / Windows taskbar
    // and the in-app window title.
    QApplication::setWindowIcon(QIcon(":/icons/app_icon.png"));
    QApplication::setApplicationName("PDFMark");
    QApplication::setApplicationDisplayName("PDFMark");
    QApplication::setOrganizationName("PDFMark");

    pdfmark::MainWindow window;
    window.show();
    return app.exec();
}
