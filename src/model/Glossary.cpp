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

} // namespace

bool Glossary::appearsInText(const QString &source, const QString &text)
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

QString Glossary::filePathFor(const QString &epubPath)
{
    QString base = epubPath; // <book>.glossary.json  (e.g. book.epub -> book.glossary.json)
    if (base.endsWith(QLatin1String(".epub"), Qt::CaseInsensitive))
        base.chop(5);
    return base + QStringLiteral(".glossary.json");
}

QVector<Glossary::Entry> Glossary::readFile(const QString &epubPath, QString *sourceLang,
                                            QString *targetLang)
{
    if (sourceLang)
        sourceLang->clear();
    if (targetLang)
        targetLang->clear();

    QVector<Entry> entries;
    if (epubPath.isEmpty())
        return entries;
    QFile f(filePathFor(epubPath));
    if (!f.open(QIODevice::ReadOnly))
        return entries;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();

    if (sourceLang)
        *sourceLang = root.value(QStringLiteral("source_lang")).toString().trimmed();
    if (targetLang)
        *targetLang = root.value(QStringLiteral("target_lang")).toString().trimmed();

    const QJsonArray arr = root.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.source = o.value(QStringLiteral("src")).toString().trimmed();
        e.target = o.value(QStringLiteral("dst")).toString().trimmed();
        e.note = o.value(QStringLiteral("note")).toString();
        if (!e.source.isEmpty() && !e.target.isEmpty())
            entries.append(e);
    }
    return entries;
}

bool Glossary::writeFile(const QString &epubPath, const QString &sourceLang,
                         const QString &targetLang, const QVector<Entry> &entries,
                         QString *error)
{
    QJsonArray arr;
    for (const Entry &e : entries) {
        QJsonObject o;
        o[QStringLiteral("src")] = e.source;
        o[QStringLiteral("dst")] = e.target;
        if (!e.note.isEmpty())
            o[QStringLiteral("note")] = e.note;
        arr.append(o);
    }
    QJsonObject root;
    if (!sourceLang.isEmpty())
        root[QStringLiteral("source_lang")] = sourceLang;
    if (!targetLang.isEmpty())
        root[QStringLiteral("target_lang")] = targetLang;
    root[QStringLiteral("entries")] = arr;

    QFile f(filePathFor(epubPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    if (f.error() != QFile::NoError) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

void Glossary::load(const QString &epubPath, const QString &lang)
{
    m_entries.clear();
    if (epubPath.isEmpty() || lang.isEmpty())
        return;

    // The file is for one source→target pair. Only apply it when its target
    // language matches the language we're translating into (a missing
    // target_lang is treated as "applies to any target").
    QString fileTarget;
    const QVector<Entry> entries = readFile(epubPath, nullptr, &fileTarget);
    if (!fileTarget.isEmpty() && fileTarget != lang)
        return;
    m_entries = entries;
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
        if (appearsInText(e.source, text))
            matched.append(e);
    }
    return promptBlockFromEntries(matched);
}
