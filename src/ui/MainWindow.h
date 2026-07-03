#pragma once

#include "core/ChapterText.h"
#include "epub/EpubBook.h"
#include "model/Glossary.h"
#include "model/Highlight.h"
#include "model/TranslationCache.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QMainWindow>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

class EpubBook;
class EpubSchemeHandler;
class QFontComboBox;
class QDialog;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QAction;
class QProgressBar;
class QStackedWidget;
class QSplitter;
class QTimer;
class QWebEngineView;
class QWebChannel;
class QTextEdit;
class Bridge;
class OllamaClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openEpub(const QString &filePath);

    // Open `path` in this window if it has no book yet, otherwise in a new one.
    void openEpubSmart(const QString &filePath);
    // Create a fresh window (auto-deleted on close) and optionally open a book.
    static MainWindow *openInNewWindow(const QString &filePath = QString());
    // Number of live MainWindow instances.
    static int instanceCount();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override; // persist window geometry
    void showEvent(QShowEvent *event) override;    // focus the reading pane
    // The central QWebEngineView consumes drops over the page area, so we filter
    // its internal child widgets to catch EPUB drops there too.
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onOpenTriggered();
    void onTocItemActivated(QTreeWidgetItem *item, int column);
    void nextChapter();
    void previousChapter();
    void increaseFont();
    void decreaseFont();
    void cycleTheme();
    void toggleXmlView(bool on);
    void onSearchTextChanged();
    void runSearch();
    void onSearchResultActivated(QListWidgetItem *item);
    void onHighlightActivated(QListWidgetItem *item);
    void showSidebarTab(int tab); // 0 = toc, 1 = highlights, 3 = recent EPUBs
    void onLoadFinished(bool ok);
    void onWebSelection(int block, const QString &side, const QString &lang, int offset,
                        int length, const QString &text);
    void openTranslateDialog();
    void openSummarySettingsDialog();
    void onBlocksReady(const QString &json);
    void onOllamaFinished(int requestId, bool ok, const QString &result);
    void onSelectionTranslated(int requestId, bool ok, const QString &result);
    void onSummaryFinished(int requestId, bool ok, const QString &result);
    void onMarkClicked(const QString &id);
    void exportHighlightsMarkdown();
    void exportHighlightsJson();
    void importHighlights();
    void importKindleNotebook();
    void exportChapterAozora();
    void exportTranslatedEpub(int mode); // 0 bilingual, 1 translation-only
    void showAboutDialog();
    void openAppearanceDialog();

