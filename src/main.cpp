#include "ui/MainWindow.h"
#include "web/EpubSchemeHandler.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>

int main(int argc, char *argv[])
{
    // Custom URL scheme must be registered before the application is created.
    EpubSchemeHandler::registerScheme();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Spindle"));
    app.setOrganizationName(QStringLiteral("Spindle"));
    app.setWindowIcon(QIcon(QStringLiteral(":/spindle.png")));

    MainWindow window;
    window.show();

    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg.startsWith(QStringLiteral("--")))
            continue;
        if (QFileInfo::exists(arg)) {
            window.openEpub(arg);
            break;
        }
    }

    return app.exec();
}
