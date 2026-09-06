#pragma once

#include <QObject>
#include <QString>

// QWebChannel bridge between the chapter page's JavaScript and the C++ app.
// Offsets are character offsets into the chapter <body>'s textContent, computed
// by the page JS so creation and rendering share one coordinate space.
class Bridge : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    void setHighlightsJson(const QString &json) { m_json = json; }
    void notifyChanged() { emit highlightsChanged(); }

    void setTranslateView(const QString &view) { m_translateView = view; }
    void notifyTranslateViewChanged() { emit translateViewChanged(m_translateView); }

    void setTranslateLang(const QString &lang) { m_lang = lang; }

    // Called from JS.
    Q_INVOKABLE QString currentHighlights() const { return m_json; }
    Q_INVOKABLE void selectionMade(int block, const QString &side, const QString &lang,
                                   int offset, int length, const QString &text)
    {
        emit selectionReceived(block, side, lang, offset, length, text);
    }
    Q_INVOKABLE void markClicked(const QString &id) { emit markActivated(id); }

    Q_INVOKABLE QString currentTranslateView() const { return m_translateView; }
    Q_INVOKABLE QString currentTranslateLang() const { return m_lang; }
    Q_INVOKABLE void blocksCollected(const QString &json) { emit blocksReady(json); }
    Q_INVOKABLE void fixedPageEdgeClicked(int edge)
    {
        if (edge != 0)
            emit fixedPageEdgeClickRequested(edge < 0 ? -1 : 1);
    }
    Q_INVOKABLE void pageWritingModeDetected(bool vertical)
    {
        emit pageWritingModeDetectionRequested(vertical);
    }
    // A pre-paginated page reporting its own coordinate space, so C++ can size
    // the view to it and lay the spread out as one canvas. Both halves share
    // this bridge, so the page says which one it is.
    Q_INVOKABLE void fixedPageMeasured(bool companion, double width, double height)
    {
        emit fixedPageMeasurementReported(companion, width, height);
    }

    // Called from C++ to drive the page.
    void applyTranslation(int index, const QString &text, const QString &state)
    {
        emit translationReady(index, text, state);
    }
    void requestScrollToHighlight(const QString &id) { emit scrollToHighlight(id); }

signals:
    void selectionReceived(int block, const QString &side, const QString &lang,
                           int offset, int length, const QString &text);
    void markActivated(const QString &id);
    void highlightsChanged();
    void translateViewChanged(const QString &view);
    void blocksReady(const QString &json);
    void fixedPageEdgeClickRequested(int edge);
    void pageWritingModeDetectionRequested(bool vertical);
    void fixedPageMeasurementReported(bool companion, double width, double height);
    void translationReady(int index, const QString &text, const QString &state);
    void scrollToHighlight(const QString &id);

private:
    QString m_json;
    QString m_translateView = QStringLiteral("original");
    QString m_lang;
};
