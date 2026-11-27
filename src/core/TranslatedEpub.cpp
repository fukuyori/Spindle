#include "core/TranslatedEpub.h"

#include "core/BlockIndex.h"
#include "epub/EpubBook.h"
#include "model/TranslationCache.h"

#include "miniz.h"

#include <QDomDocument>
#include <QRegularExpression>
#include <QSet>
#include <QVector>
#include <cstring>

namespace translated_epub {

namespace {

bool isBlockTag(const QString &tag)
{
    static const QSet<QString> tags = {"p",  "h1", "h2", "h3",          "h4",
                                       "h5", "h6", "li", "blockquote",   "figcaption",
                                       "dd", "dt"};
    return tags.contains(tag.toLower());
}

QString tagOf(const QDomElement &el)
{
    const QString ln = el.localName();
    return (ln.isEmpty() ? el.tagName() : ln).toLower();
}

void appendText(const QDomNode &node, QString &out)
{
    for (QDomNode n = node.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isText() || n.isCDATASection())
            out += n.nodeValue();
        else if (n.isElement())
            appendText(n, out);
    }
}

// Leaf blocks: a block-level element with no nested block, ≥2 chars of text that
// contains a letter or number. Matches reader.js collectLeafBlocks(). Single
// O(n) pass: the return value reports whether the subtree contains any block
// element, so leaf-ness is decided without per-element descendant rescans.
bool collectLeafBlocks(const QDomElement &root, QVector<QDomElement> &out)
{
    bool hasBlock = false;
    for (QDomNode n = root.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (!n.isElement())
            continue;
        QDomElement el = n.toElement();
        const bool childHas = collectLeafBlocks(el, out);
        const bool blk = isBlockTag(tagOf(el));
        if (blk && !childHas) { // no block descendants -> subtree appended nothing
            QString text;
            appendText(el, text);
            const QString trimmed = text.trimmed();
            static const QRegularExpression alnum(QStringLiteral("[\\p{L}\\p{N}]"));
            if (trimmed.size() >= 2 && alnum.match(trimmed).hasMatch())
                out.append(el);
        }
        hasBlock = hasBlock || blk || childHas;
    }
    return hasBlock;
}

QDomDocument parse(const QString &xhtml)
{
    QDomDocument doc;
    if (!doc.setContent(xhtml))
        doc.setContent(xhtml, QDomDocument::ParseOption::UseNamespaceProcessing);
    return doc;
}

QDomElement bodyOf(QDomDocument &doc)
{
    QDomNodeList bodies = doc.elementsByTagName(QStringLiteral("body"));
    return bodies.isEmpty() ? doc.documentElement() : bodies.at(0).toElement();
}

// Set the package <dc:language> (the EPUB's language metadata) to `lang`.
QByteArray setOpfLanguage(const QString &opfXml, const QString &lang)
{
    if (lang.isEmpty())
        return opfXml.toUtf8();
    QDomDocument doc = parse(opfXml);
    QDomNodeList langs = doc.elementsByTagName(QStringLiteral("dc:language"));
    if (langs.isEmpty())
        langs = doc.elementsByTagName(QStringLiteral("language"));
    if (!langs.isEmpty()) {
        QDomElement el = langs.at(0).toElement();
        while (!el.firstChild().isNull())
            el.removeChild(el.firstChild());
        el.appendChild(doc.createTextNode(lang));
    } else {
        QDomNodeList metas = doc.elementsByTagName(QStringLiteral("metadata"));
        if (!metas.isEmpty()) {
            QDomElement m = metas.at(0).toElement();
            QDomElement l = doc.createElement(QStringLiteral("dc:language"));
            l.appendChild(doc.createTextNode(lang));
            m.appendChild(l);
        }
    }
    return doc.toByteArray(0);
}

// The language the chapter is written in: its <html> xml:lang / lang, else the
// book's package language.
QString sourceLanguage(const QDomDocument &doc, const QString &bookLang)
{
    const QDomElement html = doc.documentElement();
    if (!html.isNull()) {
        for (const QString &attr : {QStringLiteral("xml:lang"), QStringLiteral("lang")}) {
            const QString v = html.attribute(attr).trimmed();
            if (!v.isEmpty())
                return v;
        }
    }
    return bookLang.trimmed();
}

void setLang(QDomElement &el, const QString &lang)
{
    el.setAttribute(QStringLiteral("xml:lang"), lang);
    el.setAttribute(QStringLiteral("lang"), lang);
}

bool hasOwnLang(const QDomElement &el)
{
    return el.hasAttribute(QStringLiteral("xml:lang")) || el.hasAttribute(QStringLiteral("lang"));
}

QByteArray transformChapter(const QString &xhtml, Mode mode, const TranslationCache &cache,
                            const QString &bookLang)
{
    QDomDocument doc = parse(xhtml);
    QDomElement body = bodyOf(doc);
    if (body.isNull())
        return xhtml.toUtf8();
    // Read before <html> is re-marked with the target language below.
    const QString srcLang = sourceLanguage(doc, bookLang);

    // Same bare-text wrapping as the reading view, so cache keys line up.
    block_index::normalizeBareText(body);
    QVector<QDomElement> blocks;
    collectLeafBlocks(body, blocks);

    for (QDomElement block : blocks) {
        QString src;
        appendText(block, src);
        const QString tr = cache.lookup(src);
        // Bilingual: <html> carries the target language, so every original
        // block (translated or not) states the source language itself, unless
        // the book already gave it one.
        const bool markSource =
            mode == Mode::Bilingual && !srcLang.isEmpty() && !hasOwnLang(block);
        if (tr.isEmpty()) {
            if (markSource)
                setLang(block, srcLang);
            continue; // leave untranslated blocks as-is
        }

        if (mode == Mode::Translation) {
            while (!block.firstChild().isNull())
                block.removeChild(block.firstChild());
            block.appendChild(doc.createTextNode(tr));
        } else { // Bilingual: insert a translation block after the original,
                 // cloning its tag/attributes so the translation keeps the
                 // original's formatting (headings, classes, inline styles).
            QDomElement p = block.cloneNode(false).toElement();
            p.removeAttribute(QStringLiteral("id"));
            // The translation inherits the target language from <html>.
            p.removeAttribute(QStringLiteral("xml:lang"));
            p.removeAttribute(QStringLiteral("lang"));
            const QString cls = p.attribute(QStringLiteral("class"));
            p.setAttribute(QStringLiteral("class"),
                           cls.isEmpty() ? QStringLiteral("spindle-translation")
                                         : cls + QStringLiteral(" spindle-translation"));
            QString style = p.attribute(QStringLiteral("style"));
            if (!style.isEmpty() && !style.endsWith(QLatin1Char(';')))
                style += QLatin1Char(';');
            // Same bilingual rhythm as the in-app view: hug the original,
            // clear gap before the next original.
            style += QStringLiteral(
                "opacity:0.85;margin-block-start:0.3em;margin-block-end:1.1em;");
            if (block.tagName().compare(QStringLiteral("li"), Qt::CaseInsensitive) == 0)
                style += QStringLiteral("display:block;"); // no second marker / renumbering
            p.setAttribute(QStringLiteral("style"), style);
            QString blockStyle = block.attribute(QStringLiteral("style"));
            if (!blockStyle.isEmpty() && !blockStyle.endsWith(QLatin1Char(';')))
                blockStyle += QLatin1Char(';');
            block.setAttribute(QStringLiteral("style"),
                               blockStyle + QStringLiteral("margin-block-end:0.3em;"));
            if (markSource)
                setLang(block, srcLang);
            p.appendChild(doc.createTextNode(tr));
            QDomNode parent = block.parentNode();
            if (!parent.isNull())
                parent.insertAfter(p, block);
        }
    }

    // The exported book is a target-language book (its OPF language is set to
    // the target too), in both modes — mark the document.
    if (!cache.lang().isEmpty()) {
        QDomElement html = doc.documentElement();
        if (!html.isNull())
            setLang(html, cache.lang());
    }

    return doc.toByteArray(0);
}

} // namespace

