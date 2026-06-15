#include "ui/MainWindow.h"
#include "web/EpubSchemeHandler.h"

#include <QApplication>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QTimer>
#include <QWebEngineProfile>

namespace {

bool isEpubArg(const QString &arg)
{
    return !arg.startsWith(QStringLiteral("--")) && QFileInfo::exists(arg)
           && arg.endsWith(QStringLiteral(".epub"), Qt::CaseInsensitive);
}

// QApplication subclass so macOS "open with" / dropping files on the Dock icon
// (delivered as QFileOpenEvent) each open a new window.
class SpindleApplication : public QApplication {
public:
    using QApplication::QApplication;

protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::FileOpen) {
            const QString file = static_cast<QFileOpenEvent *>(e)->file();
            if (file.endsWith(QStringLiteral(".epub"), Qt::CaseInsensitive)) {
                MainWindow::openInNewWindow(file);
                return true;
            }
        }
        return QApplication::event(e);
    }
};

} // namespace

int main(int argc, char *argv[])
{
    // Custom URL scheme must be registered before the application is created.
    EpubSchemeHandler::registerScheme();

    SpindleApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Spindle"));
    app.setOrganizationName(QStringLiteral("Spindle"));
    app.setWindowIcon(QIcon(QStringLiteral(":/spindle.png")));

    // One shared scheme handler on the default profile serves every window's book.
    QWebEngineProfile::defaultProfile()->installUrlSchemeHandler(
        EpubSchemeHandler::schemeName(), EpubSchemeHandler::instance());

    // Open each EPUB passed on the command line in its own window.
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (isEpubArg(args.at(i)))
            MainWindow::openInNewWindow(args.at(i));
    }

    // If nothing opened (no CLI files and no startup QFileOpenEvent), show an
    // empty window. Deferred so macOS launch "open with" events land first.
    QTimer::singleShot(0, &app, [] {
        if (MainWindow::instanceCount() == 0)
            MainWindow::openInNewWindow();
    });

    return app.exec();
}
