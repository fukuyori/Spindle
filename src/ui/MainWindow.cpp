#include "ui/MainWindow.h"

#include "core/AozoraExport.h"
#include "core/KindleImport.h"
#include "core/TranslatedEpub.h"
#include "core/Markdown.h"
#include "core/Matcher.h"
#include "core/Search.h"
#include "epub/EpubBook.h"
#include "epub/PathUtil.h"
#include "model/HighlightStore.h"
#include "model/SummaryStore.h"
#include "net/OllamaClient.h"
#include "web/Bridge.h"
#include "web/EpubSchemeHandler.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QCursor>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QChildEvent>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QTextEdit>
#include <QTextCursor>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QProgressDialog>
#include <QJsonArray>
#include <iterator>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QColorDialog>
#include <QFontComboBox>
#include <QSettings>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QStringList>
#include <QSlider>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace {

// Floating translation result popup: a Qt::Popup (so an outside click dismisses
// it) that also closes on Escape.
class TranslatePopup : public QLabel {
public:
    TranslatePopup() : QLabel(nullptr, Qt::Popup) {}

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            hide();
            return;
        }
        QLabel::keyPressEvent(event);
    }
};

class HighlightListDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.decorationAlignment = Qt::AlignLeft | Qt::AlignTop;
        QStyledItemDelegate::paint(painter, opt, index);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.decorationAlignment = Qt::AlignLeft | Qt::AlignTop;
        return QStyledItemDelegate::sizeHint(opt, index);
    }
};

QColor highlightQColor(HighlightColor c)
{
    switch (c) {
    case HighlightColor::Yellow: return QColor(255, 215, 64);
    case HighlightColor::Blue:   return QColor(64, 196, 255);
    case HighlightColor::Pink:   return QColor(255, 128, 171);
    case HighlightColor::Orange: return QColor(255, 167, 38);
    case HighlightColor::Green:  return QColor(105, 240, 174);
    case HighlightColor::Purple: return QColor(179, 136, 255);
    }
    return QColor(255, 215, 64);
}

QString highlightLabel(HighlightColor c)
{
    switch (c) {
    case HighlightColor::Yellow: return QStringLiteral("イエロー");
    case HighlightColor::Blue:   return QStringLiteral("ブルー");
    case HighlightColor::Pink:   return QStringLiteral("ピンク");
    case HighlightColor::Orange: return QStringLiteral("オレンジ");
    case HighlightColor::Green:  return QStringLiteral("グリーン");
    case HighlightColor::Purple: return QStringLiteral("パープル");
    }
    return {};
}

void openWebSearch(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    const QString q = QString::fromUtf8(QUrl::toPercentEncoding(trimmed));
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com/search?q=") + q));
}

struct Lang {
    const char *code;
    const char *name;  // handed to the model
    const char *label; // UI
};
const Lang kLangs[] = {{"ja", "Japanese", "日本語"}, {"en", "English", "English"},
                       {"zh", "Chinese", "中文"},     {"ko", "Korean", "한국어"},
                       {"fr", "French", "Français"},  {"de", "German", "Deutsch"},
                       {"es", "Spanish", "Español"}};
constexpr int kSummaryTranslateRequestId = -1001;

QString themeKeyForIndex(int theme)
{
    switch (theme) {
    case 0: return QStringLiteral("light");
    case 1: return QStringLiteral("sepia");
    case 2: return QStringLiteral("dark");
    }
    return QStringLiteral("light");
}

QString themeLabelForIndex(int theme)
{
    switch (theme) {
    case 0: return QStringLiteral("ライト");
    case 1: return QStringLiteral("セピア");
    case 2: return QStringLiteral("ダーク");
    }
    return QStringLiteral("ライト");
}

QColor adjustedBrightness(QColor color, int amount)
{
    float h = 0;
    float s = 0;
    float l = 0;
    float a = 1;
    color.getHslF(&h, &s, &l, &a);
    l = qBound(0.0f, l + amount / 100.0f, 1.0f);
    QColor out;
    out.setHslF(h, s, l, a);
    return out;
}

QColor baseThemeBackgroundForIndex(int theme)
{
    switch (theme) {
    case 0: return QColor("#ffffff");
    case 1: return QColor("#f4ecd8");
    case 2: return QColor("#1c1c1e");
    }
    return QColor("#ffffff");
}

QColor baseOriginalTextForIndex(int theme)
{
    switch (theme) {
    case 0: return QColor("#202124");
    case 1: return QColor("#4b3a26");
    case 2: return QColor("#d8d8da");
    }
    return QColor("#202124");
}

QString recentFileKey(const QString &filePath)
{
    const QString normalized = QFileInfo(filePath).absoluteFilePath().toCaseFolded();
    return QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(),
                                                        QCryptographicHash::Sha1)
                                   .toHex());
}

QString targetLanguageName(const QString &code)
{
    for (const Lang &l : kLangs)
        if (code == QLatin1String(l.code))
            return QString::fromUtf8(l.name);
    return code;
}

QString targetLanguagePrompt(const QString &code)
{
    for (const Lang &l : kLangs) {
        if (code == QLatin1String(l.code)) {
            return QStringLiteral("%1 (%2, ISO code: %3)")
                .arg(QString::fromUtf8(l.name), QString::fromUtf8(l.label),
                     QString::fromUtf8(l.code));
        }
    }
    return code;
}

QString compactPreview(const QString &text, qsizetype maxChars = 240)
{
    QString s = text;
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    s.replace(ws, QStringLiteral(" "));
    s = s.trimmed();
    if (s.size() > maxChars)
        s = s.left(maxChars) + QStringLiteral("...");
    return s;
}

QString translationExportFailureMessage(const QString &error, const QString &source,
                                        int index, int total, const QString &target,
                                        const QString &targetCode, const QString &model,
                                        const QString &diagnosticPath = {})
{
    QStringList lines;
    lines << QStringLiteral("翻訳に失敗しました:") << error << QString();
    lines << QStringLiteral("対象段落: %1/%2").arg(index).arg(total);
    lines << QStringLiteral("翻訳先: %1 (%2)").arg(target, targetCode);
    lines << QStringLiteral("モデル: %1").arg(model);
    lines << QStringLiteral("本文長: %1 文字").arg(source.size());
    lines << QStringLiteral("本文先頭: %1").arg(compactPreview(source));
    if (!diagnosticPath.isEmpty())
        lines << QString() << diagnosticPath;
    return lines.join(QLatin1Char('\n'));
}

QString translationDiagnosticPath(const QString &epubPath)
{
    const QFileInfo info(epubPath);
    const QString base = info.completeBaseName().isEmpty() ? QStringLiteral("book")
                                                           : info.completeBaseName();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    return info.dir().filePath(
        QStringLiteral("%1.spindle-translation-failure-%2.txt").arg(base, stamp));
}

bool writeTranslationDiagnostic(const QString &path, QString *writeError, const QString &epubPath,
                                const QString &bookTitle, const QString &bookLanguage,
                                const QString &endpoint, const QString &model,
                                const QString &targetName, const QString &targetCode,
                                const QString &targetPrompt, const QString &glossary,
                                const QString &source, int index, int total,
                                const QString &ollamaError)
{
    QStringList lines;
    lines << QStringLiteral("Spindle translation export diagnostic");
    lines << QStringLiteral("Timestamp: %1")
                 .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    lines << QStringLiteral("EPUB: %1").arg(epubPath);
    lines << QStringLiteral("Book title: %1").arg(bookTitle);
    lines << QStringLiteral("Book language: %1").arg(bookLanguage);
    lines << QStringLiteral("Target: %1 (%2)").arg(targetName, targetCode);
    lines << QStringLiteral("Target prompt: %1").arg(targetPrompt);
    lines << QStringLiteral("Endpoint: %1").arg(endpoint);
    lines << QStringLiteral("Model: %1").arg(model);
    lines << QStringLiteral("Paragraph: %1/%2").arg(index).arg(total);
    lines << QStringLiteral("Source length: %1 characters").arg(source.size());
    lines << QString();
    lines << QStringLiteral("Ollama error:");
    lines << ollamaError;
    lines << QString();
    lines << QStringLiteral("Glossary prompt sent:");
    lines << (glossary.isEmpty() ? QStringLiteral("(none)") : glossary);
    lines << QString();
    lines << QStringLiteral("Source text sent to Ollama:");
    lines << QStringLiteral("----- BEGIN SOURCE -----");
    lines << source;
    lines << QStringLiteral("----- END SOURCE -----");

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (writeError)
            *writeError = f.errorString();
        return false;
    }
    f.write(lines.join(QLatin1Char('\n')).toUtf8());
    if (!f.commit()) {
        if (writeError)
            *writeError = f.errorString();
        return false;
    }
    return true;
}

QIcon swatchIcon(HighlightColor c)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(highlightQColor(c));
    p.setPen(QPen(QColor(0, 0, 0, 60)));
    p.drawRoundedRect(1, 1, 14, 14, 3, 3);
    return QIcon(pm);
}

} // namespace

static int g_windowCount = 0;

int MainWindow::instanceCount() { return g_windowCount; }

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    static int windowCounter = 0;
    m_schemeId = QStringLiteral("b%1").arg(windowCounter++); // unique epub:// host
    ++g_windowCount;

    setWindowTitle(QStringLiteral("Spindle"));
    resize(1180, 800);
    setAcceptDrops(true);

    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(180);
    connect(m_searchDebounce, &QTimer::timeout, this, &MainWindow::runSearch);

    m_trCacheSave = new QTimer(this);
    m_trCacheSave->setSingleShot(true);
    m_trCacheSave->setInterval(1500);
    connect(m_trCacheSave, &QTimer::timeout, this, [this] { m_trCache.flush(); });

    m_summaryDetail = static_cast<SummaryDetail>(
        qBound(0, QSettings().value(QStringLiteral("summary/detail"), 1).toInt(), 2));

    buildUi();
    updateNavButtons();
    updateSidebarMode();

    // Reopen at the last window size/position (falls back to the resize() above).
    const QByteArray geo = QSettings().value(QStringLiteral("window/geometry")).toByteArray();
    if (!geo.isEmpty())
        restoreGeometry(geo);
}

MainWindow::~MainWindow()
{
    m_trCache.flush();
    EpubSchemeHandler::instance()->unregisterBook(m_schemeId);
    --g_windowCount;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings().setValue(QStringLiteral("window/geometry"), saveGeometry());
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    // Put keyboard focus on the reading view (not the search box), so Space/arrow
    // navigation works immediately. Deferred so the web view's render widget,
    // which is created lazily, exists by the time we focus it.
    if (m_view)
        QTimer::singleShot(0, m_view, [this] { if (m_view) m_view->setFocus(); });
}

MainWindow *MainWindow::openInNewWindow(const QString &filePath)
{
    auto *w = new MainWindow();
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->show();
    if (!filePath.isEmpty())
        w->openEpub(filePath);
    return w;
}

void MainWindow::openEpubSmart(const QString &filePath)
{
    if (m_book)
        openInNewWindow(filePath); // keep the current book; open another window
    else
        openEpub(filePath);
}

void MainWindow::showAboutDialog()
{
    QMessageBox::about(this, QStringLiteral("Spindle について"),
                       QStringLiteral("<h3>Spindle</h3><p>バージョン %1</p>")
                           .arg(QCoreApplication::applicationVersion()));
}

