#include "model/Glossary.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace {

bool isWordish(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// CJK prose is written without spaces, so a term inside running Japanese or
// Chinese text never sits at a "word boundary" — the boundary guards must be
// skipped for CJK-edged terms or they would never match mid-sentence.
bool isCjk(QChar c)
{
    const ushort u = c.unicode();
    return (u >= 0x3040 && u <= 0x30FF) // hiragana + katakana
        || (u >= 0x3400 && u <= 0x9FFF) // Han (ext-A + unified)
        || (u >= 0xF900 && u <= 0xFAFF) // Han compatibility
        || (u >= 0xAC00 && u <= 0xD7A3); // hangul
}

} // namespace

bool Glossary::appearsInText(const QString &source, const QString &text)
{
    if (source.isEmpty() || text.isEmpty())
        return false;

    QString pattern = QRegularExpression::escape(source);
    if (isWordish(source.front()) && !isCjk(source.front()))
        pattern.prepend(QStringLiteral("(?<![\\p{L}\\p{N}_])"));
    if (isWordish(source.back()) && !isCjk(source.back())) {
        // Tolerate common English inflection so "Caesar" still selects the
        // entry in a block that only has "Caesars" / "Caesar's".
        pattern.append(QStringLiteral("(?:'s|es|s)?(?![\\p{L}\\p{N}_])"));
    }

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

QString Glossary::promptBlockForText(const QString &text, Purpose purpose) const
{
    QVector<Entry> matched;
    matched.reserve(m_entries.size());
    for (const Entry &e : m_entries) {
        if (appearsInText(e.source, text))
            matched.append(e);
    }
    if (matched.isEmpty())
        return {};

    // Longest source first, so the most specific terms survive the size cap.
    std::stable_sort(matched.begin(), matched.end(), [](const Entry &a, const Entry &b) {
        return a.source.size() > b.source.size();
    });

    // A glossary that dwarfs the text drowns it: with a short block the model
    // answers with translated glossary lines instead of the translation. Cap
    // the term list near the text's own length, with a floor so ordinary
    // paragraphs still get a handful of terms. The first matched entry is
    // always sent — an empty block would silently drop the glossary.
    const qsizetype budget = qMax(qsizetype(200), text.size() * 3 / 2);
    QStringList items;
    qsizetype used = 0;
    for (const Entry &e : matched) {
        const QString item = QStringLiteral("%1 => %2").arg(e.source, e.target);
        if (!items.isEmpty() && used + item.size() > budget)
            break;
        items.append(item);
        used += item.size() + 2;
    }

    // One terse line, not an instruction paragraph: next to a short source
    // text, long instructions get treated as content to translate.
    const QString header =
        purpose == Purpose::Summary
            ? QStringLiteral("Write these terms exactly as given: ")
            : QStringLiteral("Use exactly these translations, inflected only as grammar "
                             "requires: ");
    return QStringLiteral("\n\n") + header + items.join(QStringLiteral("; "))
        + QStringLiteral(".");
}

QString Glossary::exactTarget(const QString &text) const
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return {};
    for (const Entry &e : m_entries) {
        if (t.compare(e.source, Qt::CaseInsensitive) == 0)
            return e.target;
    }
    return {};
}