QStringList collectMissing(const EpubBook &book, const TranslationCache &cache)
{
    QStringList missing;
    QSet<QString> seen;
    for (const Chapter &ch : book.chapters()) {
        QDomDocument doc = parse(book.readText(ch.path));
        QDomElement body = bodyOf(doc);
        if (body.isNull())
            continue;
        block_index::normalizeBareText(body);
        QVector<QDomElement> blocks;
        collectLeafBlocks(body, blocks);
        for (const QDomElement &block : blocks) {
            QString src;
            appendText(block, src);
            const QString key = TranslationCache::normalizeKey(src);
            if (key.isEmpty() || seen.contains(key))
                continue;
            seen.insert(key);
            if (!cache.contains(src))
                missing.append(key);
        }
    }
    return missing;
}

bool write(const EpubBook &book, const QString &srcPath, const QString &outPath, Mode mode,
           const TranslationCache &cache, QString *err)
{
    auto fail = [&](const QString &m) {
        if (err)
            *err = m;
        return false;
    };

    QSet<QString> chapterPaths;
    for (const Chapter &ch : book.chapters())
        chapterPaths.insert(ch.path);

    mz_zip_archive zin;
    std::memset(&zin, 0, sizeof(zin));
    if (!mz_zip_reader_init_file(&zin, srcPath.toUtf8().constData(), 0))
        return fail(QStringLiteral("元 EPUB を開けませんでした"));

    mz_zip_archive zout;
    std::memset(&zout, 0, sizeof(zout));
    if (!mz_zip_writer_init_file(&zout, outPath.toUtf8().constData(), 0)) {
        mz_zip_reader_end(&zin);
        return fail(QStringLiteral("出力 EPUB を作成できませんでした"));
    }

    const mz_uint count = mz_zip_reader_get_num_files(&zin);
    bool ok = true;
    for (mz_uint i = 0; i < count && ok; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zin, i))
            continue;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zin, i, &st))
            continue;
        const QString name = QString::fromUtf8(st.m_filename);

        size_t sz = 0;
        void *data = mz_zip_reader_extract_to_heap(&zin, i, &sz, 0);
        if (!data) {
            ok = false;
            break;
        }
        QByteArray bytes(static_cast<const char *>(data), static_cast<int>(sz));
        mz_free(data);

        if (chapterPaths.contains(name))
            bytes = transformChapter(QString::fromUtf8(bytes), mode, cache, book.language());
        else if (name.endsWith(QLatin1String(".opf"), Qt::CaseInsensitive))
            bytes = setOpfLanguage(QString::fromUtf8(bytes), cache.lang());

        const mz_uint level = (name == QLatin1String("mimetype"))
                                  ? MZ_NO_COMPRESSION
                                  : static_cast<mz_uint>(MZ_DEFAULT_LEVEL);
        if (!mz_zip_writer_add_mem(&zout, st.m_filename, bytes.constData(),
                                   static_cast<size_t>(bytes.size()), level))
            ok = false;
    }

    if (ok)
        ok = mz_zip_writer_finalize_archive(&zout);
    mz_zip_writer_end(&zout);
    mz_zip_reader_end(&zin);

    if (!ok)
        return fail(QStringLiteral("EPUB の書き込みに失敗しました"));
    return true;
}

} // namespace translated_epub
