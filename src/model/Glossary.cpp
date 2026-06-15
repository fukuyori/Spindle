#include "model/Glossary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

void Glossary::load(const QString &epubPath, const QString &lang)
{
    m_entries.clear();
    if (epubPath.isEmpty() || lang.isEmpty())
        return;

    QString base = epubPath; // <book>.glossary.json  (e.g. book.epub -> book.glossary.json)
    if (base.endsWith(QLatin1String(".epub"), Qt::CaseInsensitive))
        base.chop(5);
    QFile f(base + QStringLiteral(".glossary.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    // The file is for one source→target pair. Only apply it when its target
    // language matches the language we're translating into (a missing
    // target_lang is treated as "applies to any target").
    const QString fileTarget = root.value(QStringLiteral("target_lang")).toString().trimmed();
    if (!fileTarget.isEmpty() && fileTarget != lang)
        return;

    const QJsonArray arr = root.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.source = o.value(QStringLiteral("src")).toString().trimmed();
        e.target = o.value(QStringLiteral("dst")).toString().trimmed();
        e.note = o.value(QStringLiteral("note")).toString();
        if (!e.source.isEmpty() && !e.target.isEmpty())
            m_entries.append(e);
    }
}

QString Glossary::promptBlock() const
{
    if (m_entries.isEmpty())
        return {};
    QString block = QStringLiteral(
        "\n\nGlossary — always translate these terms with the exact given target, "
        "keeping usage consistent (adjust only for grammatical inflection):");
    for (const Entry &e : m_entries) {
        block += QStringLiteral("\n- %1 => %2").arg(e.source, e.target);
        if (!e.note.isEmpty())
            block += QStringLiteral(" (%1)").arg(e.note);
    }
    return block;
}
