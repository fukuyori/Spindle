# Changelog

All notable changes to Spindle (C++ / Qt) are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/),
and the project follows [Semantic Versioning](https://semver.org/).

## [0.2.2] - 2026-06-15

### Added
- **Font picker** — a toolbar font selector with an "適用" toggle overrides the
  book's own fonts with a chosen family (per all three views); the choice is
  remembered.
- **Translation text color** — pick the color of translation paragraphs in the
  translate dialog (theme-aware presets or a custom color); applies live, the
  original keeps the theme color.
- **Parallel translation** — up to 2 Ollama requests run concurrently instead of
  one at a time. (Requires the Ollama server to allow parallelism, e.g.
  `OLLAMA_NUM_PARALLEL=2`.)

### Changed
- **Glossary format** simplified to one source→target pair per file:
  `{ "source_lang", "target_lang", "entries": [ { "src", "dst", "note" } ] }`.
  The glossary applies only when `target_lang` matches the current target.

## [0.2.1] - 2026-06-15

### Fixed
- **Translation cache off-by-one.** When a re-translation run overlapped an
  in-flight request (e.g. pressing 再翻訳 while a translation was still running),
  a stale reply could be stored against the current block, shifting every
  translation by one paragraph. Each Ollama request now carries its own run,
  block index and source text, so replies are matched exactly and superseded
  runs are dropped. (Caches written by an earlier version stay shifted — delete
  the `<book>.epub.spindle.<lang>.json` sidecar and re-translate to rebuild it.)
- **再翻訳 now actually re-translates** the current chapter, ignoring the cache
  and overwriting it, instead of reusing cached results.

## [0.2.0] - 2026-06-15

### Changed
- **Highlight position model reworked.** Highlights are now anchored to a
  document-order block index plus a character `offset`/`length` within a chosen
  *side* (original or translation), instead of a chapter-wide character offset.
  Block ids are injected into the served HTML (`data-spindle-block`) so C++ and
  the page JavaScript share one coordinate system. Highlights now render
  correctly in all three views — character-precise on the side they were made,
  and as a whole-block tint on the other side — and a single highlight cannot
  span original and translation.
- **Highlights are stored next to the EPUB** as
  `<book>.epub.spindle.highlights.json` (was an app-data directory). Old
  app-data highlights are not migrated.
- **Translation cache moved next to the EPUB** as
  `<book>.epub.spindle.<lang>.json`.
- **Drag-and-drop** now opens the dropped EPUB in the current window when it has
  no book yet, otherwise in a new window — and works when dropping onto the
  reading area, not only the window chrome.
- Kindle Notebook import was rebuilt on the block model; the aggressive
  short-prefix fuzzy match (a source of wrong-location highlights) was removed.

### Added
- **Translation glossary** — an optional `<book>.epub.glossary.json` fixes the
  target wording of chosen terms per language and is injected into the
  translation prompt for consistent results.

## [0.1.3] - 2026-06-15

### Fixed
- Highlight positions no longer drift in the bilingual translation view.
  Inserted translation paragraphs are now excluded from the character-offset
  calculation, so marks stay aligned to the original text.

### Changed
- Documentation updated (features / usage); version bumped to 0.1.3.

## [0.1.2] - 2026-06-15

### Added
- **Translation cache** — translations are cached per book and target language
  and persisted to disk, so re-reading a chapter is instant.
- **Translated EPUB export** — generate a bilingual or translation-only `.epub`
  from the cache (paragraphs not yet translated are translated on export, with a
  cancelable progress dialog). The output's `dc:language` (and, for
  translation-only, each chapter's `lang`) is set to the target language.
- **Selection translation** — select text and choose **🌐 翻訳** to translate
  just that passage; the result popup closes on Escape or an outside click.
- **Multiple books at once** — each EPUB opens in its own window; multiple
  command-line files and macOS "open with" / Dock-drop are supported.
- **Drag-and-drop** opens the dropped EPUB in a new window.
- **Collapsible table-of-contents sidebar** (☰ 目次 toolbar toggle).
- **XHTML source view** toggle (`</> XML`) to inspect the chapter's raw markup.
- **Comfortable left/right reading margins.**

### Changed
- `scripts/package-macos.sh` now auto-prefers an official Qt (falling back to
  Homebrew with a warning), does a clean build to avoid mixing Qt versions,
  handles the AGL framework removed from newer macOS SDKs, and quiets the
  resulting benign deploy warning.

## [0.1.1] - 2026-06-15

### Fixed
- Packaged macOS app rendered chapters blank: the Qt WebEngine helper process
  could not resolve the bundled frameworks (`QtWebChannel`, `QtPositioning`,
  …). The packaging script now mirrors every bundled framework/dylib into the
  helper so the renderer starts.

### Added
- Application icon (macOS `.icns`, Windows `.ico`, runtime window icon),
  generated from `resources/spindle.svg`.

## [0.1.0] - 2026-06-15

Initial release of Spindle, a native EPUB reader built with C++ and Qt.

### Added
- EPUB 2/3 loading (vendored miniz unzip; OPF / spine / metadata via Qt XML).
- Full-fidelity chapter rendering with Qt WebEngine and a custom `epub://` URL
  scheme — vertical & horizontal writing, publisher CSS, fonts and images.
- Table of contents (EPUB 3 `nav` preferred, NCX fallback).
- Full-text search with chapter-grouped snippets and in-page jump.
- Highlights & notes with a 6-colour picker, in-page marks, and per-book JSON
  persistence.
- Markdown / JSON highlight export & import.
- Kindle Notebook (HTML) import with multi-stage text matching.
- Aozora Bunko XHTML export of the current chapter.
- Local AI translation via Ollama (original / bilingual / translation-only).
- Light / sepia / dark themes and font zoom.
- Cross-platform build & packaging scripts (macOS `.dmg`, Linux AppImage /
  `.deb`, Windows `.zip` / NSIS installer) and a GitHub Actions release workflow.

[0.1.3]: https://github.com/fukuyori/Spindle/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/fukuyori/Spindle/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/fukuyori/Spindle/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/fukuyori/Spindle/releases/tag/v0.1.0