void MainWindow::openAppearanceDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("表示の明るさ"));
    QFormLayout *form = new QFormLayout(&dialog);

    QComboBox *themeBox = new QComboBox(&dialog);
    for (int i = 0; i < 3; ++i)
        themeBox->addItem(themeLabelForIndex(i), i);
    themeBox->setCurrentIndex(static_cast<int>(m_theme));
    form->addRow(QStringLiteral("テーマ"), themeBox);

    auto makeSlider = [&dialog](QSlider **sliderOut, QLabel **labelOut) {
        QWidget *row = new QWidget(&dialog);
        QHBoxLayout *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        QSlider *slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(-50, 50);
        slider->setTickPosition(QSlider::TicksBelow);
        slider->setTickInterval(25);
        QLabel *label = new QLabel(QStringLiteral("0"), row);
        label->setMinimumWidth(34);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(slider, 1);
        layout->addWidget(label);
        *sliderOut = slider;
        *labelOut = label;
        return row;
    };

    QSlider *backgroundSlider = nullptr;
    QSlider *originalSlider = nullptr;
    QSlider *translationSlider = nullptr;
    QLabel *backgroundLabel = nullptr;
    QLabel *originalLabel = nullptr;
    QLabel *translationLabel = nullptr;
    form->addRow(QStringLiteral("背景"), makeSlider(&backgroundSlider, &backgroundLabel));
    form->addRow(QStringLiteral("原文"), makeSlider(&originalSlider, &originalLabel));
    form->addRow(QStringLiteral("翻訳文"), makeSlider(&translationSlider, &translationLabel));

    auto updateLabels = [=] {
        backgroundLabel->setText(QString::number(backgroundSlider->value()));
        originalLabel->setText(QString::number(originalSlider->value()));
        translationLabel->setText(QString::number(translationSlider->value()));
    };

    auto loadSliders = [=](int theme) {
        QSignalBlocker b1(backgroundSlider);
        QSignalBlocker b2(originalSlider);
        QSignalBlocker b3(translationSlider);
        backgroundSlider->setValue(m_brightness[theme].background);
        originalSlider->setValue(m_brightness[theme].original);
        translationSlider->setValue(m_brightness[theme].translation);
        updateLabels();
    };

    auto saveSliders = [=] {
        const int theme = themeBox->currentData().toInt();
        m_brightness[theme].background = backgroundSlider->value();
        m_brightness[theme].original = originalSlider->value();
        m_brightness[theme].translation = translationSlider->value();

        QSettings settings;
        const QString prefix = QStringLiteral("appearance/%1/").arg(themeKeyForIndex(theme));
        settings.setValue(prefix + QStringLiteral("backgroundBrightness"),
                          m_brightness[theme].background);
        settings.setValue(prefix + QStringLiteral("originalBrightness"),
                          m_brightness[theme].original);
        settings.setValue(prefix + QStringLiteral("translationBrightness"),
                          m_brightness[theme].translation);

        if (theme == static_cast<int>(m_theme)) {
            if (m_view)
                m_view->page()->setBackgroundColor(themeBackground());
            injectViewStyle();
        }
    };

    connect(themeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [=](int) { loadSliders(themeBox->currentData().toInt()); });
    for (QSlider *slider : {backgroundSlider, originalSlider, translationSlider}) {
        connect(slider, &QSlider::valueChanged, this, [=](int) {
            updateLabels();
            saveSliders();
        });
    }
    loadSliders(static_cast<int>(m_theme));

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    dialog.exec();
}

void MainWindow::buildUi()
{
    menuBar()->setNativeMenuBar(true);

    QAction *openAction = new QAction(QStringLiteral("EPUB を開く…"), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenTriggered);
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("ファイル"));
    fileMenu->addAction(openAction);
    fileMenu->addAction(QStringLiteral("最近開いた EPUB を表示"), this,
                        &MainWindow::showRecentEpubsPane);
    m_recentEpubsMenu = fileMenu->addMenu(QStringLiteral("最近開いた EPUB"));
    connect(m_recentEpubsMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentEpubsMenu);
    updateRecentEpubsMenu();

    QMenu *hlMenu = menuBar()->addMenu(QStringLiteral("ハイライト"));
    hlMenu->addAction(QStringLiteral("Kindle ノートを読み込み…"), this,
                      &MainWindow::importKindleNotebook);
    hlMenu->addSeparator();
    hlMenu->addAction(QStringLiteral("Markdown で書き出し…"), this,
                      &MainWindow::exportHighlightsMarkdown);
    hlMenu->addAction(QStringLiteral("JSON で書き出し…"), this, &MainWindow::exportHighlightsJson);
    hlMenu->addSeparator();
    hlMenu->addAction(QStringLiteral("読み込み (Markdown / JSON)…"), this,
                      &MainWindow::importHighlights);

    QMenu *chapterMenu = menuBar()->addMenu(QStringLiteral("章"));
    chapterMenu->addAction(QStringLiteral("青空文庫 XHTML で書き出し…"), this,
                           &MainWindow::exportChapterAozora);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("表示"));
    viewMenu->addAction(QStringLiteral("明るさ調整…"), this, &MainWindow::openAppearanceDialog);

    QMenu *trMenu = menuBar()->addMenu(QStringLiteral("翻訳"));
    trMenu->addAction(QStringLiteral("設定…"), this, &MainWindow::openTranslateDialog);
    trMenu->addSeparator();
    trMenu->addAction(QStringLiteral("対訳 EPUB を書き出し…"), this,
                      [this] { exportTranslatedEpub(0); });
    trMenu->addAction(QStringLiteral("訳文 EPUB を書き出し…"), this,
                      [this] { exportTranslatedEpub(1); });

    QMenu *summaryMenu = menuBar()->addMenu(QStringLiteral("要約"));
    summaryMenu->addAction(QStringLiteral("現在の章を要約"), this,
                           &MainWindow::summarizeCurrentChapter);
    summaryMenu->addAction(QStringLiteral("現在の章を再要約"), this,
                           &MainWindow::regenerateCurrentChapterSummary);
    summaryMenu->addAction(QStringLiteral("保存済みの章要約を開く"), this,
                           &MainWindow::openSavedCurrentChapterSummary);
    summaryMenu->addAction(QStringLiteral("設定…"), this,
                           &MainWindow::openSummarySettingsDialog);
    summaryMenu->addSeparator();
    QMenu *summaryDetailMenu = summaryMenu->addMenu(QStringLiteral("粒度"));
    QActionGroup *summaryDetailGroup = new QActionGroup(this);
    summaryDetailGroup->setExclusive(true);
    const struct {
        SummaryDetail detail;
        const char *label;
    } summaryDetails[] = {{SummaryDetail::Brief, "短め"},
                          {SummaryDetail::Standard, "標準"},
                          {SummaryDetail::Detailed, "詳しく"}};
    for (const auto &item : summaryDetails) {
        QAction *action = summaryDetailMenu->addAction(QString::fromUtf8(item.label));
        action->setCheckable(true);
        action->setChecked(m_summaryDetail == item.detail);
        summaryDetailGroup->addAction(action);
        connect(action, &QAction::triggered, this,
                [this, item] { setSummaryDetail(static_cast<int>(item.detail)); });
    }

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("ヘルプ"));
    helpMenu->addAction(QStringLiteral("Spindle について"), this, &MainWindow::showAboutDialog);

    QToolBar *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setAllowedAreas(Qt::TopToolBarArea);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    QAction *openToolbarAction = toolbar->addAction(QStringLiteral("開く"));
    openToolbarAction->setToolTip(QStringLiteral("EPUB を開く"));
    connect(openToolbarAction, &QAction::triggered, this, &MainWindow::onOpenTriggered);
    toolbar->addSeparator();

    m_sidebarAction = toolbar->addAction(QStringLiteral("サイドバー"));
    m_sidebarAction->setCheckable(true);
    m_sidebarAction->setChecked(true);
    m_sidebarAction->setToolTip(QStringLiteral("左ペインの表示/非表示"));
    connect(m_sidebarAction, &QAction::toggled, this,
            [this](bool on) {
                if (m_sidebar)
                    m_sidebar->setVisible(on);
            });
    QAction *recentToolbarAction = toolbar->addAction(QStringLiteral("履歴"));
    recentToolbarAction->setToolTip(QStringLiteral("最近開いた EPUB を左ペインに表示"));
    connect(recentToolbarAction, &QAction::triggered, this, &MainWindow::showRecentEpubsPane);
    toolbar->addSeparator();
    m_prevAction = toolbar->addAction(QStringLiteral("←"));
    m_prevAction->setToolTip(QStringLiteral("前の章"));
    m_nextAction = toolbar->addAction(QStringLiteral("→"));
    m_nextAction->setToolTip(QStringLiteral("次の章"));
    connect(m_prevAction, &QAction::triggered, this, &MainWindow::previousChapter);
    connect(m_nextAction, &QAction::triggered, this, &MainWindow::nextChapter);
    toolbar->addSeparator();
    QToolButton *aiButton = new QToolButton(toolbar);
    aiButton->setText(QStringLiteral("AI"));
    aiButton->setToolTip(QStringLiteral("翻訳と要約"));
    aiButton->setPopupMode(QToolButton::InstantPopup);
    QMenu *aiMenu = new QMenu(aiButton);
    aiMenu->addAction(QStringLiteral("翻訳設定…"), this, &MainWindow::openTranslateDialog);
    aiMenu->addAction(QStringLiteral("現在の章を要約"), this, &MainWindow::summarizeCurrentChapter);
    aiMenu->addAction(QStringLiteral("要約設定…"), this, &MainWindow::openSummarySettingsDialog);
    aiButton->setMenu(aiMenu);
    toolbar->addWidget(aiButton);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("A−"), this, &MainWindow::decreaseFont);
    toolbar->addAction(QStringLiteral("A+"), this, &MainWindow::increaseFont);
    toolbar->addAction(QStringLiteral("テーマ"), this, &MainWindow::cycleTheme);
    QAction *xmlAction = toolbar->addAction(QStringLiteral("XML"));
    xmlAction->setCheckable(true);
    xmlAction->setToolTip(QStringLiteral("章の XHTML ソースを表示"));
    connect(xmlAction, &QAction::toggled, this, &MainWindow::toggleXmlView);

    toolbar->addSeparator();
    m_fontCombo = new QFontComboBox(this);
    m_fontCombo->setMaximumWidth(170);
    m_fontCombo->setToolTip(QStringLiteral("本文フォント（「適用」で本のフォントを上書き）"));
    toolbar->addWidget(m_fontCombo);
    m_fontOverride = toolbar->addAction(QStringLiteral("適用"));
    m_fontOverride->setCheckable(true);
    m_fontOverride->setToolTip(
        QStringLiteral("選択したフォントを本文に適用（オフで本のフォントに戻す）"));
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this,
            [this] { applyFontChoice(); });
    connect(m_fontOverride, &QAction::toggled, this, [this] { applyFontChoice(); });

    m_location = new QLabel(QStringLiteral("No book loaded"), this);
    statusBar()->addWidget(m_location);

    // --- sidebar ---
    QWidget *sidebar = new QWidget(this);
    m_sidebar = sidebar;
    QVBoxLayout *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(10, 10, 10, 10);
    sideLayout->setSpacing(8);

    m_titleLabel = new QLabel(QStringLiteral("EPUB を開いてください"), sidebar);
    m_titleLabel->setWordWrap(true);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_authorLabel = new QLabel(QStringLiteral("ローカルファイルを選択して読み始めます"), sidebar);
    m_authorLabel->setWordWrap(true);

    m_searchInput = new QLineEdit(sidebar);
    m_searchInput->setPlaceholderText(QStringLiteral("本文を検索..."));
    m_searchInput->setClearButtonEnabled(true);
    connect(m_searchInput, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);

    QWidget *tabs = new QWidget(sidebar);
    QHBoxLayout *tabsLayout = new QHBoxLayout(tabs);
    tabsLayout->setContentsMargins(0, 0, 0, 0);
    tabsLayout->setSpacing(6);
    m_tabToc = new QPushButton(QStringLiteral("目次"), tabs);
    m_tabHighlights = new QPushButton(QStringLiteral("ハイライト"), tabs);
    m_tabRecent = new QPushButton(QStringLiteral("履歴"), tabs);
    m_tabToc->setCheckable(true);
    m_tabHighlights->setCheckable(true);
    m_tabRecent->setCheckable(true);
    m_tabToc->setChecked(true);
    connect(m_tabToc, &QPushButton::clicked, this, [this] { showSidebarTab(0); });
    connect(m_tabHighlights, &QPushButton::clicked, this, [this] { showSidebarTab(1); });
    connect(m_tabRecent, &QPushButton::clicked, this, [this] { showSidebarTab(3); });
    tabsLayout->addWidget(m_tabToc);
    tabsLayout->addWidget(m_tabHighlights);
    tabsLayout->addWidget(m_tabRecent);

    m_toc = new QTreeWidget(sidebar);
    m_toc->setHeaderHidden(true);
    connect(m_toc, &QTreeWidget::itemClicked, this, &MainWindow::onTocItemActivated);

    m_highlightsList = new QListWidget(sidebar);
    m_highlightsList->setWordWrap(true);
    m_highlightsList->setItemDelegate(new HighlightListDelegate(m_highlightsList));
    m_highlightsList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_highlightsList, &QListWidget::itemClicked, this, &MainWindow::onHighlightActivated);
    connect(m_highlightsList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QListWidgetItem *item = m_highlightsList->itemAt(pos);
                if (!item)
                    return;
                QMenu menu;
                QAction *del = menu.addAction(QStringLiteral("削除"));
                if (menu.exec(m_highlightsList->mapToGlobal(pos)) == del)
                    removeHighlightById(item->data(Qt::UserRole).toString());
            });

    m_searchResults = new QListWidget(sidebar);
    m_searchResults->setWordWrap(true);
    connect(m_searchResults, &QListWidget::itemClicked, this, &MainWindow::onSearchResultActivated);

    m_recentEpubsList = new QListWidget(sidebar);
    m_recentEpubsList->setWordWrap(true);
    connect(m_recentEpubsList, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                if (!item)
                    return;
                const QString path = item->data(Qt::UserRole).toString();
                if (!path.isEmpty())
                    openRecentEpub(path);
            });

    m_sidebarStack = new QStackedWidget(sidebar);
    m_sidebarStack->addWidget(m_toc);            // 0
    m_sidebarStack->addWidget(m_highlightsList); // 1
    m_sidebarStack->addWidget(m_searchResults);  // 2
    m_sidebarStack->addWidget(m_recentEpubsList); // 3

    sideLayout->addWidget(m_titleLabel);
    sideLayout->addWidget(m_authorLabel);
    sideLayout->addWidget(m_searchInput);
    sideLayout->addWidget(tabs);
    sideLayout->addWidget(m_sidebarStack, 1);
    updateRecentEpubsView();
    if (!recentEpubs().isEmpty())
        showSidebarTab(3);

    // --- web viewer ---
    // The epub:// scheme handler is installed once, globally, on the default
    // profile (see main()); this window just registers its book under m_schemeId.
    m_view = new QWebEngineView(this);
    // Catch EPUB drops over the page area: the view's render widget (a lazily
    // created child) handles drops itself, so watch it and its descendants.
    m_view->installEventFilter(this);
    m_view->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    connect(m_view, &QWebEngineView::loadFinished, this, &MainWindow::onLoadFinished);
    setupWebChannel();

    QSplitter *splitter = new QSplitter(this);
    splitter->addWidget(sidebar);
    splitter->addWidget(m_view);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 880});
    setCentralWidget(splitter);
}

