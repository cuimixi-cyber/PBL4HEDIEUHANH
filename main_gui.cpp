#include <QApplication>
#include <QDir>
#include <QIcon>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Process Manager GUI");

    // Neu moi truong chua co theme icon (hay gap khi chay Qt tren GNOME khong cai plugin platform theme),
    // chon thu mot theme co san de cac icon he thong hien ra. Khong co thi IconProvider dung icon chuan cua Qt.
    if (QIcon::themeName().isEmpty()) {
        for (const char *name : {"Yaru", "Adwaita", "breeze", "hicolor"}) {
            if (QDir(QString("/usr/share/icons/") + name).exists()) {
                QIcon::setThemeName(name);
                break;
            }
        }
    }

    MainWindow window;
    window.show();

    return app.exec();
}
