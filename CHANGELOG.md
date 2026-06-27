# Changelog

All notable changes to Spindle (C++ / Qt) are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/),
and the project follows [Semantic Versioning](https://semver.org/).

## [0.3.4] - 2026-06-27

### Changed
- **Recent EPUB history** can now be shown in the left pane, switches back to
  the table of contents after a history item is opened, and can be reopened from
  the toolbar or File menu.
- **Glossary prompting** now sends only entries that appear in the current text,
  preventing short labels such as `Notes` from being overwhelmed by unrelated
  glossary terms.

### Fixed
- **Translated EPUB export** now disables Ollama thinking output for translation
  requests, reports no-token Ollama stops with response metadata, saves the full
  failing paragraph and request conditions to a diagnostic file, and does not
  send export translation requests when the book language already matches the
  target language.

## [0.3.3] - 2026-06-27

### Changed
- **Summary generation** now applies `<book>.glossary.json` when its
  `target_lang` matches the current summary target, matching translation
  behavior.

## [0.3.2] - 2026-06-26

### Added
- **Summary translation button** — summary dialogs now include a Translate
  action that translates the displayed summary into the current translation
  target language.
- **Apache-2.0 project license** — Spindle now ships with an Apache License 2.0
  `LICENSE` file, and packages include both the project license and the vendored
  miniz MIT license.

### Changed
- **Summary language prompting** now passes the target language name, UI label,
  and ISO code to Ollama to make target-language summaries more reliable.
- **macOS packaging** now signs the app with Developer ID, submits the `.dmg`
  through `notarytool`, staples the ticket, and includes license files in the
  disk image.

### Fixed
- **Stale CMake build directories** are automatically refreshed when a
  `CMakeCache.txt` was generated from another source checkout path.
- **macOS AGL SDK compatibility** uses a more stable link-time stub for newer
  SDKs that no longer ship the legacy AGL framework.

## [0.3.1] - 2026-06-26

### Added
- **Summaries via Ollama** — summarize the current chapter from the Summary
  menu or compact AI toolbar button, or summarize selected text from the selection
  context menu. Summary results open in a resizable, scrollable dialog with a
  copy button and render Markdown returned by the model.
- **Summary detail setting** — choose brief, standard, or detailed summaries
  from the Summary menu; the choice is saved for the next run.
- **Separate summary model setting** — the Summary menu now has its own model
  setting, so changing the summary model does not trigger retranslation.
- **Cleaner reader toolbar** — duplicate translation and summary toolbar items
  are grouped under a compact AI button, while the native menu bar remains
  available on macOS.
- **Inno Setup packaging script** — Windows installers can now be built with
  `scripts/package-windows-inno.ps1`.
- **Chapter summary sidecars** — chapter summaries can be saved and reopened
  from `<book>.summaries.json`; saved summaries are keyed by chapter, target
  language, and summary detail. Running chapter summary now opens an existing
  saved summary when available, with an explicit re-create action for regenerating it.
- **Recent EPUB history** — the File menu now keeps the last 8 successfully
  opened EPUB files for quick reopening.

## [0.3.0] - 2026-06-26

### Added
- **About dialog** — the Help menu now shows the current Spindle version.

### Fixed
- **Windows local builds run from a normal PowerShell prompt** — `build.ps1`
  now detects Visual Studio's bundled CMake, loads the MSVC developer
  environment, finds the local Qt kit, and refreshes stale CMake generator
  caches when needed.
- **Windows build output starts directly** — `build.ps1` now runs
  `windeployqt` after compiling so `build\spindle.exe` has the Qt WebEngine
  runtime beside it.

## [0.2.8] - 2026-06-16

### Added
- **Search the web from highlights** — clicking an existing highlight now offers
  「Web で検索」 in the highlight menu, matching the selection menu.

### Fixed
- **Highlight list swatches align to the top** — colour chips in the highlight
  list now stay aligned with the first line of multi-line entries.

## [0.2.7] - 2026-06-16

### Added
- **Search the web** — the selection menu has a 「Web で検索」 item that opens the
  selected text in the default browser's search.

### Changed
- **Note editor wraps text** — the note add/edit dialog now word-wraps long
  lines instead of scrolling horizontally.

## [0.2.6] - 2026-06-16

### Added
- **Copy to clipboard** — the selection menu and the highlight menu both have a
  「コピー」 item that copies the selected / highlighted text.

### Fixed
- **Multiple highlights in one block** no longer render merged or shifted.
  Highlight offsets are now measured in the block's plain-text coordinate (text
  inside existing marks is counted), so creation and rendering agree.
  (Highlights saved by an earlier version may still be misplaced — re-create
  them.)

