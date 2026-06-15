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

// Return the chapter XHTML with data-spindle-block="N" added to each leaf block.
// Returns an empty string if the XHTML won't parse (caller serves the original).
QString injectBlockIds(const QString &xhtml);

} // namespace block_index
