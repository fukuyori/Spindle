#pragma once

#include <QString>

// Path helpers mirroring the original Spindle (main.ts) so chapter/asset
// resolution stays byte-for-byte compatible with the TypeScript version.
namespace path_util {

// Collapse "." and ".." segments. Input/output use '/' separators.
QString normalize(const QString &path);

// Directory portion of a zip-internal path ("" for top level).
QString dirname(const QString &path);

// Resolve an href relative to a base *file* path (base's directory is used).
QString resolve(const QString &basePath, const QString &href);

// Drop a trailing #fragment.
QString stripHash(const QString &path);

// The #fragment of a path, or "" when absent.
QString hashOf(const QString &path);

bool isExternalUrl(const QString &url);

} // namespace path_util