void MainWindow::setupWebChannel()
{
    m_bridge = new Bridge(this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject(QStringLiteral("spindle"), m_bridge);
    m_view->page()->setWebChannel(m_channel);
    connect(m_bridge, &Bridge::selectionReceived, this, &MainWindow::onWebSelection);
    connect(m_bridge, &Bridge::markActivated, this, &MainWindow::onMarkClicked);
    connect(m_bridge, &Bridge::blocksReady, this, &MainWindow::onBlocksReady);

    m_ollama = new OllamaClient(this);
    connect(m_ollama, &OllamaClient::finished, this, &MainWindow::onOllamaFinished);

    m_selectionOllama = new OllamaClient(this);
    connect(m_selectionOllama, &OllamaClient::finished, this,
            &MainWindow::onSelectionTranslated);
    m_summaryOllama = new OllamaClient(this);
    connect(m_summaryOllama, &OllamaClient::finished, this, &MainWindow::onSummaryFinished);

    QSettings settings;
    m_trTarget = settings.value(QStringLiteral("translate/target"), m_trTarget).toString();
    m_trModel = settings.value(QStringLiteral("translate/model"), m_trModel).toString();
    m_summaryModel = settings.value(QStringLiteral("summary/model")).toString().trimmed();
    m_trEndpoint = settings.value(QStringLiteral("translate/endpoint"), m_trEndpoint).toString();
    m_trColor = settings.value(QStringLiteral("translate/color")).toString();

    // Restore the theme and translation view used last time.
    m_theme = static_cast<Theme>(
        qBound(0, settings.value(QStringLiteral("view/theme"), 0).toInt(), 2));
    for (int i = 0; i < 3; ++i) {
        const QString prefix = QStringLiteral("appearance/%1/").arg(themeKeyForIndex(i));
        m_brightness[i].background =
            qBound(-50, settings.value(prefix + QStringLiteral("backgroundBrightness"), 0).toInt(),
                   50);
        m_brightness[i].original =
            qBound(-50, settings.value(prefix + QStringLiteral("originalBrightness"), 0).toInt(),
                   50);
        m_brightness[i].translation =
            qBound(-50, settings.value(prefix + QStringLiteral("translationBrightness"), 0).toInt(),
                   50);
    }
    m_translateView = static_cast<TranslateView>(
        qBound(0, settings.value(QStringLiteral("translate/view"), 0).toInt(), 2));

    // Restore the font choice without firing the change handlers (which would
    // persist defaults). injectViewStyle runs on each chapter load.
    const bool fontOn = settings.value(QStringLiteral("font/override"), false).toBool();
    const QString fontFamily = settings.value(QStringLiteral("font/family")).toString();
    if (m_fontCombo && !fontFamily.isEmpty()) {
        QSignalBlocker block(m_fontCombo);
        m_fontCombo->setCurrentFont(QFont(fontFamily));
    }
    if (m_fontOverride) {
        QSignalBlocker block(m_fontOverride);
        m_fontOverride->setChecked(fontOn);
    }
    m_fontFamily = (fontOn && m_fontCombo) ? m_fontCombo->currentFont().family() : QString();

    auto readResource = [](const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };

    // qwebchannel.js must exist before our reader script runs.
    const QString qwebchannel = readResource(QStringLiteral(":/qtwebchannel/qwebchannel.js"));
    if (!qwebchannel.isEmpty()) {
        QWebEngineScript s;
        s.setName(QStringLiteral("qwebchannel"));
        s.setSourceCode(qwebchannel);
        s.setInjectionPoint(QWebEngineScript::DocumentCreation);
        s.setWorldId(QWebEngineScript::MainWorld);
        s.setRunsOnSubFrames(false);
        m_view->page()->scripts().insert(s);
    }

    const QString reader = readResource(QStringLiteral(":/reader.js"));
    if (!reader.isEmpty()) {
        QWebEngineScript s;
        s.setName(QStringLiteral("spindle-reader"));
        s.setSourceCode(reader);
        s.setInjectionPoint(QWebEngineScript::DocumentReady);
        s.setWorldId(QWebEngineScript::MainWorld);
        s.setRunsOnSubFrames(false);
        m_view->page()->scripts().insert(s);
    }
}

// --- file opening ----------------------------------------------------------

void MainWindow::onOpenTriggered()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("EPUB を開く"), QString(), QStringLiteral("EPUB (*.epub)"));
    if (!path.isEmpty())
        openEpubSmart(path);
}

QStringList MainWindow::recentEpubs() const
{
    return QSettings().value(QStringLiteral("recent/epubs")).toStringList();
}

void MainWindow::addRecentEpub(const QString &filePath)
{
    const QFileInfo info(filePath);
    const QString path = info.exists() ? info.absoluteFilePath() : filePath;
    if (path.isEmpty())
        return;

    QStringList next;
    next.append(path);
    for (const QString &existing : recentEpubs()) {
        if (existing.compare(path, Qt::CaseInsensitive) == 0)
            continue;
        next.append(existing);
        if (next.size() >= 8)
            break;
    }
    QSettings().setValue(QStringLiteral("recent/epubs"), next);
    updateRecentEpubsMenu();
    updateRecentEpubsView();
}

void MainWindow::removeRecentEpub(const QString &filePath)
{
    QStringList next;
    for (const QString &existing : recentEpubs()) {
        if (existing.compare(filePath, Qt::CaseInsensitive) != 0)
            next.append(existing);
    }
    QSettings().setValue(QStringLiteral("recent/epubs"), next);
    QSettings().remove(QStringLiteral("recent/chapters/%1").arg(recentFileKey(filePath)));
    updateRecentEpubsMenu();
    updateRecentEpubsView();
}

void MainWindow::updateRecentEpubsMenu()
{
    if (!m_recentEpubsMenu)
        return;

    m_recentEpubsMenu->clear();
    const QStringList paths = recentEpubs();
    if (paths.isEmpty()) {
        QAction *empty = m_recentEpubsMenu->addAction(QStringLiteral("(履歴なし)"));
        empty->setEnabled(false);
        return;
    }

    int index = 1;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        const QString name = info.fileName().isEmpty() ? path : info.fileName();
        QAction *action = m_recentEpubsMenu->addAction(QStringLiteral("%1. %2").arg(index++).arg(name));
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path] { openRecentEpub(path); });
    }
}

void MainWindow::updateRecentEpubsView()
{
    if (!m_recentEpubsList)
        return;

    m_recentEpubsList->clear();
    const QStringList paths = recentEpubs();
    if (paths.isEmpty()) {
        QListWidgetItem *empty = new QListWidgetItem(QStringLiteral("履歴なし"), m_recentEpubsList);
        empty->setFlags(empty->flags() & ~Qt::ItemIsEnabled);
        return;
    }

    for (const QString &path : paths) {
        const QFileInfo info(path);
        const QString name = info.fileName().isEmpty() ? path : info.fileName();
        const QString folder = info.absolutePath();
        const QString chapter = recentChapterLabel(path);
        const QString detail =
            chapter.isEmpty() ? folder : QStringLiteral("%1\n最後: %2").arg(folder, chapter);
        auto *item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(name, detail),
                                         m_recentEpubsList);
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
        if (!info.exists()) {
            item->setText(QStringLiteral("%1\n%2").arg(name, QStringLiteral("見つかりません")));
            item->setForeground(Qt::gray);
        }
    }
}

int MainWindow::recentChapterIndex(const QString &filePath) const
{
    const QString key = recentFileKey(filePath);
    return QSettings().value(QStringLiteral("recent/chapters/%1/index").arg(key), 0).toInt();
}

QString MainWindow::recentChapterLabel(const QString &filePath) const
{
    const QString key = recentFileKey(filePath);
    return QSettings().value(QStringLiteral("recent/chapters/%1/label").arg(key)).toString();
}

void MainWindow::saveRecentChapter(const QString &filePath, int index, const QString &label)
{
    if (filePath.isEmpty() || index < 0)
        return;
    const QString key = recentFileKey(filePath);
    QSettings settings;
    settings.setValue(QStringLiteral("recent/chapters/%1/index").arg(key), index);
    settings.setValue(QStringLiteral("recent/chapters/%1/label").arg(key), label);
    updateRecentEpubsView();
}

void MainWindow::showRecentEpubsPane()
{
    updateRecentEpubsView();
    if (m_sidebar)
        m_sidebar->setVisible(true);
    if (m_sidebarAction) {
        QSignalBlocker block(m_sidebarAction);
        m_sidebarAction->setChecked(true);
    }
    if (m_searchInput)
        m_searchInput->clear();
    showSidebarTab(3);
}

void MainWindow::openRecentEpub(const QString &filePath)
{
    if (!QFileInfo::exists(filePath)) {
        QMessageBox::warning(this, QStringLiteral("Spindle"),
                             QStringLiteral("履歴の EPUB が見つかりません:\n%1").arg(filePath));
        removeRecentEpub(filePath);
        return;
    }

    if (m_book) {
        openInNewWindow(filePath);
    } else if (!openEpub(filePath)) {
        return;
    }

    if (m_searchInput)
        m_searchInput->clear();
    showSidebarTab(0);
    if (m_sidebar)
        m_sidebar->setVisible(true);
    if (m_sidebarAction) {
        QSignalBlocker block(m_sidebarAction);
        m_sidebarAction->setChecked(true);
    }
}

