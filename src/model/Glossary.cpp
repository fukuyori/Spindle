#include "model/Glossary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

QString promptBlockFromEntries(const QVector<Glossary::Entry> &entries)
{
    if (entries.isEmpty())
        return {};
    QString block = QStringLiteral(
        "\n\nGlossary — always translate these terms with the exact given target, "
        "keeping usage consistent (adjust only for grammatical inflection):");
    for (const Glossary::Entry &e : entries) {
        block += QStringLiteral("\n- %1 => %2").arg(e.source, e.target);
        if (!e.note.isEmpty())
            block += QStringLiteral(" (%1)").arg(e.note);
    }
    return block;
}

bool isWordish(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

bool sourceAppearsInText(const QString &source, const QString &text)
{
    if (source.isEmpty() || text.isEmpty())
        return false;

    QString pattern = QRegularExpression::escape(source);
    if (isWordish(source.front()))
        pattern.prepend(QStringLiteral("(?<![\\p{L}\\p{N}_])"));
    if (isWordish(source.back()))
        pattern.append(QStringLiteral("(?![\\p{L}\\p{N}_])"));

    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption
                                       | QRegularExpression::UseUnicodePropertiesOption);
    return re.match(text).hasMatch();
}

} // namespace

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
    return promptBlockFromEntries(m_entries);
}

QString Glossary::promptBlockForText(const QString &text) const
{
    QVector<Entry> matched;
    matched.reserve(m_entries.size());
    for (const Entry &e : m_entries) {
        if (sourceAppearsInText(e.source, text))
            matched.append(e);
    }
    return promptBlockFromEntries(matched);
}