## [0.2.5] - 2026-06-16

### Changed
- **Highlights are no longer mirrored across sides.** A highlight is shown only
  on the side it was made on (original or translation), character-precise; the
  earlier whole-block tint on the opposite side was removed.
- **Highlight list** marks each entry with ［原］ / ［訳］ (original / translation
  side), and clicking an entry jumps to the highlight's block — landing on the
  original block or its translation paragraph, whichever the current view shows,
  even when the highlight's own side isn't displayed.

## [0.2.4] - 2026-06-16

### Added
- **Session restore** — the theme, window size/position, and translation view
  used last time are restored on startup.

### Changed
- **No self-translation** — when the book's own language (`dc:language`) matches
  the translation target, the view is locked to the original and the mode can't
  be changed in the translate dialog (re-enabled once a different target is
  chosen).
- **Windows: no console window** — the app is built as a GUI (WinMain)
  executable, so no terminal window appears at launch.
- **Reading pane gets keyboard focus** on launch and after each chapter load
  (instead of the search box), so scrolling/paging works immediately.

## [0.2.3] - 2026-06-15

### Changed
- **Sidecar file names shortened** — the `.epub` and `.spindle` parts were
  dropped, so for `Foo.epub` the files are now `Foo.highlights.json`,
  `Foo.<lang>.json` (e.g. `Foo.ja.json`), and `Foo.glossary.json`. Old-named
  files are not migrated automatically; rename them to the new scheme to keep
  existing highlights, caches, and glossaries.

### Fixed
- **Linux AppImage build** — Qt WebEngine pulls in QtPositioning, whose NMEA
  position plugin links Qt SerialPort and broke `linuxdeploy`. The position
  plugins (geolocation, unused by a reader) are now excluded from the AppImage;
  `libQt6Positioning` itself is still bundled as a WebEngine dependency.

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
  `.deb`, Windows `.zip` / NSIS / Inno Setup installers) and a GitHub Actions
  release workflow.

[0.3.4]: https://github.com/fukuyori/Spindle/compare/v0.3.3...v0.3.4
[0.3.3]: https://github.com/fukuyori/Spindle/compare/v0.3.2...v0.3.3
[0.3.2]: https://github.com/fukuyori/Spindle/compare/v0.3.1...v0.3.2
[0.3.1]: https://github.com/fukuyori/Spindle/compare/v0.3.0...v0.3.1
[0.3.0]: https://github.com/fukuyori/Spindle/compare/v0.2.8...v0.3.0
[0.2.8]: https://github.com/fukuyori/Spindle/compare/v0.2.7...v0.2.8
[0.2.7]: https://github.com/fukuyori/Spindle/compare/v0.2.6...v0.2.7
[0.2.6]: https://github.com/fukuyori/Spindle/compare/v0.2.5...v0.2.6
[0.2.5]: https://github.com/fukuyori/Spindle/compare/v0.2.4...v0.2.5
[0.2.4]: https://github.com/fukuyori/Spindle/compare/v0.2.3...v0.2.4
[0.2.3]: https://github.com/fukuyori/Spindle/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/fukuyori/Spindle/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/fukuyori/Spindle/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/fukuyori/Spindle/compare/v0.1.3...v0.2.0
[0.1.3]: https://github.com/fukuyori/Spindle/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/fukuyori/Spindle/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/fukuyori/Spindle/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/fukuyori/Spindle/releases/tag/v0.1.0