bool MainWindow::openEpub(const QString &filePath)
{
    auto book = std::make_unique<EpubBook>();
    if (!book->open(filePath)) {
        QMessageBox::warning(this, QStringLiteral("Spindle"),
                             QStringLiteral("EPUB を読み込めませんでした:\n%1")
                                 .arg(book->errorString()));
        return false;
    }

    m_book = std::move(book);
    m_epubPath = filePath;
    EpubSchemeHandler::instance()->registerBook(m_schemeId, m_book.get());
    m_chapterTexts.clear();
    m_chapterTextsReady = false;
    m_searchInput->clear();

    m_bookId = bookId(m_book->title(), m_book->author());
    m_highlights = highlight_store::load(m_epubPath);
    m_trCache.load(m_epubPath, m_trTarget);
    m_trGlossary.load(m_epubPath, m_trTarget);

    m_titleLabel->setText(m_book->title().isEmpty() ? QStringLiteral("(無題)") : m_book->title());
    m_authorLabel->setText(m_book->author());
    setWindowTitle(m_book->title().isEmpty()
                       ? QStringLiteral("Spindle")
                       : QStringLiteral("%1 — Spindle").arg(m_book->title()));

    populateToc();
    renderHighlightsList();
    const int savedChapter = qBound(0, recentChapterIndex(filePath),
                                    qMax(0, m_book->chapters().size() - 1));
    displayChapter(savedChapter);
    showSidebarTab(0);
    addRecentEpub(filePath);
    return true;
}

// --- table of contents -----------------------------------------------------

void MainWindow::populateToc()
{
    m_toc->clear();
    if (!m_book)
        return;
    addTocItems(m_book->toc(), nullptr);
    m_toc->expandAll();
}

void MainWindow::addTocItems(const QVector<TocItem> &items, QTreeWidgetItem *parent)
{
    for (const TocItem &item : items) {
        QTreeWidgetItem *node = parent ? new QTreeWidgetItem(parent)
                                       : new QTreeWidgetItem(m_toc);
        node->setText(0, item.label);
        node->setData(0, Qt::UserRole, item.href);
        if (item.href.isEmpty())
            node->setFlags(node->flags() & ~Qt::ItemIsSelectable);
        addTocItems(item.subitems, node);
    }
}

void MainWindow::onTocItemActivated(QTreeWidgetItem *item, int)
{
    if (!item || !m_book)
        return;
    const QString href = item->data(0, Qt::UserRole).toString();
    if (href.isEmpty())
        return;
    const int index = m_book->chapterIndexForPath(path_util::stripHash(href));
    if (index >= 0)
        displayChapter(index, path_util::hashOf(href));
}

// --- chapter display -------------------------------------------------------

void MainWindow::displayChapter(int index, const QString &fragment)
{
    if (!m_book || index < 0 || index >= m_book->chapters().size())
        return;

    m_currentChapter = index;
    const Chapter &chapter = m_book->chapters().at(index);
    saveRecentChapter(m_epubPath, index, chapter.label);

    // Raw XHTML source view: render the chapter markup as escaped monospace text.
    if (m_xmlView) {
        const QString raw = m_book->readText(chapter.path).toHtmlEscaped();
        const QString html =
            QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"><style>"
                           "html,body{margin:0;} pre{white-space:pre-wrap;word-break:break-word;"
                           "font-family:Menlo,Consolas,'DejaVu Sans Mono',monospace;font-size:12px;"
                           "line-height:1.5;padding:16px;margin:0;}</style></head><body><pre>%1"
                           "</pre></body></html>")
                .arg(raw);
        m_view->page()->setBackgroundColor(themeBackground());
        m_view->setHtml(html);
        updateLocation();
        updateNavButtons();
        return;
    }

    // Make this chapter's highlights / translation mode available to the page
    // before it loads so the injected reader script can act on DocumentReady.
    ++m_trRunId; // abandon any in-flight translation from the previous chapter
    m_trQueue.clear();
    m_trReqs.clear();
    m_trInFlight = 0;
    if (m_bridge) {
        m_bridge->setHighlightsJson(chapterHighlightsJson());
        m_bridge->setTranslateView(translateViewString());
        m_bridge->setTranslateLang(m_trTarget);
    }

    m_pendingFragment = fragment;
    QString urlStr = EpubSchemeHandler::urlFor(m_schemeId, chapter.path);
    if (!fragment.isEmpty())
        urlStr += QLatin1Char('#') + fragment;
    m_view->page()->setBackgroundColor(themeBackground());
    m_view->setUrl(QUrl(urlStr));

    updateLocation();
    updateNavButtons();
}

void MainWindow::onLoadFinished(bool ok)
{
    if (!ok)
        return;
    applyZoom();
    injectViewStyle();

    if (!m_pendingFragment.isEmpty()) {
        const QString frag = m_pendingFragment;
        m_pendingFragment.clear();
        const QString js = QStringLiteral(
            "(function(){var e=document.getElementById(%1)||"
            "document.getElementsByName(%1)[0];if(e)e.scrollIntoView();})();")
            .arg(QStringLiteral("'") + frag + QStringLiteral("'"));
        m_view->page()->runJavaScript(js);
    }
    if (!m_pendingFind.isEmpty()) {
        const QString find = m_pendingFind;
        m_pendingFind.clear();
        m_view->findText(find);
    }
    if (!m_pendingScrollId.isEmpty()) {
        const QString id = m_pendingScrollId;
        m_pendingScrollId.clear();
        if (m_bridge)
            m_bridge->requestScrollToHighlight(id); // reader.js retries until the mark exists
    }
    if (m_view)
        m_view->setFocus(); // keep keyboard focus on the reading pane after a load
}

void MainWindow::applyZoom()
{
    if (m_view)
        m_view->setZoomFactor(m_fontSize / 100.0);
}

QColor MainWindow::themeBackground() const
{
    const int idx = static_cast<int>(m_theme);
    return adjustedBrightness(baseThemeBackgroundForIndex(idx), m_brightness[idx].background);
}

void MainWindow::injectViewStyle()
{
    if (!m_view)
        return;
    const QString bg = themeBackground().name();
    const QString original = originalTextColor().name();
    const QString translation = translationTextColor().name();
    QString css = QStringLiteral(
        "html,body{background:%1 !important;}"
        "body,body *{color:%2 !important;}"
        ".spindle-translation,.spindle-translation *{color:%3 !important;}")
                      .arg(bg, original, translation);
    // Comfortable left/right reading margins (physical, so they apply equally to
    // horizontal and vertical-rl writing modes). box-sizing keeps them inside.
    css += QStringLiteral(" html{box-sizing:border-box;padding-left:6%;padding-right:6%;}");

    // Optional font override: force the chosen family over the book's own fonts.
    // Skipped in the raw-XHTML source view (which wants its monospace styling).
    if (!m_fontFamily.isEmpty() && !m_xmlView) {
        QString fam = m_fontFamily;
        fam.remove(QLatin1Char('`')).remove(QLatin1Char('\''))
            .remove(QLatin1Char('\\'));
        css += QStringLiteral(" body, body *{ font-family:'%1' !important; }").arg(fam);
    }

    const QString js = QStringLiteral(
        "(function(){var s=document.getElementById('__spindle_theme');"
        "if(!s){s=document.createElement('style');s.id='__spindle_theme';"
        "document.documentElement.appendChild(s);}s.textContent=`%1`;})();")
        .arg(css);
    m_view->page()->runJavaScript(js);
}

QColor MainWindow::originalTextColor() const
{
    const int idx = static_cast<int>(m_theme);
    return adjustedBrightness(baseOriginalTextForIndex(idx), m_brightness[idx].original);
}

QColor MainWindow::translationTextColor() const
{
    const int idx = static_cast<int>(m_theme);
    const QString tint = translationColor();
    const QColor base = tint.isEmpty() ? baseOriginalTextForIndex(idx) : QColor(tint);
    return adjustedBrightness(base, m_brightness[idx].translation);
}

QString MainWindow::translationColor() const
{
    if (m_trColor.isEmpty())
        return {};
    if (m_trColor.startsWith(QLatin1Char('#')))
        return m_trColor; // custom: same color across themes
    struct Preset {
        const char *key, *light, *sepia, *dark;
    };
    static const Preset presets[] = {
        {"blue", "#3a5f8a", "#4a6678", "#9db8d6"},
        {"teal", "#2f6f6a", "#3f6f68", "#8fcfc4"},
        {"gray", "#5a5a5a", "#6b5d4a", "#a8a8ac"},
        {"green", "#3a6b45", "#4a6b40", "#9fce9f"},
    };
    for (const Preset &p : presets) {
        if (m_trColor == QLatin1String(p.key))
            return QString::fromLatin1(m_theme == Theme::Dark    ? p.dark
                                       : m_theme == Theme::Sepia ? p.sepia
                                                                 : p.light);
    }
    return {};
}

void MainWindow::applyFontChoice()
{
    const bool on = m_fontOverride && m_fontOverride->isChecked();
    m_fontFamily = (on && m_fontCombo) ? m_fontCombo->currentFont().family() : QString();

    QSettings settings;
    settings.setValue(QStringLiteral("font/override"), on);
    if (m_fontCombo)
        settings.setValue(QStringLiteral("font/family"), m_fontCombo->currentFont().family());

    injectViewStyle();
}

void MainWindow::updateLocation()
{
    if (!m_book || m_currentChapter < 0) {
        m_location->setText(QStringLiteral("No book loaded"));
        return;
    }
    m_location->setText(QStringLiteral("%1 / %2 — %3")
                            .arg(m_currentChapter + 1)
                            .arg(m_book->chapters().size())
                            .arg(m_book->chapters().at(m_currentChapter).label));
}

void MainWindow::updateNavButtons()
{
    const bool has = m_book != nullptr;
    if (m_prevAction)
        m_prevAction->setEnabled(has && m_currentChapter > 0);
    if (m_nextAction)
        m_nextAction->setEnabled(has && m_currentChapter < m_book->chapters().size() - 1);
}

// --- navigation / controls -------------------------------------------------

void MainWindow::nextChapter()
{
    if (m_book && m_currentChapter < m_book->chapters().size() - 1)
        displayChapter(m_currentChapter + 1);
}

void MainWindow::previousChapter()
{
    if (m_book && m_currentChapter > 0)
        displayChapter(m_currentChapter - 1);
}

void MainWindow::increaseFont() { m_fontSize = qMin(m_fontSize + 10, 200); applyZoom(); }
void MainWindow::decreaseFont() { m_fontSize = qMax(m_fontSize - 10, 50); applyZoom(); }
void MainWindow::cycleTheme()
{
    m_theme = static_cast<Theme>((static_cast<int>(m_theme) + 1) % 3);
    QSettings().setValue(QStringLiteral("view/theme"), static_cast<int>(m_theme));
    m_view->page()->setBackgroundColor(themeBackground());
    injectViewStyle();
}

void MainWindow::toggleXmlView(bool on)
{
    m_xmlView = on;
    if (m_book && m_currentChapter >= 0)
        displayChapter(m_currentChapter); // re-render in the chosen mode
}

// --- search ----------------------------------------------------------------

void MainWindow::onSearchTextChanged()
{
    m_searchDebounce->start();
    updateSidebarMode();
}

void MainWindow::ensureChapterTexts()
{
    if (m_chapterTextsReady || !m_book)
        return;
    QVector<ChapterRef> refs;
    refs.reserve(m_book->chapters().size());
    for (const Chapter &ch : m_book->chapters())
        refs.append(ChapterRef{ch.path, ch.label});
    m_chapterTexts = buildChapterTexts(*m_book, refs);
    m_chapterTextsReady = true;
}

void MainWindow::runSearch()
{
    if (!m_book) {
        m_searchResults->clear();
        return;
    }
    const QString query = m_searchInput->text();
    m_searchResults->clear();
    if (query.trimmed().isEmpty()) {
        updateSidebarMode();
        return;
    }

    ensureChapterTexts();
    const QVector<SearchHit> hits = searchChapters(m_chapterTexts, query);

    QString lastChapter;
    for (const SearchHit &hit : hits) {
        if (hit.chapterPath != lastChapter) {
            auto *header = new QListWidgetItem(hit.chapterLabel, m_searchResults);
            header->setFlags(Qt::NoItemFlags);
            QFont hf = header->font();
            hf.setBold(true);
            header->setFont(hf);
            lastChapter = hit.chapterPath;
        }
        auto *item = new QListWidgetItem(
            QStringLiteral("…%1%2%3…").arg(hit.snippetBefore, hit.snippetMatch, hit.snippetAfter),
            m_searchResults);
        item->setData(Qt::UserRole, hit.chapterPath);
    }

    if (hits.isEmpty())
        new QListWidgetItem(QStringLiteral("該当なし"), m_searchResults);
    updateSidebarMode();
}

