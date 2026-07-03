#pragma once

#include <QString>
#include <QVector>

// Shared block enumeration: the single authority for "document-order block
// number" used by both highlight anchoring (data-spindle-block injected into the
// served HTML) and Kindle import matching. A block is a leaf block element (one
// of p/h1..h6/li/blockquote/figcaption/dd/dt with no descendant block), counted
// 0-based in document order. JS reads the injected numbers and never recomputes
// them, so creation, rendering, and import all share one coordinate system.
namespace block_index {

struct BlockInfo {
    int index = 0;  // document-order block number (== data-spindle-block)
    QString text;   // block text content (script/style excluded, nbsp -> space)
};

// Leaf blocks of the chapter, in document order. Empty if the XHTML won't parse.
QVector<BlockInfo> enumerateBlocks(const QString &xhtml);

// Body text + leaf blocks extracted from a single parse of the chapter XHTML
// (enumerateBlocks and the body textContent share one QDomDocument). `ok` is
// false when the markup won't parse at all.
struct ChapterScan {
    bool ok = false;
    QString bodyText; // textContent of <body> (script/style skipped, nbsp -> space)
    QVector<BlockInfo> blocks;
};
ChapterScan scanChapter(const QString &xhtml);

// Return the chapter XHTML with data-spindle-block="N" added to each leaf block,
// and active content stripped (<script>, on* handlers, javascript: URLs — book
// scripting is not supported). When themeCss is non-empty a
// <style id="__spindle_theme"> element carrying it is embedded (end of <head>),
// so the served document paints with the reader theme on its very first frame.
// Returns an empty string if the XHTML won't parse (the caller must then scrub
// the original by other means before serving it).
QString injectBlockIds(const QString &xhtml, const QString &themeCss = QString());

} // namespace block_index
