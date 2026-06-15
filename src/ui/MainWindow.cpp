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
#include "net/OllamaClient.h"
#include "web/Bridge.h"
#include "web/EpubSchemeHandler.h"

#include <QAction>
#include <QCursor>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QInputDialog>
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
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
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

struct Lang {
    const char *code;
    const char *name;  // handed to the model
    const char *label; // UI
};
const Lang kLangs[] = {{"ja", "Japanese", "日本語"}, {"en", "English", "English"},
                       {"zh", "Chinese", "中文"},     {"ko", "Korean", "한국어"},
                       {"fr", "French", "Français"},  {"de", "German", "Deutsch"},
                       {"es", "Spanish", "Español"}};

QString targetLanguageName(const QString &code)
{
    for (const Lang &l : kLangs)
        if (code == QLatin1String(l.code))
            return QString::fromUtf8(l.name);
    return code;
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

    buildUi();
    updateNavButtons();
    updateSidebarMode();
}

MainWindow::~MainWindow()
{
    m_trCache.flush();
    EpubSchemeHandler::instance()->unregisterBook(m_schemeId);
    --g_windowCount;
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

void MainWindow::buildUi()
{
    QAction *openAction = new QAction(QStringLiteral("EPUB を開く…"), this);
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenTriggered);
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("ファイル"));
    fileMenu->addAction(openAction);

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

    QMenu *trMenu = menuBar()->addMenu(QStringLiteral("翻訳"));
    trMenu->addAction(QStringLiteral("設定…"), this, &MainWindow::openTranslateDialog);
    trMenu->addSeparator();
    trMenu->addAction(QStringLiteral("対訳 EPUB を書き出し…"), this,
                      [this] { exportTranslatedEpub(0); });
    trMenu->addAction(QStringLiteral("訳文 EPUB を書き出し…"), this,
                      [this] { exportTranslatedEpub(1); });

    QToolBar *toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setMovable(false);
    QAction *sidebarAction = toolbar->addAction(QStringLiteral("☰ 目次"));
    sidebarAction->setCheckable(true);
    sidebarAction->setChecked(true);
    sidebarAction->setToolTip(QStringLiteral("目次サイドバーの表示/非表示"));
    connect(sidebarAction, &QAction::toggled, this,
            [this](bool on) { if (m_sidebar) m_sidebar->setVisible(on); });
    toolbar->addSeparator();
    m_prevAction = toolbar->addAction(QStringLiteral("‹ 前"));
    m_nextAction = toolbar->addAction(QStringLiteral("次 ›"));
    connect(m_prevAction, &QAction::triggered, this, &MainWindow::previousChapter);
    connect(m_nextAction, &QAction::triggered, this, &MainWindow::nextChapter);
    toolbar->addSeparator();
    toolbar->addAction(QStringLiteral("🌐 翻訳"), this, &MainWindow::openTranslateDialog);
    toolbar->addAction(QStringLiteral("A−"), this, &MainWindow::decreaseFont);
    toolbar->addAction(QStringLiteral("A+"), this, &MainWindow::increaseFont);
    toolbar->addAction(QStringLiteral("◐ テーマ"), this, &MainWindow::cycleTheme);
    QAction *xmlAction = toolbar->addAction(QStringLiteral("</> XML"));
    xmlAction->setCheckable(true);
    xmlAction->setToolTip(QStringLiteral("章の XHTML ソースを表示"));
    connect(xmlAction, &QAction::toggled, this, &MainWindow::toggleXmlView);

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
    m_tabToc->setCheckable(true);
    m_tabHighlights->setCheckable(true);
    m_tabToc->setChecked(true);
    connect(m_tabToc, &QPushButton::clicked, this, [this] { showSidebarTab(0); });
    connect(m_tabHighlights, &QPushButton::clicked, this, [this] { showSidebarTab(1); });
    tabsLayout->addWidget(m_tabToc);
    tabsLayout->addWidget(m_tabHighlights);

    m_toc = new QTreeWidget(sidebar);
    m_toc->setHeaderHidden(true);
    connect(m_toc, &QTreeWidget::itemClicked, this, &MainWindow::onTocItemActivated);

    m_highlightsList = new QListWidget(sidebar);
    m_highlightsList->setWordWrap(true);
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

    m_sidebarStack = new QStackedWidget(sidebar);
    m_sidebarStack->addWidget(m_toc);            // 0
    m_sidebarStack->addWidget(m_highlightsList); // 1
    m_sidebarStack->addWidget(m_searchResults);  // 2

    sideLayout->addWidget(m_titleLabel);
    sideLayout->addWidget(m_authorLabel);
    sideLayout->addWidget(m_searchInput);
    sideLayout->addWidget(tabs);
    sideLayout->addWidget(m_sidebarStack, 1);

    // --- web viewer ---
    // The epub:// scheme handler is installed once, globally, on the default
    // profile (see main()); this window just registers its book under m_schemeId.
    m_view = new QWebEngineView(this);
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

    QSettings settings;
    m_trTarget = settings.value(QStringLiteral("translate/target"), m_trTarget).toString();
    m_trModel = settings.value(QStringLiteral("translate/model"), m_trModel).toString();
    m_trEndpoint = settings.value(QStringLiteral("translate/endpoint"), m_trEndpoint).toString();

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
    m_highlights = highlight_store::load(m_bookId);
    m_trCache.load(m_bookId, m_trTarget);

    m_titleLabel->setText(m_book->title().isEmpty() ? QStringLiteral("(無題)") : m_book->title());
    m_authorLabel->setText(m_book->author());
    setWindowTitle(m_book->title().isEmpty()
                       ? QStringLiteral("Spindle")
                       : QStringLiteral("%1 — Spindle").arg(m_book->title()));

    populateToc();
    renderHighlightsList();
    displayChapter(0);
    showSidebarTab(0);
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
    if (m_bridge) {
        m_bridge->setHighlightsJson(chapterHighlightsJson());
        m_bridge->setTranslateView(translateViewString());
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
}