void MainWindow::onSearchResultActivated(QListWidgetItem *item)
{
    if (!item || !m_book)
        return;
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty())
        return;
    const int index = m_book->chapterIndexForPath(path);
    if (index < 0)
        return;
    const QString query = m_searchInput->text().trimmed();
    if (index != m_currentChapter) {
        m_pendingFind = query;
        displayChapter(index);
    } else {
        m_view->findText(query);
    }
}

// --- highlights ------------------------------------------------------------

void MainWindow::onWebSelection(int block, const QString &side, const QString &lang, int offset,
                                int length, const QString &text)
{
    if (!m_book || m_currentChapter < 0 || text.trimmed().isEmpty() || length <= 0)
        return;
    const HighlightSide hside = highlightSideFromString(side);
    // A translation-side highlight belongs to the language currently displayed.
    const QString effLang =
        hside == HighlightSide::Translation ? (lang.isEmpty() ? m_trTarget : lang) : QString();

    QMenu menu;
    const HighlightColor colors[] = {HighlightColor::Yellow, HighlightColor::Blue,
                                     HighlightColor::Pink, HighlightColor::Orange,
                                     HighlightColor::Green, HighlightColor::Purple};
    QHash<QAction *, HighlightColor> map;
    for (HighlightColor c : colors)
        map.insert(menu.addAction(swatchIcon(c), highlightLabel(c)), c);
    menu.addSeparator();
    QAction *withNote = menu.addAction(QStringLiteral("＋ ノート付きで追加…"));
    QAction *translateAction = menu.addAction(QStringLiteral("🌐 翻訳"));
    QAction *summaryAction = menu.addAction(QStringLiteral("要約"));
    QAction *copyAction = menu.addAction(QStringLiteral("コピー"));
    QAction *webSearchAction = menu.addAction(QStringLiteral("Web で検索"));

    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen)
        return;
    if (chosen == copyAction) {
        QGuiApplication::clipboard()->setText(text);
    } else if (chosen == webSearchAction) {
        openWebSearch(text);
    } else if (chosen == translateAction) {
        translateSelection(text);
    } else if (chosen == summaryAction) {
        summarizeSelection(text);
    } else if (chosen == withNote) {
        bool ok = false;
        const QString note = promptNoteText(QStringLiteral("ノートを入力:"), QString(), &ok);
        if (ok)
            createHighlight(HighlightColor::Yellow, block, hside, effLang, offset, length, text, note);
    } else if (map.contains(chosen)) {
        createHighlight(map.value(chosen), block, hside, effLang, offset, length, text);
    }
}

QString MainWindow::chapterHighlightsJson() const
{
    QJsonArray arr;
    if (m_book && m_currentChapter >= 0) {
        const QString path = m_book->chapters().at(m_currentChapter).path;
        for (const Highlight &h : highlight_store::byChapter(m_highlights, path)) {
            QJsonObject o;
            o[QStringLiteral("id")] = h.id;
            o[QStringLiteral("block")] = h.block;
            o[QStringLiteral("side")] = toString(h.side);
            o[QStringLiteral("lang")] = h.lang;
            o[QStringLiteral("offset")] = h.offset;
            o[QStringLiteral("length")] = h.length;
            o[QStringLiteral("color")] = toString(h.color);
            if (!h.note.isEmpty())
                o[QStringLiteral("note")] = h.note;
            arr.append(o);
        }
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void MainWindow::pushHighlightsToView()
{
    if (!m_bridge)
        return;
    m_bridge->setHighlightsJson(chapterHighlightsJson());
    m_bridge->notifyChanged();
}

void MainWindow::createHighlight(HighlightColor color, int block, HighlightSide side,
                                 const QString &lang, int offset, int length,
                                 const QString &selectedText, const QString &note)
{
    if (!m_book || m_currentChapter < 0)
        return;
    const QString text = selectedText;
    if (text.trimmed().isEmpty() || length <= 0)
        return;

    Highlight h;
    h.id = generateHighlightId();
    h.chapter = m_book->chapters().at(m_currentChapter).path;
    h.block = block;
    h.side = side;
    if (side == HighlightSide::Translation)
        h.lang = lang;
    h.offset = offset;
    h.length = length;
    h.text = text;
    h.color = color;
    h.note = note.trimmed();
    h.source = HighlightSource::User;
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    h.createdAt = now;
    h.updatedAt = now;

    highlight_store::upsert(m_highlights, h);
    afterHighlightsMutated();
}

void MainWindow::removeHighlightById(const QString &id)
{
    highlight_store::remove(m_highlights, id);
    afterHighlightsMutated();
}

Highlight *MainWindow::findHighlight(const QString &id)
{
    for (Highlight &h : m_highlights)
        if (h.id == id)
            return &h;
    return nullptr;
}

void MainWindow::setHighlightColor(const QString &id, HighlightColor color)
{
    Highlight *h = findHighlight(id);
    if (!h)
        return;
    h->color = color;
    h->updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    afterHighlightsMutated();
}

QString MainWindow::promptNoteText(const QString &label, const QString &initial, bool *ok)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("ノート"));
    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(label, &dlg));
    QTextEdit *edit = new QTextEdit(&dlg);
    edit->setAcceptRichText(false);
    edit->setLineWrapMode(QTextEdit::WidgetWidth); // wrap to the editor width
    edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    edit->setPlainText(initial);
    edit->setMinimumSize(440, 180);
    lay->addWidget(edit);
    QDialogButtonBox *bb =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    edit->setFocus();
    const bool accepted = dlg.exec() == QDialog::Accepted;
    if (ok)
        *ok = accepted;
    return accepted ? edit->toPlainText() : QString();
}

void MainWindow::editHighlightNote(const QString &id)
{
    Highlight *h = findHighlight(id);
    if (!h)
        return;
    bool ok = false;
    const QString note = promptNoteText(QStringLiteral("ノートを編集:"), h->note, &ok);
    if (!ok)
        return;
    h->note = note.trimmed();
    h->updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    afterHighlightsMutated();
}

void MainWindow::onMarkClicked(const QString &id)
{
    Highlight *h = findHighlight(id);
    if (!h)
        return;

    QMenu menu;
    QAction *copyAction = menu.addAction(QStringLiteral("コピー"));
    QAction *webSearchAction = menu.addAction(QStringLiteral("Web で検索"));
    menu.addSeparator();
    QMenu *colorMenu = menu.addMenu(QStringLiteral("色を変更"));
    const HighlightColor colors[] = {HighlightColor::Yellow, HighlightColor::Blue,
                                     HighlightColor::Pink, HighlightColor::Orange,
                                     HighlightColor::Green, HighlightColor::Purple};
    QHash<QAction *, HighlightColor> map;
    for (HighlightColor c : colors)
        map.insert(colorMenu->addAction(swatchIcon(c), highlightLabel(c)), c);
    QAction *noteAction = menu.addAction(h->note.isEmpty() ? QStringLiteral("ノートを追加…")
                                                           : QStringLiteral("ノートを編集…"));
    menu.addSeparator();
    QAction *deleteAction = menu.addAction(QStringLiteral("削除"));

    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen)
        return;
    if (chosen == copyAction)
        QGuiApplication::clipboard()->setText(h->text);
    else if (chosen == webSearchAction)
        openWebSearch(h->text);
    else if (chosen == noteAction)
        editHighlightNote(id);
    else if (chosen == deleteAction)
        removeHighlightById(id);
    else if (map.contains(chosen))
        setHighlightColor(id, map.value(chosen));
}

void MainWindow::afterHighlightsMutated()
{
    persistHighlights();
    renderHighlightsList();
    pushHighlightsToView();
}

void MainWindow::renderHighlightsList()
{
    m_highlightsList->clear();
    if (!m_book)
        return;

    QVector<Highlight> ordered = m_highlights;
    std::sort(ordered.begin(), ordered.end(), [this](const Highlight &a, const Highlight &b) {
        const int ai = m_book->chapterIndexForPath(a.chapter);
        const int bi = m_book->chapterIndexForPath(b.chapter);
        if (ai != bi)
            return ai < bi;
        if (a.block != b.block)
            return a.block < b.block;
        return a.offset < b.offset;
    });

    for (const Highlight &h : ordered) {
        QString snippet = h.text.simplified();
        if (snippet.size() > 80)
            snippet = snippet.left(80) + QStringLiteral("…");
        // Mark which side the highlight was made on (原文 / 訳文).
        const QString tag = h.side == HighlightSide::Translation ? QStringLiteral("訳")
                                                                 : QStringLiteral("原");
        QString label = QStringLiteral("［%1］%2").arg(tag, snippet);
        if (!h.note.isEmpty())
            label += QStringLiteral("\n📝 ") + h.note.simplified();
        auto *item = new QListWidgetItem(swatchIcon(h.color), label, m_highlightsList);
        item->setData(Qt::UserRole, h.id);
    }
}

void MainWindow::onHighlightActivated(QListWidgetItem *item)
{
    if (!item || !m_book)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    for (const Highlight &h : m_highlights) {
        if (h.id != id)
            continue;
        const int index = m_book->chapterIndexForPath(h.chapter);
        if (index < 0)
            return;
        // Scroll to the highlight's own mark (by id), not a text search — a text
        // search would also match a romanized term echoed in the translation.
        if (index != m_currentChapter) {
            m_pendingScrollId = id;
            displayChapter(index);
        } else if (m_bridge) {
            m_bridge->requestScrollToHighlight(id);
        }
        return;
    }
}

void MainWindow::persistHighlights()
{
    if (m_epubPath.isEmpty())
        return;
    BookRef ref{m_bookId, m_book ? m_book->title() : QString(),
                m_book ? m_book->author() : QString()};
    highlight_store::save(m_epubPath, ref, m_highlights);
}

// --- import / export -------------------------------------------------------

void MainWindow::exportHighlightsMarkdown()
{
    if (!m_book)
        return;
    if (m_highlights.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("書き出すハイライトがありません。"));
        return;
    }

    QVector<markdown::ChapterLabel> labels;
    for (const Chapter &ch : m_book->chapters())
        labels.append({ch.path, ch.label});

    BookRef ref{m_bookId, m_book->title(), m_book->author()};
    const QString exportedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString md = markdown::exportMarkdown(ref, m_highlights, labels, exportedAt);

    QString suggested = (m_book->title().isEmpty() ? QStringLiteral("highlights") : m_book->title());
    suggested.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Markdown で書き出し"), suggested + QStringLiteral(".md"),
        QStringLiteral("Markdown (*.md)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(md.toUtf8());
}

void MainWindow::exportHighlightsJson()
{
    if (!m_book)
        return;
    if (m_highlights.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("書き出すハイライトがありません。"));
        return;
    }

    BookHighlightFile file;
    file.version = 1;
    file.book = BookRef{m_bookId, m_book->title(), m_book->author()};
    file.highlights = m_highlights;

    QString suggested = (m_book->title().isEmpty() ? QStringLiteral("highlights") : m_book->title());
    suggested.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("JSON で書き出し"), suggested + QStringLiteral(".json"),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(highlight_store::serializeFile(file));
}

void MainWindow::exportChapterAozora()
{
    if (!m_book || m_currentChapter < 0) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("先に章を開いてください。"));
        return;
    }
    const Chapter &chapter = m_book->chapters().at(m_currentChapter);
    const QString xhtml = aozora::exportChapter(*m_book, chapter);
    if (xhtml.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Spindle"),
                             QStringLiteral("章の書き出しに失敗しました。"));
        return;
    }

    QString suggested = chapter.label.isEmpty() ? QStringLiteral("chapter") : chapter.label;
    suggested.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("青空文庫 XHTML で書き出し"), suggested + QStringLiteral(".html"),
        QStringLiteral("XHTML (*.html *.xhtml)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(xhtml.toUtf8());
}

