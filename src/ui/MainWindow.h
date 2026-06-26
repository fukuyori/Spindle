#pragma once

#include "core/ChapterText.h"
#include "epub/EpubBook.h"
#include "model/Glossary.h"
#include "model/Highlight.h"
#include "model/TranslationCache.h"

#include <QHash>
#include <QMainWindow>
#include <QPair>
#include <QString>
#include <QVector>
#include <memory>

class EpubBook;
class EpubSchemeHandler;
class QFontComboBox;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QAction;
class QStackedWidget;
class QTimer;
class QWebEngineView;
class QWebChannel;
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
    void showSidebarTab(int tab); // 0 = toc, 1 = highlights
    void onLoadFinished(bool ok);
    void onWebSelection(int block, const QString &side, const QString &lang, int offset,
                        int length, const QString &text);
    void openTranslateDialog();
    void onBlocksReady(const QString &json);
    void onOllamaFinished(int requestId, bool ok, const QString &result);
    void onSelectionTranslated(int requestId, bool ok, const QString &result);
    void onMarkClicked(const QString &id);
    void exportHighlightsMarkdown();
    void exportHighlightsJson();
    void importHighlights();
    void importKindleNotebook();
    void exportChapterAozora();
    void exportTranslatedEpub(int mode); // 0 bilingual, 1 translation-only
    void showAboutDialog();

private:
    void buildUi();
    void applyFontChoice(); // read the font picker, persist, and re-inject CSS
    QString translationColor() const; // resolve m_trColor for the current theme ("" = none)
    static bool mimeHasEpub(const class QMimeData *mime);
    void openEpubsFromMime(const class QMimeData *mime);
    void populateToc();
    void addTocItems(const QVector<TocItem> &items, QTreeWidgetItem *parent);
    void displayChapter(int index, const QString &fragment = QString());
    void applyZoom();
    void injectViewStyle();
    QColor themeBackground() const;
    void updateLocation();
    void updateNavButtons();
    void updateSidebarMode();

    void ensureChapterTexts();

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
    void showTranslatePopup(const QString &text);

    enum class Theme { Light, Sepia, Dark };
    enum class TranslateView { Original, Bilingual, Translation };

    std::unique_ptr<EpubBook> m_book;
    QString m_bookId;
    QString m_epubPath; // source .epub path (for translated-epub export)
    int m_currentChapter = -1;
    int m_fontSize = 100; // percent (zoom)
    Theme m_theme = Theme::Light;
    bool m_xmlView = false; // show raw chapter source instead of rendered view
    QString m_fontFamily;   // body font override ("" = use the book's own fonts)

    QVector<ChapterText> m_chapterTexts;
    bool m_chapterTextsReady = false;
    QVector<Highlight> m_highlights;

    QString m_pendingFragment;
    QString m_pendingFind;
    QString m_pendingScrollId; // highlight id to scroll to after the page loads

    QString m_schemeId; // unique epub:// host for this window's book
    QWebEngineView *m_view = nullptr;
    QWebChannel *m_channel = nullptr;
    Bridge *m_bridge = nullptr;
    OllamaClient *m_ollama = nullptr;
    OllamaClient *m_selectionOllama = nullptr; // ad-hoc selection translation
    QLabel *m_translatePopup = nullptr;

    TranslateView m_translateView = TranslateView::Original;
    QString m_trTarget = QStringLiteral("ja");
    QString m_trModel = QStringLiteral("qwen2.5");
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
    QTreeWidget *m_toc = nullptr;
    QListWidget *m_highlightsList = nullptr;
    QListWidget *m_searchResults = nullptr;
    QWidget *m_sidebar = nullptr;
    QStackedWidget *m_sidebarStack = nullptr;
    QPushButton *m_tabToc = nullptr;
    QPushButton *m_tabHighlights = nullptr;
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