void MainWindow::applyZoom()
{
    if (m_view)
        m_view->setZoomFactor(m_fontSize / 100.0);
}

QColor MainWindow::themeBackground() const
{
    switch (m_theme) {
    case Theme::Light: return QColor("#ffffff");
    case Theme::Sepia: return QColor("#f4ecd8");
    case Theme::Dark:  return QColor("#1c1c1e");
    }
    return QColor("#ffffff");
}

void MainWindow::injectViewStyle()
{
    if (!m_view)
        return;
    QString css;
    switch (m_theme) {
    case Theme::Light:
        css = QStringLiteral("html,body{background:#ffffff !important;}");
        break;
    case Theme::Sepia:
        css = QStringLiteral("html,body{background:#f4ecd8 !important;} "
                             "body{color:#4b3a26 !important;}");
        break;
    case Theme::Dark:
        css = QStringLiteral("html,body{background:#1c1c1e !important;} "
                             "body{color:#d8d8da !important;} a{color:#6db3ff !important;}");
        break;
    }
    // Comfortable left/right reading margins (physical, so they apply equally to
    // horizontal and vertical-rl writing modes). box-sizing keeps them inside.
    css += QStringLiteral(" html{box-sizing:border-box;padding-left:6%;padding-right:6%;}");

    const QString js = QStringLiteral(
        "(function(){var s=document.getElementById('__spindle_theme');"
        "if(!s){s=document.createElement('style');s.id='__spindle_theme';"
        "document.documentElement.appendChild(s);}s.textContent=`%1`;})();")
        .arg(css);
    m_view->page()->runJavaScript(js);
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

void MainWindow::onWebSelection(int start, int end, const QString &text)
{
    if (!m_book || m_currentChapter < 0 || text.trimmed().isEmpty() || start >= end)
        return;

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

    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen)
        return;
    if (chosen == translateAction) {
        translateSelection(text);
    } else if (chosen == withNote) {
        bool ok = false;
        const QString note = QInputDialog::getMultiLineText(
            this, QStringLiteral("ノート"), QStringLiteral("ノートを入力:"), QString(), &ok);
        if (ok)
            createHighlight(HighlightColor::Yellow, start, end, text, note);
    } else if (map.contains(chosen)) {
        createHighlight(map.value(chosen), start, end, text);
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
            o[QStringLiteral("start")] = h.start;
            o[QStringLiteral("end")] = h.end;
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

void MainWindow::createHighlight(HighlightColor color, int start, int end,
                                 const QString &selectedText, const QString &note)
{
    if (!m_book || m_currentChapter < 0)
        return;
    const QString text = selectedText;
    if (text.trimmed().isEmpty() || start >= end)
        return;

    Highlight h;
    h.id = generateHighlightId();
    h.chapter = m_book->chapters().at(m_currentChapter).path;
    h.start = start;
    h.end = end;
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

void MainWindow::editHighlightNote(const QString &id)
{
    Highlight *h = findHighlight(id);
    if (!h)
        return;
    bool ok = false;
    const QString note = QInputDialog::getMultiLineText(
        this, QStringLiteral("ノート"), QStringLiteral("ノートを編集:"), h->note, &ok);
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
    if (chosen == noteAction)
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
        return a.start < b.start;
    });

    for (const Highlight &h : ordered) {
        QString snippet = h.text.simplified();
        if (snippet.size() > 80)
            snippet = snippet.left(80) + QStringLiteral("…");
        QString label = snippet;
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
        QString find = h.text.section(QChar('\n'), 0, 0).trimmed();
        if (find.size() > 40)
            find = find.left(40);
        if (index != m_currentChapter) {
            m_pendingFind = find;
            displayChapter(index);
        } else {
            m_view->findText(find);
        }
        return;
    }
}

void MainWindow::persistHighlights()
{
    if (m_bookId.isEmpty())
        return;
    BookRef ref{m_bookId, m_book ? m_book->title() : QString(),
                m_book ? m_book->author() : QString()};
    highlight_store::save(m_bookId, ref, m_highlights);
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

    // Translate any paragraphs not yet cached, filling the cache.
    const QStringList missing = translated_epub::collectMissing(*m_book, m_trCache);
    if (!missing.isEmpty()) {
        QProgressDialog progress(
            QStringLiteral("未翻訳の段落を翻訳しています（%1 へ: %2）…")
                .arg(targetLanguageName(m_trTarget), m_trModel),
            QStringLiteral("キャンセル"), 0, missing.size(), this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);

        OllamaClient client;
        int done = 0;
        for (const QString &text : missing) {
            if (progress.wasCanceled())
                return;
            progress.setValue(done);

            QEventLoop loop;
            bool ok = false;
            QString out;
            auto conn = connect(&client, &OllamaClient::finished, &loop,
                                [&](bool o, const QString &r) { ok = o; out = r; loop.quit(); });
            client.translate(m_trEndpoint, m_trModel, targetLanguageName(m_trTarget), text);
            loop.exec();
            disconnect(conn);

            if (!ok) {
                QMessageBox::warning(this, QStringLiteral("Spindle"),
                                     QStringLiteral("翻訳に失敗しました:\n%1").arg(out));
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

QString MainWindow::translateViewString() const
{
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
    m_trQueue.clear();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const int index = o.value(QStringLiteral("index")).toInt();
        const QString text = o.value(QStringLiteral("text")).toString();
        // Cache hit → apply instantly without calling Ollama.
        const QString cached = m_trCache.lookup(text);
        if (!cached.isEmpty()) {
            if (m_bridge)
                m_bridge->applyTranslation(index, cached, QString());
            continue;
        }
        m_trQueue.append({index, text});
    }
    m_trCursor = 0;
    m_trAnyOk = false;
    translateNext(m_trRunId);
}

void MainWindow::translateNext(int run)
{
    if (run != m_trRunId)
        return; // superseded by a newer run
    if (m_trCursor >= m_trQueue.size())
        return;
    const auto &item = m_trQueue.at(m_trCursor);
    m_trCurIndex = item.first;
    m_trCurText = item.second;
    m_trReqRun = run;
    if (m_bridge)
        m_bridge->applyTranslation(m_trCurIndex, QStringLiteral("翻訳中…"),
                                   QStringLiteral("pending"));
    m_ollama->translate(m_trEndpoint, m_trModel, targetLanguageName(m_trTarget), item.second);
}

void MainWindow::onOllamaFinished(bool ok, const QString &result)
{
    if (m_trReqRun != m_trRunId)
        return; // stale reply (chapter/mode changed)

    if (ok) {
        m_trAnyOk = true;
        m_trCache.put(m_trCurText, result); // remember for next time / export
        m_trCacheSave->start();
        if (m_bridge)
            m_bridge->applyTranslation(m_trCurIndex, result, QString());
    } else {
        if (m_bridge)
            m_bridge->applyTranslation(m_trCurIndex,
                                       QStringLiteral("⚠ 翻訳に失敗しました: ") + result,
                                       QStringLiteral("error"));
        // A failure before any success usually means Ollama is unreachable —
        // stop rather than spamming every paragraph with the same error.
        if (!m_trAnyOk) {
            m_trQueue.clear();
            return;
        }
    }
    ++m_trCursor;
    translateNext(m_trRunId);
}

void MainWindow::translateSelection(const QString &text)
{
    const QString src = text.trimmed();
    if (src.isEmpty())
        return;
    showTranslatePopup(QStringLiteral("翻訳中…"));
    m_selectionOllama->translate(m_trEndpoint, m_trModel, targetLanguageName(m_trTarget), src);
}

void MainWindow::onSelectionTranslated(bool ok, const QString &result)
{
    if (!m_translatePopup || !m_translatePopup->isVisible())
        return;
    showTranslatePopup(ok ? result : QStringLiteral("⚠ 翻訳に失敗しました: ") + result);
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

    form->addRow(QStringLiteral("表示モード"), modeBox);
    form->addRow(QStringLiteral("翻訳先"), targetBox);
    form->addRow(QStringLiteral("モデル"), modelEdit);
    form->addRow(QStringLiteral("エンドポイント"), endpointEdit);

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
            m_trCache.load(m_bookId, m_trTarget); // switch to the new language's cache
        }
        QSettings settings;
        settings.setValue(QStringLiteral("translate/target"), m_trTarget);
        settings.setValue(QStringLiteral("translate/model"), m_trModel);
        settings.setValue(QStringLiteral("translate/endpoint"), m_trEndpoint);
        if (m_translateView == TranslateView::Original)
            m_translateView = TranslateView::Bilingual;
        if (m_currentChapter >= 0)
            displayChapter(m_currentChapter); // reload clean, then auto-translate
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
                && h.start == m.start && h.end == m.end) {
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
        h.text = m.matchedText;
        h.start = m.start;
        h.end = m.end;
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
    if (m_searchInput->text().trimmed().isEmpty())
        m_sidebarStack->setCurrentIndex(tab);
}

void MainWindow::updateSidebarMode()
{
    const bool searching = !m_searchInput->text().trimmed().isEmpty();
    m_sidebarStack->setCurrentIndex(searching ? 2 : m_sidebarTab);
}

// --- events ----------------------------------------------------------------

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.toLocalFile().endsWith(QStringLiteral(".epub"), Qt::CaseInsensitive)) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    // Each dropped EPUB opens its own window (the current book stays open).
    for (const QUrl &url : event->mimeData()->urls()) {
        const QString local = url.toLocalFile();
        if (local.endsWith(QStringLiteral(".epub"), Qt::CaseInsensitive))
            openEpubSmart(local);
    }
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