void MainWindow::exportTranslatedEpub(int mode)
{
    if (!m_book || m_epubPath.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("先に EPUB を開いてください。"));
        return;
    }
    const auto emode = mode == 1 ? translated_epub::Mode::Translation
                                 : translated_epub::Mode::Bilingual;
    const QString label = mode == 1 ? QStringLiteral("訳文") : QStringLiteral("対訳");
    const bool translationNeeded = !isBookLanguage(m_trTarget);

    // Translate any paragraphs not yet cached, filling the cache.
    const QStringList missing = translated_epub::collectMissing(*m_book, m_trCache);
    if (translationNeeded && !missing.isEmpty()) {
        QProgressDialog progress(
            QStringLiteral("未翻訳の段落を翻訳しています（%1 へ: %2）…")
                .arg(targetLanguageName(m_trTarget), m_trModel),
            QStringLiteral("キャンセル"), 0, missing.size(), this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);

        OllamaClient client;
        auto translateForExport = [&](const QString &text, QString *out) {
            QEventLoop loop;
            bool ok = false;
            QString result;
            auto conn = connect(&client, &OllamaClient::finished, &loop,
                                [&](int, bool o, const QString &r) {
                                    ok = o;
                                    result = r;
                                    loop.quit();
                                });
            client.translate(m_trEndpoint, m_trModel, targetLanguagePrompt(m_trTarget), text,
                             m_trGlossary.promptBlockForText(text));
            loop.exec();
            disconnect(conn);

            *out = result;
            return ok;
        };

        int done = 0;
        for (const QString &text : missing) {
            if (progress.wasCanceled())
                return;
            progress.setValue(done);

            QString out;
            if (!translateForExport(text, &out)) {
                const QString targetName = targetLanguageName(m_trTarget);
                const QString targetPrompt = targetLanguagePrompt(m_trTarget);
                const QString glossary = m_trGlossary.promptBlockForText(text);
                const QString diagnosticPath = translationDiagnosticPath(m_epubPath);
                QString diagnosticError;
                QString diagnosticMessage;
                if (writeTranslationDiagnostic(
                        diagnosticPath, &diagnosticError, m_epubPath, m_book->title(),
                        m_book->language(), m_trEndpoint, m_trModel, targetName, m_trTarget,
                        targetPrompt, glossary, text, done + 1, missing.size(), out)) {
                    diagnosticMessage =
                        QStringLiteral("送信した全文を保存しました:\n%1").arg(diagnosticPath);
                } else if (!diagnosticError.isEmpty()) {
                    diagnosticMessage =
                        QStringLiteral("診断ファイルを保存できませんでした: %1").arg(diagnosticError);
                }
                QMessageBox::warning(
                    this, QStringLiteral("Spindle"),
                    translationExportFailureMessage(out, text, done + 1, missing.size(),
                                                    targetName, m_trTarget, m_trModel,
                                                    diagnosticMessage));
                m_trCache.flush();
                return;
            }
            m_trCache.put(text, out);
            ++done;
        }
        progress.setValue(missing.size());
        m_trCache.flush();
    }

    QString suggested = m_book->title().isEmpty() ? QStringLiteral("book") : m_book->title();
    suggested.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString outPath = QFileDialog::getSaveFileName(
        this, QStringLiteral("%1 EPUB を書き出し").arg(label),
        QStringLiteral("%1_%2.epub").arg(suggested, label), QStringLiteral("EPUB (*.epub)"));
    if (outPath.isEmpty())
        return;

    QString err;
    if (translated_epub::write(*m_book, m_epubPath, outPath, emode, m_trCache, &err)) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("%1 EPUB を書き出しました。").arg(label));
    } else {
        QMessageBox::warning(this, QStringLiteral("Spindle"),
                             QStringLiteral("書き出しに失敗しました:\n%1").arg(err));
    }
}

// --- translation -----------------------------------------------------------

// Primary language subtag, lowercased (e.g. "ja-JP" / "ja_JP" -> "ja").
static QString primaryLangSubtag(const QString &code)
{
    QString c = code.trimmed().toLower();
    const int dash = c.indexOf(QLatin1Char('-'));
    const int us = c.indexOf(QLatin1Char('_'));
    int cut = dash;
    if (us >= 0 && (cut < 0 || us < cut))
        cut = us;
    return cut >= 0 ? c.left(cut) : c;
}

bool MainWindow::isBookLanguage(const QString &targetCode) const
{
    if (!m_book)
        return false;
    const QString bl = m_book->language();
    if (bl.isEmpty() || targetCode.isEmpty())
        return false;
    return primaryLangSubtag(bl) == primaryLangSubtag(targetCode);
}

QString MainWindow::translateViewString() const
{
    // If the book is already in the target language, translation is a no-op:
    // always show the original.
    if (isBookLanguage(m_trTarget))
        return QStringLiteral("original");
    switch (m_translateView) {
    case TranslateView::Bilingual: return QStringLiteral("bilingual");
    case TranslateView::Translation: return QStringLiteral("translation");
    case TranslateView::Original: break;
    }
    return QStringLiteral("original");
}

void MainWindow::setTranslateView(int view)
{
    m_translateView = static_cast<TranslateView>(view);
    QSettings().setValue(QStringLiteral("translate/view"), static_cast<int>(m_translateView));
    ++m_trRunId; // start a fresh translation run for the new mode
    m_trQueue.clear();
    if (m_bridge) {
        m_bridge->setTranslateView(translateViewString());
        m_bridge->notifyTranslateViewChanged();
    }
}

void MainWindow::onBlocksReady(const QString &json)
{
    const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
    // Bump the run so any prior (possibly still in-flight) loop is superseded and
    // two onBlocksReady calls can't drive two interleaved queues at once.
    ++m_trRunId;
    const int run = m_trRunId;
    m_trQueue.clear();
    const bool force = m_trForce; // 再翻訳: redo every block, overwriting the cache
    m_trForce = false;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const int index = o.value(QStringLiteral("index")).toInt();
        const QString text = o.value(QStringLiteral("text")).toString();
        // Cache hit → apply instantly without calling Ollama (unless re-translating).
        if (!force) {
            const QString cached = m_trCache.lookup(text);
            if (!cached.isEmpty()) {
                if (m_bridge)
                    m_bridge->applyTranslation(index, cached, QString());
                continue;
            }
        }
        m_trQueue.append({index, text});
    }
    m_trCursor = 0;
    m_trInFlight = 0;
    m_trAnyOk = false;
    translateNext(run);
}

// Number of Ollama requests kept in flight at once.
static constexpr int kTranslateConcurrency = 2;

void MainWindow::translateNext(int run)
{
    if (run != m_trRunId)
        return; // superseded by a newer run
    // Top up to the concurrency limit, dispatching the next queued blocks.
    while (m_trInFlight < kTranslateConcurrency && m_trCursor < m_trQueue.size()) {
        const QPair<int, QString> item = m_trQueue.at(m_trCursor);
        ++m_trCursor;
        const int reqId = ++m_trReqSeq;
        m_trReqs.insert(reqId, {run, item.first, item.second});
        ++m_trInFlight;
        if (m_bridge)
            m_bridge->applyTranslation(item.first, QStringLiteral("翻訳中…"),
                                       QStringLiteral("pending"));
        m_ollama->translate(m_trEndpoint, m_trModel, targetLanguageName(m_trTarget), item.second,
                            m_trGlossary.promptBlockForText(item.second), reqId);
    }
}

void MainWindow::onOllamaFinished(int requestId, bool ok, const QString &result)
{
    const auto it = m_trReqs.find(requestId);
    if (it == m_trReqs.end())
        return; // unknown / already handled
    const TrRequest req = it.value();
    m_trReqs.erase(it);
    if (req.run != m_trRunId)
        return; // stale reply from a superseded run — drop, don't touch in-flight
    --m_trInFlight;

    if (ok) {
        m_trAnyOk = true;
        m_trCache.put(req.text, result); // key = this block's text (never the next's)
        m_trCacheSave->start();
        if (m_bridge)
            m_bridge->applyTranslation(req.index, result, QString());
    } else {
        if (m_bridge)
            m_bridge->applyTranslation(req.index,
                                       QStringLiteral("⚠ 翻訳に失敗しました: ") + result,
                                       QStringLiteral("error"));
        // A failure before any success usually means Ollama is unreachable —
        // stop dispatching rather than spamming every paragraph with the error.
        if (!m_trAnyOk) {
            m_trQueue.clear();
            return;
        }
    }
    translateNext(req.run); // top up the next request(s)
}

void MainWindow::translateSelection(const QString &text)
{
    const QString src = text.trimmed();
    if (src.isEmpty())
        return;
    showTranslatePopup(QStringLiteral("翻訳中…"));
    m_selectionOllama->translate(m_trEndpoint, m_trModel, targetLanguageName(m_trTarget), src,
                                 m_trGlossary.promptBlockForText(src));
}

void MainWindow::onSelectionTranslated(int requestId, bool ok, const QString &result)
{
    Q_UNUSED(requestId);
    if (!m_translatePopup || !m_translatePopup->isVisible())
        return;
    showTranslatePopup(ok ? result : QStringLiteral("⚠ 翻訳に失敗しました: ") + result);
}

void MainWindow::summarizeSelection(const QString &text)
{
    const QString src = text.trimmed();
    if (src.isEmpty())
        return;
    m_summarySaveable = false;
    m_summaryChapterPath.clear();
    m_summaryChapterTitle.clear();
    m_summaryTruncated = false;
    showSummaryDialog(QStringLiteral("選択範囲の要約 (%1)").arg(summaryDetailLabel()),
                      QStringLiteral("要約中…"));
    m_summaryOllama->summarize(m_trEndpoint, effectiveSummaryModel(),
                               targetLanguagePrompt(m_trTarget), src, summaryDetailInstruction(),
                               m_trGlossary.promptBlockForText(src));
}

void MainWindow::summarizeCurrentChapter()
{
    generateCurrentChapterSummary(false);
}

void MainWindow::regenerateCurrentChapterSummary()
{
    generateCurrentChapterSummary(true);
}

void MainWindow::generateCurrentChapterSummary(bool force)
{
    if (!m_book || m_currentChapter < 0) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("EPUB を開いてから要約してください。"));
        return;
    }

    const Chapter &chapter = m_book->chapters().at(m_currentChapter);
    if (!force) {
        const ChapterSummary saved =
            summary_store::find(summary_store::load(m_epubPath), chapter.path, m_trTarget,
                                summaryDetailKey());
        if (!saved.summaryMarkdown.isEmpty()) {
            m_summaryChapterPath = chapter.path;
            m_summaryChapterTitle = chapter.label;
            m_summarySaveable = true;
            m_summaryTruncated = false;
            showSummaryDialog(QStringLiteral("保存済みの章要約 (%1)").arg(summaryDetailLabel()),
                              saved.summaryMarkdown);
            if (m_summarySaveButton)
                m_summarySaveButton->setText(QStringLiteral("保存済み"));
            return;
        }
    }

    ensureChapterTexts();
    const QString chapterPath = chapter.path;
    QString text;
    for (const ChapterText &chapter : m_chapterTexts) {
        if (chapter.path == chapterPath) {
            text = chapter.normalizedBody.trimmed();
            break;
        }
    }
    if (text.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("この章には要約できる本文がありません。"));
        return;
    }

    static constexpr int kMaxSummaryInputChars = 60000;
    m_summarySaveable = false;
    m_summaryChapterPath = chapterPath;
    m_summaryChapterTitle = chapter.label;
    m_summaryTruncated = text.size() > kMaxSummaryInputChars;
    if (m_summaryTruncated)
        text = text.left(kMaxSummaryInputChars);

    showSummaryDialog(QStringLiteral("章の要約 (%1)").arg(summaryDetailLabel()),
                      m_summaryTruncated
                          ? QStringLiteral("要約中…（章が長いため先頭部分を要約します）")
                          : QStringLiteral("要約中…"));
    m_summaryOllama->summarize(m_trEndpoint, effectiveSummaryModel(),
                               targetLanguagePrompt(m_trTarget), text, summaryDetailInstruction(),
                               m_trGlossary.promptBlockForText(text));
}

