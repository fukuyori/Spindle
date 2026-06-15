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

    // Called from JS.
    Q_INVOKABLE QString currentHighlights() const { return m_json; }
    Q_INVOKABLE void selectionMade(int start, int end, const QString &text)
    {
        emit selectionReceived(start, end, text);
    }
    Q_INVOKABLE void markClicked(const QString &id) { emit markActivated(id); }

    Q_INVOKABLE QString currentTranslateView() const { return m_translateView; }
    Q_INVOKABLE void blocksCollected(const QString &json) { emit blocksReady(json); }

    // Called from C++ to drive the page.
    void applyTranslation(int index, const QString &text, const QString &state)
    {
        emit translationReady(index, text, state);
    }

signals:
    void selectionReceived(int start, int end, const QString &text);
    void markActivated(const QString &id);
    void highlightsChanged();
    void translateViewChanged(const QString &view);
    void blocksReady(const QString &json);
    void translationReady(int index, const QString &text, const QString &state);

private:
    QString m_json;
    QString m_translateView = QStringLiteral("original");
};