private:
    void buildUi();
    void applyFontChoice(); // read the font picker, persist, and re-inject CSS
    QString translationColor() const; // resolve m_trColor for the current theme ("" = none)
    QColor originalTextColor() const;
    QColor translationTextColor() const;
    static bool mimeHasEpub(const class QMimeData *mime);
    void openEpubsFromMime(const class QMimeData *mime);
    QStringList recentEpubs() const;
    void addRecentEpub(const QString &filePath);
    void removeRecentEpub(const QString &filePath);
    void updateRecentEpubsMenu();
    void updateRecentEpubsView();
    void showRecentEpubsPane();
    void openRecentEpub(const QString &filePath);
    int recentChapterIndex(const QString &filePath) const;
    QString recentChapterLabel(const QString &filePath) const;
    // Debounced: records the pending (path, index, label) and commits shortly
    // after the last chapter change (QSettings hits the registry on Windows).
    void saveRecentChapter(const QString &filePath, int index, const QString &label);
    void commitRecentChapter();
    void populateToc();
    void addTocItems(const QVector<TocItem> &items, QTreeWidgetItem *parent);
    void displayChapter(int index, const QString &fragment = QString());
    void applyZoom();
    // Theme CSS is delivered two ways: a DocumentCreation user script (so a
    // newly navigated chapter paints correctly on its very first frame — no
    // flash of the book's own styles) and a live runJavaScript for the page
    // currently on screen (settings changes apply without a reload).
    void injectViewStyle();
    QString viewStyleCss() const;
    void updateThemeScript(const QString &css);
    QColor themeBackground() const;
    void updateLocation();
    void updateNavButtons();
    void updateSidebarMode();

    void ensureChapterTexts();
    void startChapterTextsBuild(); // kick off the background build after open
    void adoptChapterTexts();      // take the finished future's result (once)

    // Deferred web-view creation: the window shows immediately and Chromium
    // (the bulk of cold-start time) initializes afterwards / on first use.
    void ensureWebView();
    void restoreViewSettings(); // QSettings restore split out of setupWebChannel

    // Web channel / highlights
    void setupWebChannel();
    QString chapterHighlightsJson() const;
    void pushHighlightsToView();
    void createHighlight(HighlightColor color, int block, HighlightSide side, const QString &lang,
                         int offset, int length, const QString &selectedText,
                         const QString &note = QString());
    void removeHighlightById(const QString &id);
    void setHighlightColor(const QString &id, HighlightColor color);
    void editHighlightNote(const QString &id);
    // Multi-line note editor with word wrap (replaces QInputDialog::getMultiLineText).
    QString promptNoteText(const QString &label, const QString &initial, bool *ok);
    Highlight *findHighlight(const QString &id);
    void renderHighlightsList();
    void persistHighlights();
    void afterHighlightsMutated();

    // Translation
    void setTranslateView(int view); // 0 original, 1 bilingual, 2 translation
    void translateNext(int run);
    QString translateViewString() const;
    // True when the book's own language matches `targetCode` (primary subtag),
    // i.e. translating it into that language is a no-op.
    bool isBookLanguage(const QString &targetCode) const;
    void translateSelection(const QString &text);
    void summarizeSelection(const QString &text);
    void summarizeCurrentChapter();
    void regenerateCurrentChapterSummary();
    void openSavedCurrentChapterSummary();
    void generateCurrentChapterSummary(bool force);
    void setSummaryDetail(int detail);
    QString summaryDetailLabel() const;
    QString summaryDetailKey() const;
    QString summaryDetailInstruction() const;
    QString effectiveSummaryModel() const;
    void saveCurrentChapterSummary();
    void translateCurrentSummary();
    void showTranslatePopup(const QString &text);
    void showSummaryDialog(const QString &title, const QString &text);

    // Translated-EPUB export: queue-driven async translation (2 requests in
    // flight, no nested event loop; cancel keeps already-translated paragraphs).
    void startTranslatedEpubExport(const QStringList &missing);
    void exportTranslateNext();
    void onExportTranslateFinished(int requestId, bool ok, const QString &result);
    void cancelTranslatedEpubExport();
    void closeExportDialog();
    void finishTranslatedEpubExport(); // save-file dialog + zip write

    enum class Theme { Light, Sepia, Dark };
    enum class TranslateView { Original, Bilingual, Translation };
    enum class SummaryDetail { Brief, Standard, Detailed };
    struct BrightnessAdjust {
        int background = 0;
        int original = 0;
        int translation = 0;
    };

    std::unique_ptr<EpubBook> m_book;
    QString m_bookId;
    QString m_epubPath; // source .epub path (for translated-epub export)
    int m_currentChapter = -1;
    int m_fontSize = 100; // percent (zoom)
    Theme m_theme = Theme::Light;
    BrightnessAdjust m_brightness[3];
    bool m_xmlView = false; // show raw chapter source instead of rendered view
    QString m_fontFamily;   // body font override ("" = use the book's own fonts)

    QVector<ChapterText> m_chapterTexts;
    bool m_chapterTextsReady = false;
    // Background build of m_chapterTexts (started right after the book opens so
    // the first search / summary / import doesn't stall parsing the whole book).
    bool m_chapterTextsBuilding = false;
    QFuture<QVector<ChapterText>> m_chapterTextsFuture;
    QFutureWatcher<QVector<ChapterText>> *m_chapterTextsWatcher = nullptr;
    QVector<Highlight> m_highlights;

    QString m_pendingFragment;
    QString m_pendingFind;
    QString m_pendingScrollId; // highlight id to scroll to after the page loads

    QString m_schemeId; // unique epub:// host for this window's book
    QWebEngineView *m_view = nullptr; // created lazily — see ensureWebView()
    QSplitter *m_splitter = nullptr;
    QWidget *m_viewPlaceholder = nullptr; // holds the view's splitter slot until then
    QWebChannel *m_channel = nullptr;
    Bridge *m_bridge = nullptr;
    OllamaClient *m_ollama = nullptr;
    OllamaClient *m_selectionOllama = nullptr; // ad-hoc selection translation
    OllamaClient *m_summaryOllama = nullptr;   // ad-hoc selection/chapter summary
    int m_selectionReqSeq = 0; // latest selection-translate request (stale replies dropped)
    int m_summaryReqSeq = 0;   // latest summary/summary-translate request
    bool m_summaryReqIsTranslate = false; // whether that request was 要約の翻訳
    QLabel *m_translatePopup = nullptr;
    QDialog *m_summaryDialog = nullptr;
    QTextEdit *m_summaryText = nullptr;
    QPushButton *m_summarySaveButton = nullptr;
    QPushButton *m_summaryRegenerateButton = nullptr;
    QPushButton *m_summaryTranslateButton = nullptr;
    QString m_summaryMarkdown;
    QString m_summaryPreTranslateTitle;
    bool m_summarySaveable = false;
    bool m_summaryTruncated = false;
    QString m_summaryChapterPath;
    QString m_summaryChapterTitle;
    SummaryDetail m_summaryDetail = SummaryDetail::Standard;

    TranslateView m_translateView = TranslateView::Original;
    QString m_trTarget = QStringLiteral("ja");
    QString m_trModel = QStringLiteral("qwen2.5");
    QString m_summaryModel;
    QString m_trEndpoint = QStringLiteral("http://localhost:11434");
    QString m_trColor; // translation text color: ""=none, preset key, or "#rrggbb"
    QVector<QPair<int, QString>> m_trQueue;
    int m_trCursor = 0;     // next queue index to dispatch
    int m_trInFlight = 0;   // requests currently awaiting a reply (current run)
    int m_trRunId = 0;
    // Each in-flight Ollama request remembers its run, block index and source
    // text, so a reply from a superseded run can't be misattributed to the
    // current block (which would shift the cache by one).
    struct TrRequest {
        int run = 0;
        int index = 0;
        QString text;
    };
    QHash<int, TrRequest> m_trReqs;
    int m_trReqSeq = 0;
    bool m_trAnyOk = false;
    bool m_trForce = false; // one-shot: 再翻訳 ignores the cache and re-requests
    TranslationCache m_trCache;
    Glossary m_trGlossary;
    QTimer *m_trCacheSave = nullptr;

    // Translated-EPUB export state (active while the progress dialog is up).
    OllamaClient *m_exportOllama = nullptr;
    QDialog *m_exportDialog = nullptr;
    QProgressBar *m_exportBar = nullptr;
    QStringList m_exportQueue;
    QHash<int, QString> m_exportReqs; // requestId -> source paragraph
    int m_exportCursor = 0;
    int m_exportInFlight = 0;
    int m_exportDone = 0;
    int m_exportMode = 0; // 0 bilingual, 1 translation-only
    bool m_exportActive = false;

    // Freezes view repaints during chapter navigation (the previous chapter
    // stays on screen until the new one has finished loading — no intermediate
    // frames). The timer is a safety net that unfreezes if a load never ends.
    QTimer *m_viewUnfreeze = nullptr;

    // Debounced last-read-chapter persistence (see saveRecentChapter).
    QTimer *m_recentSave = nullptr;
    QString m_recentPendingPath;
    int m_recentPendingIndex = -1;
    QString m_recentPendingLabel;
    QTreeWidget *m_toc = nullptr;
    QMenu *m_recentEpubsMenu = nullptr;
    QListWidget *m_recentEpubsList = nullptr;
    QListWidget *m_highlightsList = nullptr;
    QListWidget *m_searchResults = nullptr;
    QWidget *m_sidebar = nullptr;
    QStackedWidget *m_sidebarStack = nullptr;
    QAction *m_sidebarAction = nullptr;
    QPushButton *m_tabToc = nullptr;
    QPushButton *m_tabHighlights = nullptr;
    QPushButton *m_tabRecent = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QFontComboBox *m_fontCombo = nullptr;
    QAction *m_fontOverride = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_authorLabel = nullptr;
    QLabel *m_location = nullptr;
    QAction *m_prevAction = nullptr;
    QAction *m_nextAction = nullptr;
    QTimer *m_searchDebounce = nullptr;
    int m_sidebarTab = 0;
};