void MainWindow::openSavedCurrentChapterSummary()
{
    if (!m_book || m_currentChapter < 0) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("EPUB を開いてから保存済み要約を選択してください。"));
        return;
    }

    const Chapter &chapter = m_book->chapters().at(m_currentChapter);
    const ChapterSummary summary =
        summary_store::find(summary_store::load(m_epubPath), chapter.path, m_trTarget,
                            summaryDetailKey());
    if (summary.summaryMarkdown.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("Spindle"),
            QStringLiteral("現在の章には、%1 / %2 の保存済み要約がありません。")
                .arg(targetLanguageName(m_trTarget), summaryDetailLabel()));
        return;
    }

    m_summaryChapterPath = chapter.path;
    m_summaryChapterTitle = chapter.label;
    m_summarySaveable = true;
    m_summaryTruncated = false;
    showSummaryDialog(QStringLiteral("保存済みの章要約 (%1)").arg(summaryDetailLabel()),
                      summary.summaryMarkdown);
    if (m_summarySaveButton)
        m_summarySaveButton->setText(QStringLiteral("保存済み"));
}

void MainWindow::setSummaryDetail(int detail)
{
    const int bounded = qBound(0, detail, 2);
    m_summaryDetail = static_cast<SummaryDetail>(bounded);
    QSettings().setValue(QStringLiteral("summary/detail"), bounded);
}

QString MainWindow::summaryDetailLabel() const
{
    switch (m_summaryDetail) {
    case SummaryDetail::Brief: return QStringLiteral("短め");
    case SummaryDetail::Standard: return QStringLiteral("標準");
    case SummaryDetail::Detailed: return QStringLiteral("詳しく");
    }
    return QStringLiteral("標準");
}

QString MainWindow::summaryDetailKey() const
{
    switch (m_summaryDetail) {
    case SummaryDetail::Brief: return QStringLiteral("brief");
    case SummaryDetail::Standard: return QStringLiteral("standard");
    case SummaryDetail::Detailed: return QStringLiteral("detailed");
    }
    return QStringLiteral("standard");
}

QString MainWindow::summaryDetailInstruction() const
{
    switch (m_summaryDetail) {
    case SummaryDetail::Brief:
        return QStringLiteral(
            "Make it brief: 3 to 5 compact bullet points, focusing only on the main points.");
    case SummaryDetail::Standard:
        return QStringLiteral(
            "Use a balanced level of detail: about 6 to 10 bullet points covering the main "
            "claims, events, and relationships.");
    case SummaryDetail::Detailed:
        return QStringLiteral(
            "Make it detailed: preserve important named entities, chronology, cause and "
            "effect, and notable nuance. Use section headings and bullet points when helpful.");
    }
    return {};
}

QString MainWindow::effectiveSummaryModel() const
{
    return m_summaryModel.isEmpty() ? m_trModel : m_summaryModel;
}

void MainWindow::saveCurrentChapterSummary()
{
    if (!m_summarySaveable || m_epubPath.isEmpty() || m_summaryChapterPath.isEmpty()
        || m_summaryMarkdown.trimmed().isEmpty()) {
        return;
    }

    QVector<ChapterSummary> summaries = summary_store::load(m_epubPath);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    ChapterSummary summary;
    summary.chapter = m_summaryChapterPath;
    summary.chapterTitle = m_summaryChapterTitle;
    summary.targetLang = m_trTarget;
    summary.detail = summaryDetailKey();
    summary.model = effectiveSummaryModel();
    summary.summaryMarkdown = m_summaryMarkdown;
    summary.createdAt = now;
    summary.updatedAt = now;

    summary_store::upsert(summaries, summary);
    summary_store::save(m_epubPath, summaries);

    if (m_summarySaveButton)
        m_summarySaveButton->setText(QStringLiteral("保存済み"));
}

void MainWindow::translateCurrentSummary()
{
    const QString src = m_summaryMarkdown.trimmed();
    if (src.isEmpty())
        return;

    m_summaryPreTranslateTitle =
        m_summaryDialog ? m_summaryDialog->windowTitle() : QStringLiteral("要約");
    m_summarySaveable = false;
    showSummaryDialog(QStringLiteral("要約を翻訳中 (%1)").arg(targetLanguageName(m_trTarget)),
                      QStringLiteral("翻訳中…"));
    m_summaryOllama->translate(m_trEndpoint, effectiveSummaryModel(),
                               targetLanguagePrompt(m_trTarget), src,
                               m_trGlossary.promptBlockForText(src), kSummaryTranslateRequestId);
}

void MainWindow::onSummaryFinished(int requestId, bool ok, const QString &result)
{
    if (!m_summaryDialog || !m_summaryDialog->isVisible())
        return;
    if (!ok) {
        showSummaryDialog(m_summaryDialog->windowTitle(),
                          requestId == kSummaryTranslateRequestId
                              ? QStringLiteral("⚠ 翻訳に失敗しました: ") + result
                              : QStringLiteral("⚠ 要約に失敗しました: ") + result);
        return;
    }
    if (requestId == kSummaryTranslateRequestId) {
        m_summarySaveable = !m_summaryChapterPath.isEmpty();
        const QString title = m_summaryPreTranslateTitle.isEmpty()
                                  ? QStringLiteral("要約")
                                  : m_summaryPreTranslateTitle;
        showSummaryDialog(title, result);
        return;
    }

    const QString prefix =
        m_summaryTruncated ? QStringLiteral("※ 長いため先頭部分から要約しました。\n\n") : QString();
    m_summarySaveable = !m_summaryChapterPath.isEmpty();
    showSummaryDialog(m_summaryDialog->windowTitle(), prefix + result);
}

void MainWindow::showSummaryDialog(const QString &title, const QString &text)
{
    if (!m_summaryDialog) {
        m_summaryDialog = new QDialog(this);
        m_summaryDialog->setAttribute(Qt::WA_DeleteOnClose);
        m_summaryDialog->resize(680, 520);

        QVBoxLayout *layout = new QVBoxLayout(m_summaryDialog);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(10);

        m_summaryText = new QTextEdit(m_summaryDialog);
        m_summaryText->setReadOnly(true);
        m_summaryText->setAcceptRichText(true);
        m_summaryText->setLineWrapMode(QTextEdit::WidgetWidth);
        m_summaryText->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                               Qt::TextSelectableByKeyboard);
        layout->addWidget(m_summaryText);

        QDialogButtonBox *buttons = new QDialogButtonBox(m_summaryDialog);
        QPushButton *copyButton = buttons->addButton(QStringLiteral("コピー"),
                                                     QDialogButtonBox::ActionRole);
        m_summarySaveButton = buttons->addButton(QStringLiteral("保存"),
                                                 QDialogButtonBox::ActionRole);
        m_summarySaveButton->setEnabled(false);
        m_summaryTranslateButton = buttons->addButton(QStringLiteral("翻訳"),
                                                      QDialogButtonBox::ActionRole);
        m_summaryTranslateButton->setEnabled(false);
        m_summaryRegenerateButton = buttons->addButton(QStringLiteral("再作成"),
                                                       QDialogButtonBox::ActionRole);
        m_summaryRegenerateButton->setEnabled(false);
        buttons->addButton(QDialogButtonBox::Close);
        connect(copyButton, &QPushButton::clicked, this, [this] {
            QGuiApplication::clipboard()->setText(m_summaryMarkdown);
        });
        connect(m_summarySaveButton, &QPushButton::clicked, this,
                &MainWindow::saveCurrentChapterSummary);
        connect(m_summaryTranslateButton, &QPushButton::clicked, this,
                &MainWindow::translateCurrentSummary);
        connect(m_summaryRegenerateButton, &QPushButton::clicked, this,
                &MainWindow::regenerateCurrentChapterSummary);
        connect(buttons, &QDialogButtonBox::rejected, m_summaryDialog, &QDialog::close);
        layout->addWidget(buttons);

        connect(m_summaryDialog, &QObject::destroyed, this, [this] {
            m_summaryDialog = nullptr;
            m_summaryText = nullptr;
            m_summarySaveButton = nullptr;
            m_summaryTranslateButton = nullptr;
            m_summaryRegenerateButton = nullptr;
        });
    }

    m_summaryDialog->setWindowTitle(title);
    m_summaryMarkdown = text;
    if (m_summarySaveButton) {
        m_summarySaveButton->setText(QStringLiteral("保存"));
        m_summarySaveButton->setEnabled(m_summarySaveable && !m_summaryMarkdown.trimmed().isEmpty());
    }
    if (m_summaryRegenerateButton)
        m_summaryRegenerateButton->setEnabled(!m_summaryChapterPath.isEmpty());
    if (m_summaryTranslateButton)
        m_summaryTranslateButton->setEnabled(!m_summaryMarkdown.trimmed().isEmpty()
                                             && !m_summaryMarkdown.contains(
                                                 QStringLiteral("要約中"))
                                             && m_summaryMarkdown != QStringLiteral("翻訳中…")
                                             && !m_summaryMarkdown.startsWith(
                                                 QStringLiteral("⚠")));
    if (m_summaryText) {
        m_summaryText->setMarkdown(text);
        m_summaryText->moveCursor(QTextCursor::Start);
    }
    m_summaryDialog->show();
    m_summaryDialog->raise();
    m_summaryDialog->activateWindow();
}

void MainWindow::showTranslatePopup(const QString &text)
{
    if (!m_translatePopup) {
        m_translatePopup = new TranslatePopup();
        m_translatePopup->setWordWrap(true);
        m_translatePopup->setMargin(12);
        m_translatePopup->setMaximumWidth(440);
        m_translatePopup->setFocusPolicy(Qt::StrongFocus);
        m_translatePopup->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_translatePopup->setStyleSheet(
            QStringLiteral("QLabel{background:#2b2b2e;color:#eaeaea;border:1px solid #555;"
                           "border-radius:8px;font-size:14px;}"));
    }
    m_translatePopup->setText(text);
    m_translatePopup->adjustSize();
    m_translatePopup->move(QCursor::pos() + QPoint(8, 14));
    m_translatePopup->show();
    m_translatePopup->setFocus(); // ensure it receives the Escape key
}

void MainWindow::openSummarySettingsDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("要約設定 (Ollama)"));
    QFormLayout *form = new QFormLayout(&dialog);

    QLineEdit *modelEdit = new QLineEdit(m_summaryModel, &dialog);
    modelEdit->setPlaceholderText(m_trModel);
    form->addRow(QStringLiteral("要約モデル"), modelEdit);

    QLabel *hint = new QLabel(QStringLiteral("未入力の場合は翻訳モデルを使用します。"), &dialog);
    hint->setWordWrap(true);
    form->addRow(hint);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("閉じる"));
    form->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    connect(&dialog, &QDialog::accepted, this, [&]() {
        const QString model = modelEdit->text().trimmed();
        m_summaryModel = model;
        QSettings settings;
        if (m_summaryModel.isEmpty()) {
            settings.remove(QStringLiteral("summary/model"));
        } else {
            settings.setValue(QStringLiteral("summary/model"), m_summaryModel);
        }
    });

    dialog.exec();
}

