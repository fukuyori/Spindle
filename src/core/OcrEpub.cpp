#include "core/OcrEpub.h"

#include "miniz.h"

#include <QDateTime>
#include <QFile>
#include <QStringList>
#include <QUuid>

namespace {

QString pageFileName(int index)
{
    return QStringLiteral("page%1.xhtml").arg(index + 1, 4, 10, QLatin1Char('0'));
}

QString pageXhtml(const ocr_epub::Page &page, const QString &lang)
{
    QString body;
    body += QStringLiteral("<h2>%1</h2>\n").arg(page.title.toHtmlEscaped());
    if (page.failed)
        body += QStringLiteral("<!-- OCR_FAILED -->\n");
    // One <p> per OCR line: glm-ocr emits logical lines (dialogue, paragraph
    // runs), not hard-wrapped column fragments, so lines reflow well.
    const QStringList lines = page.text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (!t.isEmpty())
            body += QStringLiteral("<p>%1</p>\n").arg(t.toHtmlEscaped());
    }
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               "<!DOCTYPE html>\n"
               "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"%1\" lang=\"%1\">\n"
               "<head>\n"
               "<meta charset=\"utf-8\"/>\n"
               "<title>%2</title>\n"
               "<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/>\n"
               "</head>\n"
               "<body>\n%3</body>\n"
               "</html>\n")
        .arg(lang, page.title.toHtmlEscaped(), body);
}

bool addEntry(mz_zip_archive *zip, const char *name, const QByteArray &data, mz_uint level)
{
    return mz_zip_writer_add_mem(zip, name, data.constData(), size_t(data.size()), level);
}

} // namespace

namespace ocr_epub {

bool write(const QString &outPath, const QString &title, const QString &language,
           bool verticalRtl, const QVector<Page> &pages, QString *err)
{
    if (err)
        err->clear();
    if (pages.isEmpty()) {
        if (err)
            *err = QStringLiteral("no pages");
        return false;
    }
    const QString lang = language.trimmed().isEmpty() ? QStringLiteral("und")
                                                      : language.trimmed();

    // --- content ----------------------------------------------------------
    const QString container = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<container version=\"1.0\" "
        "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n"
        "  <rootfiles>\n"
        "    <rootfile full-path=\"OEBPS/content.opf\" "
        "media-type=\"application/oebps-package+xml\"/>\n"
        "  </rootfiles>\n"
        "</container>\n");

    QString css = QStringLiteral("body { margin: 1em; line-height: 1.8; }\n"
                                 "h2 { font-size: 1.05em; margin: 0 0 1em 0; }\n"
                                 "p { margin: 0; text-indent: 0; }\n");
    if (verticalRtl) {
        css += QStringLiteral("html { writing-mode: vertical-rl; "
                              "-epub-writing-mode: vertical-rl; }\n");
    }

    QString manifest;
    QString spine;
    QString navList;
    for (int i = 0; i < pages.size(); ++i) {
        const QString file = pageFileName(i);
        manifest += QStringLiteral("    <item id=\"p%1\" href=\"%2\" "
                                   "media-type=\"application/xhtml+xml\"/>\n")
                        .arg(i + 1)
                        .arg(file);
        spine += QStringLiteral("    <itemref idref=\"p%1\"/>\n").arg(i + 1);
        navList += QStringLiteral("      <li><a href=\"%1\">%2</a></li>\n")
                       .arg(file, pages.at(i).title.toHtmlEscaped());
    }

    const QString opf =
        QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" "
            "unique-identifier=\"uid\">\n"
            "  <metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
            "    <dc:identifier id=\"uid\">urn:uuid:%1</dc:identifier>\n"
            "    <dc:title>%2</dc:title>\n"
            "    <dc:language>%3</dc:language>\n"
            "    <meta property=\"dcterms:modified\">%4</meta>\n"
            "  </metadata>\n"
            "  <manifest>\n"
            "    <item id=\"nav\" href=\"nav.xhtml\" "
            "media-type=\"application/xhtml+xml\" properties=\"nav\"/>\n"
            "    <item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n"
            "%5"
            "  </manifest>\n"
            "  <spine%6>\n"
            "%7"
            "  </spine>\n"
            "</package>\n")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces),
                 title.toHtmlEscaped(), lang,
                 QDateTime::currentDateTimeUtc().toString(QStringLiteral(
                     "yyyy-MM-ddThh:mm:ssZ")),
                 manifest,
                 verticalRtl ? QStringLiteral(" page-progression-direction=\"rtl\"")
                             : QString(),
                 spine);

    const QString nav =
        QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<!DOCTYPE html>\n"
            "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
            "xmlns:epub=\"http://www.idpf.org/2007/ops\" xml:lang=\"%1\" lang=\"%1\">\n"
            "<head><meta charset=\"utf-8\"/><title>%2</title></head>\n"
            "<body>\n"
            "  <nav epub:type=\"toc\">\n"
            "    <ol>\n"
            "%3"
            "    </ol>\n"
            "  </nav>\n"
            "</body>\n"
            "</html>\n")
            .arg(lang, title.toHtmlEscaped(), navList);

    // --- zip --------------------------------------------------------------
    QFile::remove(outPath);
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file(&zip, outPath.toUtf8().constData(), 0)) {
        if (err)
            *err = QStringLiteral("could not create %1").arg(outPath);
        return false;
    }

    bool ok = true;
    // The mimetype entry must come first and be stored uncompressed.
    ok = ok && addEntry(&zip, "mimetype", QByteArrayLiteral("application/epub+zip"),
                        MZ_NO_COMPRESSION);
    ok = ok && addEntry(&zip, "META-INF/container.xml", container.toUtf8(),
                        MZ_DEFAULT_COMPRESSION);
    ok = ok && addEntry(&zip, "OEBPS/content.opf", opf.toUtf8(), MZ_DEFAULT_COMPRESSION);
    ok = ok && addEntry(&zip, "OEBPS/nav.xhtml", nav.toUtf8(), MZ_DEFAULT_COMPRESSION);
    ok = ok && addEntry(&zip, "OEBPS/style.css", css.toUtf8(), MZ_DEFAULT_COMPRESSION);
    for (int i = 0; ok && i < pages.size(); ++i) {
        const QByteArray xhtml = pageXhtml(pages.at(i), lang).toUtf8();
        ok = addEntry(&zip, ("OEBPS/" + pageFileName(i)).toUtf8().constData(), xhtml,
                      MZ_DEFAULT_COMPRESSION);
    }
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);

    if (!ok) {
        if (err)
            *err = QStringLiteral("zip write failed for %1").arg(outPath);
        QFile::remove(outPath);
        return false;
    }
    return true;
}

} // namespace ocr_epub