void MainWindow::openTranslateDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("対訳翻訳 (Ollama)"));
    QFormLayout *form = new QFormLayout(&dialog);

    QComboBox *modeBox = new QComboBox(&dialog);
    modeBox->addItems({QStringLiteral("原文"), QStringLiteral("対訳併記"), QStringLiteral("訳文")});
    modeBox->setCurrentIndex(static_cast<int>(m_translateView));

    QComboBox *targetBox = new QComboBox(&dialog);
    int targetIdx = 0;
    for (int i = 0; i < static_cast<int>(std::size(kLangs)); ++i) {
        targetBox->addItem(QString::fromUtf8(kLangs[i].label), QString::fromUtf8(kLangs[i].code));
        if (m_trTarget == QLatin1String(kLangs[i].code))
            targetIdx = i;
    }
    targetBox->setCurrentIndex(targetIdx);

    QLineEdit *modelEdit = new QLineEdit(m_trModel, &dialog);
    QLineEdit *endpointEdit = new QLineEdit(m_trEndpoint, &dialog);

    QComboBox *colorBox = new QComboBox(&dialog);
    colorBox->addItem(QStringLiteral("なし（原文と同じ）"), QString());
    colorBox->addItem(QStringLiteral("藍 / 青"), QStringLiteral("blue"));
    colorBox->addItem(QStringLiteral("ティール"), QStringLiteral("teal"));
    colorBox->addItem(QStringLiteral("グレー"), QStringLiteral("gray"));
    colorBox->addItem(QStringLiteral("緑"), QStringLiteral("green"));
    const int customIdx = colorBox->count();
    colorBox->addItem(QStringLiteral("カスタム…"), QStringLiteral("custom"));
    if (m_trColor.startsWith(QLatin1Char('#'))) {
        colorBox->setItemText(customIdx, QStringLiteral("カスタム (%1)").arg(m_trColor));
        colorBox->setCurrentIndex(customIdx);
    } else {
        const int i = colorBox->findData(m_trColor);
        colorBox->setCurrentIndex(i >= 0 ? i : 0);
    }

    form->addRow(QStringLiteral("表示モード"), modeBox);
    form->addRow(QStringLiteral("翻訳先"), targetBox);
    form->addRow(QStringLiteral("翻訳モデル"), modelEdit);
    form->addRow(QStringLiteral("エンドポイント"), endpointEdit);
    form->addRow(QStringLiteral("訳文の色"), colorBox);

    QDialogButtonBox *buttons = new QDialogButtonBox(&dialog);
    QPushButton *retranslate = buttons->addButton(QStringLiteral("再翻訳"),
                                                  QDialogButtonBox::AcceptRole);
    buttons->addButton(QStringLiteral("閉じる"), QDialogButtonBox::RejectRole);
    form->addRow(buttons);
    QLabel *hint = new QLabel(QStringLiteral("ローカルの Ollama が起動している必要があります。"),
                              &dialog);
    hint->setWordWrap(true);
    form->addRow(hint);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // Mode changes apply live.
    connect(modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) { setTranslateView(idx); });

    // Translation color applies live (no re-translation needed).
    connect(colorBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, colorBox, customIdx](int i) {
                const QString key = colorBox->itemData(i).toString();
                if (key == QLatin1String("custom")) {
                    const QColor init = m_trColor.startsWith(QLatin1Char('#'))
                                            ? QColor(m_trColor)
                                            : QColor(QStringLiteral("#3a5f8a"));
                    const QColor c =
                        QColorDialog::getColor(init, this, QStringLiteral("訳文の色"));
                    if (!c.isValid())
                        return; // cancelled — keep the current color
                    m_trColor = c.name();
                    colorBox->setItemText(customIdx, QStringLiteral("カスタム (%1)").arg(m_trColor));
                } else {
                    m_trColor = key;
                }
                QSettings().setValue(QStringLiteral("translate/color"), m_trColor);
                injectViewStyle();
            });

    // When the target equals the book's own language, translation is a no-op:
    // lock the mode to 原文 and disable it. Re-evaluated when the target changes.
    auto updateModeForLang = [this, modeBox, targetBox]() {
        const bool same = isBookLanguage(targetBox->currentData().toString());
        modeBox->setEnabled(!same);
        QSignalBlocker block(modeBox);
        modeBox->setCurrentIndex(same ? 0 : static_cast<int>(m_translateView));
        modeBox->setToolTip(same ? QStringLiteral("本の言語と翻訳先が同じため、原文表示のみです")
                                 : QString());
    };
    updateModeForLang();
    connect(targetBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [updateModeForLang](int) { updateModeForLang(); });

    connect(&dialog, &QDialog::accepted, this, [&]() {
        const QString newTarget = targetBox->currentData().toString();
        m_trModel = modelEdit->text().trimmed().isEmpty() ? QStringLiteral("qwen2.5")
                                                          : modelEdit->text().trimmed();
        QString ep = endpointEdit->text().trimmed();
        while (ep.endsWith(QLatin1Char('/')))
            ep.chop(1);
        m_trEndpoint = ep.isEmpty() ? QStringLiteral("http://localhost:11434") : ep;
        if (newTarget != m_trTarget) {
            m_trTarget = newTarget;
            m_trCache.load(m_epubPath, m_trTarget); // switch to the new language's cache
            m_trGlossary.load(m_epubPath, m_trTarget);
        }
        QSettings settings;
        settings.setValue(QStringLiteral("translate/target"), m_trTarget);
        settings.setValue(QStringLiteral("translate/model"), m_trModel);
        settings.setValue(QStringLiteral("translate/endpoint"), m_trEndpoint);
        // 再翻訳 normally upgrades 原文 → 対訳, but not when the book is already in
        // the target language (translation would be a no-op).
        if (m_translateView == TranslateView::Original && !isBookLanguage(m_trTarget))
            m_translateView = TranslateView::Bilingual;
        settings.setValue(QStringLiteral("translate/view"), static_cast<int>(m_translateView));
        m_trForce = true; // 再翻訳: redo the translation rather than reusing the cache
        if (m_currentChapter >= 0)
            displayChapter(m_currentChapter); // reload clean, then re-translate
    });
    Q_UNUSED(retranslate);

    dialog.exec();
}

void MainWindow::importKindleNotebook()
{
    if (!m_book) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("先に EPUB を開いてください。"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Kindle ノートを読み込み"), QString(),
        QStringLiteral("Kindle Notebook (*.html *.htm)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Spindle"),
                             QStringLiteral("ファイルを開けませんでした。"));
        return;
    }
    const kindle::KindleNotebook notebook =
        kindle::parseKindleNotebook(QString::fromUtf8(f.readAll()));

    ensureChapterTexts();
    const matcher::MatchOutcome outcome = matcher::matchEntries(notebook.entries, m_chapterTexts);

    int existingKindle = 0;
    for (const Highlight &h : m_highlights)
        if (h.source == HighlightSource::Kindle)
            ++existingKindle;

    bool replace = false;
    if (existingKindle > 0) {
        const QMessageBox::StandardButton btn = QMessageBox::question(
            this, QStringLiteral("Kindle ハイライト"),
            QStringLiteral("既に %1 件の Kindle 由来ハイライトがあります。\n\n"
                           "「Yes」: 既存の Kindle ハイライトを削除して今回の %2 件で置き換え\n"
                           "「No」: 既存を残し、重複しない新規分のみ追加")
                .arg(existingKindle)
                .arg(outcome.matches.size()),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
        if (btn == QMessageBox::Cancel)
            return;
        replace = (btn == QMessageBox::Yes);
    }

    if (replace) {
        QVector<Highlight> kept;
        for (const Highlight &h : m_highlights)
            if (h.source != HighlightSource::Kindle)
                kept.append(h);
        m_highlights = kept;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    int added = 0, skipped = 0;
    for (const matcher::MatchResult &m : outcome.matches) {
        bool exists = false;
        for (const Highlight &h : m_highlights) {
            if (h.source == HighlightSource::Kindle && h.chapter == m.chapterPath
                && h.block == m.block && h.offset == m.offset && h.length == m.length) {
                exists = true;
                break;
            }
        }
        if (exists) {
            ++skipped;
            continue;
        }
        Highlight h;
        h.id = generateHighlightId();
        h.chapter = m.chapterPath;
        h.block = m.block;
        h.side = HighlightSide::Original;
        h.offset = m.offset;
        h.length = m.length;
        h.text = m.matchedText;
        h.color = m.entry.color;
        h.note = m.entry.note;
        h.source = HighlightSource::Kindle;
        KindleMeta meta;
        meta.location = m.entry.location;
        meta.page = m.entry.page;
        meta.chapterTitle = m.entry.chapterTitle;
        meta.partTitle = m.entry.partTitle;
        if (!meta.isEmpty())
            h.kindle = meta;
        h.createdAt = now;
        h.updatedAt = now;
        m_highlights.append(h);
        ++added;
    }

    afterHighlightsMutated();

    QMessageBox::information(
        this, QStringLiteral("Spindle"),
        QStringLiteral("Kindle ノート読み込み完了\nエントリ: %1 / マッチ: %2 (失敗 %3)\n"
                       "新規追加: %4 / 重複スキップ: %5%6")
            .arg(notebook.entries.size())
            .arg(outcome.matches.size())
            .arg(outcome.failures.size())
            .arg(added)
            .arg(skipped)
            .arg(replace ? QStringLiteral("\n(既存の Kindle ハイライトを置き換えました)") : QString()));
}

void MainWindow::importHighlights()
{
    if (!m_book) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("先に EPUB を開いてください。"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("ハイライトを読み込み"), QString(),
        QStringLiteral("ハイライト (*.md *.markdown *.json)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("Spindle"),
                             QStringLiteral("ファイルを開けませんでした。"));
        return;
    }
    const QByteArray bytes = f.readAll();

    QVector<Highlight> imported;
    if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        bool ok = false;
        imported = highlight_store::parseFile(bytes, &ok).highlights;
        if (!ok && imported.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Spindle"),
                                 QStringLiteral("対応していない JSON 形式です。"));
            return;
        }
    } else {
        imported = markdown::parseMarkdown(QString::fromUtf8(bytes)).highlights;
    }

    if (imported.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Spindle"),
                                 QStringLiteral("読み込めるハイライトがありませんでした。"));
        return;
    }

    for (const Highlight &h : imported)
        highlight_store::upsert(m_highlights, h);
    afterHighlightsMutated();

    QMessageBox::information(
        this, QStringLiteral("Spindle"),
        QStringLiteral("%1 件のハイライトを読み込みました。").arg(imported.size()));
}

// --- sidebar mode ----------------------------------------------------------

void MainWindow::showSidebarTab(int tab)
{
    m_sidebarTab = tab;
    m_tabToc->setChecked(tab == 0);
    m_tabHighlights->setChecked(tab == 1);
    m_tabRecent->setChecked(tab == 3);
    if (m_searchInput->text().trimmed().isEmpty())
        m_sidebarStack->setCurrentIndex(tab);
}

void MainWindow::updateSidebarMode()
{
    const bool searching = !m_searchInput->text().trimmed().isEmpty();
    m_sidebarStack->setCurrentIndex(searching ? 2 : m_sidebarTab);
}

// --- events ----------------------------------------------------------------

bool MainWindow::mimeHasEpub(const QMimeData *mime)
{
    if (!mime || !mime->hasUrls())
        return false;
    for (const QUrl &url : mime->urls())
        if (url.toLocalFile().endsWith(QStringLiteral(".epub"), Qt::CaseInsensitive))
            return true;
    return false;
}

void MainWindow::openEpubsFromMime(const QMimeData *mime)
{
    if (!mime)
        return;
    // The first EPUB lands in this window if it has no book yet; subsequent ones
    // (and any when a book is already open) each open in a new window.
    for (const QUrl &url : mime->urls()) {
        const QString local = url.toLocalFile();
        if (local.endsWith(QStringLiteral(".epub"), Qt::CaseInsensitive))
            openEpubSmart(local);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (mimeHasEpub(event->mimeData()))
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (mimeHasEpub(event->mimeData())) {
        openEpubsFromMime(event->mimeData());
        event->acceptProposedAction();
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    switch (event->type()) {
    case QEvent::ChildAdded:
        // Watch the render widget (and its descendants) as they appear so drops
        // over the page reach us instead of being swallowed by the web view.
        if (auto *ce = static_cast<QChildEvent *>(event)) {
            if (ce->child() && ce->child()->isWidgetType())
                ce->child()->installEventFilter(this);
        }
        break;
    case QEvent::DragEnter:
    case QEvent::DragMove:
        if (mimeHasEpub(static_cast<QDragMoveEvent *>(event)->mimeData())) {
            static_cast<QDragMoveEvent *>(event)->acceptProposedAction();
            return true;
        }
        break;
    case QEvent::Drop:
        if (mimeHasEpub(static_cast<QDropEvent *>(event)->mimeData())) {
            openEpubsFromMime(static_cast<QDropEvent *>(event)->mimeData());
            static_cast<QDropEvent *>(event)->acceptProposedAction();
            return true;
        }
        break;
    default:
        break;
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if ((event->modifiers() & Qt::ControlModifier || event->modifiers() & Qt::MetaModifier)
        && event->key() == Qt::Key_F) {
        m_searchInput->setFocus();
        m_searchInput->selectAll();
        return;
    }
    if (!m_book) {
        QMainWindow::keyPressEvent(event);
        return;
    }
    switch (event->key()) {
    case Qt::Key_Right:
    case Qt::Key_Space:
        nextChapter();
        break;
    case Qt::Key_Left:
        previousChapter();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}
