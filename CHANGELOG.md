# Changelog

All notable changes to Spindle (C++ / Qt) are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/),
and the project follows [Semantic Versioning](https://semver.org/).

## [0.8.0] - 2026-09-06

### Added
- **Zoom for fixed-layout pages.** A−/A+ and Ctrl+mouse wheel now enlarge
  pre-paginated pages. Page zoom never could: reader.js refits such a page to
  the CSS viewport, which shrinks by exactly the zoom factor and cancels it
  out. The page is now scaled by the app instead, and past fit-to-window it can
  be dragged around.
- **The spread is one sheet.** A fixed-layout spread is laid out as a single
  canvas rather than two independently fitted halves: zooming scales both by
  the same factor and panning moves both together, so the join at the spine
  travels with the pages instead of being pinned to the middle of the window.
  Each page turn starts fitted and centred again.
- **Legibility adjustments for scanned pages**, in View ▸ Display Adjustments:
  - **Darken text on image pages** — a levels curve anchored at white, so the
    paper stays paper while the ink gains weight. (CSS `contrast()` pivots on
    mid grey, which pushes the pale half-tones that give a character its body
    up to white and leaves the strokes thinner, not clearer.)
  - **Sharpen edges on image pages** — an unsharp mask whose radius follows the
    display scale, so it lands at the same size on screen at any zoom.
  - **Even out density page by page** — measures each page's paper and ink and
    maps them onto shared targets, so a scan whose pages vary in density reads
    evenly. Applies to fixed-layout pages and single-image chapters only;
    ordinary chapters render real text and are left alone.
- **`-Sign` for the Windows packagers.** `package-windows.ps1` and
  `package-windows-inno.ps1` apply an Authenticode signature to the
  executable, the installer and the uninstaller. The identity comes from
  `CODESIGN_CERT` (a .pfx path, a SHA1 thumbprint or a subject name);
  third-party binaries keep the signatures they arrived with.

### Changed
- **Package filenames are uniform** across platforms:
  `Spindle-<version>-<os>-<arch>.<ext>`, with the architecture read from the
  binary that was actually built rather than from the host.
- **View ▸ Adjust Brightness… is now View ▸ Display Adjustments…**, since it
  covers more than brightness.

### Fixed
- **A page of a spread could come back cut in half** after the spread was
  switched off and on again. The page's own size was measured from the DOM
  before its image had dimensions, and the view was then sized to that
  measurement; a size taken that early is now never used.

### Removed
- **NSIS packaging.** The Windows installer is built with Inno Setup only.

## [0.7.3] - 2026-07-29

### Removed
- **OCR text extraction** (introduced in 0.7.2). The in-app feature — the
  Translation-menu action, the `<book>.ocr.md` / `<book>.ocr.epub` outputs,
  and the Ollama vision-model plumbing behind them — was removed.

## [0.7.2] - 2026-07-28

### Added
- **OCR text extraction for image-based books.** Translation menu (and the AI
  toolbar button) gained **Extract Text from Image Pages (OCR)…**, which ran
  every image page of a fixed-layout EPUB (magazines, comics) through an
  Ollama vision model (default `glm-ocr:q8_0`, changeable per run, saved as
  `ocr/model`) and wrote the text to a Markdown sidecar `<book>.ocr.md` in
  spine order. Replies collapsing into endless repetition were detected and
  retried once with a fallback model (`ocr/retryModel`, default
  `glm-ocr:bf16`); still-failing pages kept an `OCR_FAILED` marker. Canceling
  saved the pages finished so far, and consecutive failures at the start
  (server down, missing model) aborted early. With existing results the run
  offered **resume** (reuse finished pages, redo failed/unprocessed ones)
  instead of overwriting silently, and a complete run also generated
  **`<book>.ocr.epub`** — a reflowable text EPUB (one chapter per page,
  vertical right-to-left when the source book is) usable with search,
  translation, summaries, and read-aloud.

## [0.7.1] - 2026-07-28

### Fixed
- **Glossary terms now reliably reach the model.** The glossary is sent inside
  the user-turn task statement as well as the system prompt (translation-tuned
  models weigh the former), reworded per task (translation vs. summary), and
  emitted as one terse line instead of an instruction paragraph. The term list
  is capped relative to the text length so a large glossary can no longer
  drown a short paragraph and derail the translation.
- **Glossary matching misses fewer occurrences.** Terms whose edges are CJK
  characters match inside unspaced Japanese/Chinese prose (the word-boundary
  guard no longer applies there), and English entries also match simple
  plural/possessive forms ("Caesars", "Caesar's").
- **Blocks that are exactly one glossary term skip the model.** Headings such
  as a bare person's name are rendered directly with the glossary target — in
  page translation, selection translation, and EPUB export — instead of
  risking a model round-trip mangling the mandated form.

## [0.7.0] - 2026-07-19

### Added
- **Bilingual UI (Japanese / English).** All menu, toolbar, dialog, and status
  strings are now translatable. The UI language follows the system locale
  (Japanese system → Japanese UI, anything else → English) and can be forced
  via the new 表示 → 言語 / Language menu (applied after restart). Source
  strings remain Japanese; the English catalog lives in `i18n/spindle_en.ts`
  (refresh with the `update_translations` CMake target, compiled into the
  binary by lrelease at build time).

## [0.6.6] - 2026-07-19

### Fixed
- **Facing pages meet cleanly at the spine.** Each page of a spread now
  edge-aligns toward the center instead of centering in its own half, the 2px
  seam between the two views is gone, and the page-fit measurement no longer
  loses the width of the pre-fit scrollbar — which had left a theme-colored
  strip at the spine.
- **Page turns can no longer leave pages unscaled.** Navigating quickly could
  occasionally skip the reader-script injection, showing the raw page at
  natural size with scrollbars. The injected-script collections are now only
  rewritten when their content actually changes, and every finished load
  verifies the fixed-layout fit, re-injecting the script if it never ran.
- **Backward navigation keeps the established spread pairing.** Spread pairs
  are anchored at the front of the book, so stepping back from a lone final
  page rejoins the canonical pairs instead of shifting every spread by one.
- **Arrow keys follow the reading direction on fixed-layout pages.** In
  right-bound books ← advances and → goes back, matching the edge-click
  mapping.

## [0.6.5] - 2026-07-18

### Changed
- **The selection-translation popup closes on any click outside it.** Clicks
  landing on the reading view (a native child window invisible to the popup's
  mouse grab) previously left the popup open; only clicking outside the app
  window or pressing Escape dismissed it.

### Fixed
- **Selection translation works in fixed-layout books.** The facing-page
  spread view was created after the web channel was wired up, so selections on
  the companion page could never reach the app — no menu appeared. The
  companion page now connects to the bridge; selecting text there opens the
  text-action menu (翻訳 / 要約 / コピー / Web で検索). Highlight colors and
  notes are offered only on the primary page, where the selection can be
  anchored to the current chapter.

## [0.6.4] - 2026-07-18

### Fixed
- **Selections that cannot be anchored to a paragraph block still open the
  menu.** Fixed-layout overlay text, SVG text, or chapters whose markup
  prevented block-id injection now fall back to the text-action menu instead
  of silently doing nothing.

## [0.6.3] - 2026-07-18

### Added
- **Fixed-layout EPUBs can be read as facing-page spreads.** The new
  表示 → 固定レイアウトを見開き表示 option is enabled by default and honors
  the book's left/right page-progression direction plus `page-spread-*`
  placement metadata. Clicking the outer 15% of either page also turns the
  spread in the corresponding physical direction. The first spine page is
  always treated as a cover and shown alone. Binding direction can be set to
  automatic, right-bound, or left-bound; automatic uses right binding for
  vertical writing and otherwise follows the EPUB.

### Fixed
- **Fixed-layout arrow navigation turns the page on the first key press.**
  Left/Right are intercepted before Chromium can use the first press to
  horizontally scroll an oversized page. The complete fixed page is fitted to
  the reading pane while retaining its authored aspect ratio. Books that omit
  `rendition:layout` are also recognized when every spine document is exactly
  one image page with no surrounding text.
- **Single-image chapters now always open fitted to the reading pane.** The
  source image's aspect ratio determines whether its width or height meets the
  window edge, independently of the retained text zoom. Image zoom and mouse
  drag panning now use the image's actual bounds, avoiding scrollable empty
  space on the non-overflowing axis.
- **Book-search results now jump to the selected occurrence.** Repeated matches
  in the same chapter use their stored text offsets instead of always opening
  Chromium's first match. Pressing Enter in the search field opens the first
  result immediately.

## [0.6.2] - 2026-07-15

### Added
- **Configurable line wrapping.** 表示 > 折り返し設定… can keep text fitted to
  the window or limit each line to a chosen 10–120 character measure (initially
  40). The setting is saved across launches and also works with vertical writing.

## [0.6.1] - 2026-07-14

### Fixed
- **Glossary extraction no longer times out when a model keeps generating
  malformed JSON.** Responses now use a strict schema and a bounded output
  length, so a runaway model cannot occupy Ollama until the five-minute
  request timeout.

## [0.6.0] - 2026-07-14

### Added
- **Glossary generation (用語集を生成).** 翻訳 > 用語集を生成… extracts proper
  nouns and recurring terms from the current chapter or the whole book with
  Ollama (JSON-forced output, summary model) and writes their target-language
  wordings into `<book>.glossary.json`, merging with any existing entries
  (existing ones win). Extracted terms are kept only if they actually occur in
  the scanned text; progress dialog with cancel (cancel keeps the terms found
  so far), and the updated glossary applies to translations and summaries
  immediately.

## [0.5.2] - 2026-07-12

### Added
- **Audio-file export (章を音声ファイルへ書き出し).** The 読み上げ menu can
  render the current chapter to a single audio file using the same voices and
  原文/訳文 rule as playback, with a progress dialog and cancel. Blocks
  synthesized by different engines/sample rates (VOICEVOX 24 kHz, Piper
  22.05 kHz, OS voices) are resampled into one stream with a 300 ms pause
  between paragraphs. OS voices are used via QTextToSpeech's synthesize
  capability where the engine supports it; VOICEVOX and Piper always support
  export. WAV (16-bit mono) is written directly; **MP3 / M4A** are encoded
  through ffmpeg (auto-detected on PATH, or set 読み上げ > 音声設定 >
  ffmpeg) — MP3 at ~130 kbps VBR (`-q:a 4`), M4A as 96 kbps AAC.

## [0.5.1] - 2026-07-12

### Added
- **Local AI voices for read-aloud: VOICEVOX and Piper.** Besides the OS
  voices, the voice pickers list the speakers of a running
  [VOICEVOX](https://voicevox.hiroshiba.jp) server (`VOICEVOX: 四国めたん
  （ノーマル）`, Japanese) and the `.onnx` voice models of a local
  [Piper](https://github.com/rhasspy/piper) install (`Piper:
  en_US-lessac-medium`, multi-language). Picking a prefixed voice routes that
  language to that engine — so e.g. Japanese can be spoken by VOICEVOX while
  English in the same book falls back to an OS voice. The VOICEVOX endpoint
  (default `http://localhost:50021`) and the Piper executable / voices folder
  are set in 読み上げ > 音声設定. Speech rate maps to VOICEVOX `speedScale` /
  Piper `--length_scale`; synthesized audio plays through Qt Multimedia (the
  engines are built only when that module is present).

## [0.5.0] - 2026-07-12

### Added
- **Read-aloud (読み上げ).** The current chapter can be spoken block by block
  with the OS speech voices (Qt TextToSpeech; WinRT/SAPI on Windows,
  AVSpeechSynthesizer on macOS, speech-dispatcher on Linux). Reading follows
  the translation display mode — 原文 and 対訳 read the source text, 訳文
  reads the translation (falling back to the source where none is cached
  yet) — with the voice matched to the spoken language. The spoken paragraph
  is tinted and auto-scrolled into view,
  `<ruby>` text is spoken as its `<rt>` reading instead of base + furigana
  concatenated, reading starts from the paragraph currently on screen, and the
  chapter end can auto-advance into the next chapter (読み上げ menu toggle).
  Speech rate and a per-language voice are configurable in 読み上げ > 音声設定;
  play/pause/stop live on the toolbar and in the new 読み上げ menu
  (Ctrl+Shift+S toggles). Builds without Qt TextToSpeech still compile, with
  the actions reporting that no speech engine is available.

## [0.4.5] - 2026-07-12

### Added
- **Bare-text chapters are now translatable.** Some EPUBs (often converted
  ones, e.g. Aozora/Gutenberg conversions) carry chapter prose as bare text
  directly inside `<div>`/`<body>`, separated by `<br/><br/>` instead of
  `<p>` elements. Such text had no block structure, so translation,
  highlighting, search jumps, and Kindle-note matching only saw the headings.
  Chapters are now normalized on load: each run of bare inline text becomes a
  synthetic paragraph (split at 2+ consecutive `<br>`, spacing preserved),
  consistently across the reading view, sidecar caches, and translated-EPUB
  export.
- **Wrong-language translations are caught.** When the model answers in a
  language whose script clearly doesn't match the target (e.g. Chinese
  returned for a Japanese target), the request is retried once with a
  stronger instruction; if it still doesn't comply the paragraph shows an
  error instead of caching the wrong text.

### Fixed
- **Chinese ⇄ Japanese translation reliability.** The translation prompt
  told the model to return text "already in the target language" unchanged,
  which made kanji-heavy Japanese pass through untranslated with a Chinese
  target (and vice versa) — and the untranslated result was then cached.
  The prompt now states the task in the user turn in a form that
  translation-tuned models (e.g. TranslateGemma) also follow, and no longer
  offers the pass-through escape hatch.
- **Reasoning models' chain of thought leaked into translations.** Content
  ending with a `</think>` marker (e.g. Qwen3 with thinking nominally
  disabled) now has everything before the marker stripped.

## [0.4.4] - 2026-07-07

### Added
- **Image-only chapters fit the window.** A chapter consisting of a single
  image (manga-style EPUBs, including fixed-layout pages that position the
  image with inline `position:absolute` and fixed pixel sizes) now scales to
  fit the reading pane, centered, instead of rendering at its native size
  inside the text margins.
- **Image chapters zoom past 100%.** The A−/A+ buttons and Ctrl+mouse wheel
  now enlarge an image chapter beyond fit-to-window (up to the same 200%
  ceiling as text). Ctrl+wheel is routed through the app's own zoom instead
  of Chromium's page zoom, so both inputs share one zoom state.
- **Drag to pan.** When an image chapter is zoomed beyond the window, the
  image can be dragged with the mouse to move the visible area (grab cursor;
  the native image drag-out is suppressed while panning).

### Fixed
- **Black flash on startup.** The first window briefly painted black while
  Chromium produced its first frame. The reading pane now keeps a
  theme-colored placeholder on screen and swaps in the web view only when
  the first page has actually finished loading.

## [0.4.3] - 2026-07-07

### Fixed
- **Right-click "Copy image" / "Save image" in the reading pane now works.**
  Chromium's own copy/download machinery never completed for images served
  over the app's custom `epub://` scheme, so both menu entries silently did
  nothing. Image right-clicks now show a dedicated 画像をコピー /
  名前を付けて画像を保存... menu that reads the image straight from the EPUB's
  own bytes instead.

### Changed
- **`package-windows.ps1` / `package-windows-inno.ps1` no longer build.**
  They previously re-ran `build.ps1` every time, which could relink
  `spindle.exe` and silently wipe out a code signature applied to the build
  output beforehand. Both scripts now only stage and archive whatever already
  exists under `build\` (erroring out if it's missing), so run `build.ps1` —
  and sign the exe, if you sign releases — first.

## [0.4.2] - 2026-07-06

### Added
- **Toolbar icons** — 開く・サイドバー・履歴・前/次の章・文字サイズ (A−/A+) are
  now theme icons (monochrome symbolic set) instead of text buttons. Where the
  icon theme lacks a glyph the buttons fall back to Qt's standard icons, or to
  the original text for the zoom pair. AI / 原文 / 対訳 / 訳文 / XML stay text.

### Changed
- **Translations keep the original's formatting.** The in-app bilingual /
  translation views and the bilingual EPUB export now clone the source block
  (tag, classes, inline style) instead of inserting a plain paragraph, so
  headings translate as headings and styled paragraphs keep their look. The
  translation block also carries the target language's `lang` attribute.
- **Bilingual spacing** — in 対訳 view a translation hugs its original
  paragraph and a clear gap separates the pair from the next original
  (logical margins, so vertical writing gets the same rhythm);
  訳文のみ view keeps the book's own block spacing.

### Fixed
- **Brightness (and theme) changes now apply to the page being read.** A served
  chapter embeds the theme CSS, and the pre-paint theme script could race the
  parser and create a second `style#__spindle_theme`; live updates then edited
  the losing duplicate, so 明るさ調整 sliders (and sometimes theme switches)
  had no visible effect until the next chapter load. The style updater now
  dedupes and re-appends the element so the latest CSS always wins.
- **Ollama errors now show the server's own message** — e.g.
  `model "qwen2.5" not found` instead of the bare HTTP status line
  ("server replied: Not Found"), so a missing model is diagnosable
  from the error popup.
- **Startup warning about the desktop file name** — `setDesktopFileName` is
  now passed `spindle` without the `.desktop` suffix, as Qt expects.

## [0.4.1] - 2026-07-06

### Added
- **Toolbar translation-view switcher** — 原文 / 対訳 / 訳文 buttons on the
  toolbar switch the view (and kick off translation) without opening the
  translate dialog. When the book is already in the target language the
  buttons lock to 原文, with a tooltip explaining why.

### Changed
- **Theme and font moved into the 表示 menu** — the theme is now picked
  directly (ライト / セピア / ダーク radio entries) instead of a cycling toolbar
  button, and the body font is chosen via the native font dialog
  (表示 > フォント…) with a separate 「フォントを本文に適用」 toggle. The
  toolbar keeps A− / A+ / XML.
- **README build instructions** now give the exact Ubuntu/Debian package list,
  and `build.sh` prints an install hint when Qt6 WebEngine development files
  are missing.

### Fixed
- **`package-linux.sh` no longer deletes position plugins from the Qt kit
  itself** — with a system Qt this failed with permission errors (and with
  root privileges would have damaged the system Qt install), aborting the
  AppImage build. The plugins are now hidden from linuxdeploy via a filtered
  symlink copy of the plugins dir handed over through a `$QMAKE` wrapper.
- **Linux desktop integration** — the window icon is set at runtime, the
  desktop file name is registered (fixes taskbar icon association on
  Wayland), a 256×256 icon is installed, and the .deb refreshes the icon
  cache and desktop database on install/removal.

## [0.4.0] - 2026-07-04

### Added
- **Quit shortcut** — Ctrl+Q (ファイル > 終了) closes every window, persisting
  window geometry, caches, and the last-read chapter on the way out.

### Changed
- **Faster startup** — the window appears immediately; Chromium (Qt WebEngine)
  now initializes after the window is shown, and books opened from the command
  line load after the frame is up.
- **Flicker-free chapter navigation** — the reader theme CSS is embedded into
  served chapter documents so the first painted frame is already styled, view
  repaints are held during navigation, and the view widget's palette follows
  the theme.
- **Faster chapter navigation** — served chapter documents (block ids + theme
  CSS) are cached per book, and leaf-block enumeration is a single pass.
- **Background text indexing** — chapter texts are built on a worker thread
  right after a book opens, so the first search, chapter summary, or Kindle
  import no longer freezes the UI; search also stops re-lowercasing the whole
  book on every keystroke.
- **Translated-EPUB export** now runs two translation requests in parallel with
  a cancellable progress dialog (no nested event loop); cancelling keeps the
  paragraphs already translated in the cache.

### Fixed
- **Ollama requests now time out** (5 minutes) instead of leaving "翻訳中…"
  stuck forever when the server hangs.
- **Stale AI replies** from superseded selection-translation and summary
  requests no longer overwrite newer results.
- **Atomic saves** — highlights, translation caches, and all exports are
  written via temp-file replace so a crash can no longer corrupt them, and
  write failures are reported instead of ignored.

### Security
- **Navigation policy** — the reading pane only navigates within the current
  book; external links open in the system browser and everything else is
  refused.
- **Book scripting disabled** — served chapters are sanitized (`<script>`,
  event-handler attributes, and `javascript:` URLs removed), and the web
  channel bridge now lives in an isolated JavaScript world out of page reach.
- **Zip-bomb protection** — archive entries that decompress beyond 512 MB are
  refused.

## [0.3.5] - 2026-06-27

### Changed
- **Theme brightness controls** now let each theme store separate brightness
  adjustments for the page background, original text, and translated text.
- **Recent EPUB history** now remembers the last-opened chapter for each book
  and resumes there next time.

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

[0.7.3]: https://github.com/fukuyori/Spindle/compare/v0.7.2...v0.7.3
[0.7.2]: https://github.com/fukuyori/Spindle/compare/v0.7.1...v0.7.2
[0.7.1]: https://github.com/fukuyori/Spindle/compare/v0.7.0...v0.7.1
[0.7.0]: https://github.com/fukuyori/Spindle/compare/v0.6.6...v0.7.0
[0.6.6]: https://github.com/fukuyori/Spindle/compare/v0.6.5...v0.6.6
[0.6.5]: https://github.com/fukuyori/Spindle/compare/v0.6.4...v0.6.5
[0.6.4]: https://github.com/fukuyori/Spindle/compare/v0.6.3...v0.6.4
[0.6.3]: https://github.com/fukuyori/Spindle/compare/v0.6.2...v0.6.3
[0.6.2]: https://github.com/fukuyori/Spindle/compare/v0.6.1...v0.6.2
[0.6.1]: https://github.com/fukuyori/Spindle/compare/v0.6.0...v0.6.1
[0.6.0]: https://github.com/fukuyori/Spindle/compare/v0.5.2...v0.6.0
[0.5.2]: https://github.com/fukuyori/Spindle/compare/v0.5.1...v0.5.2
[0.5.1]: https://github.com/fukuyori/Spindle/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/fukuyori/Spindle/compare/v0.4.5...v0.5.0
[0.4.5]: https://github.com/fukuyori/Spindle/compare/v0.4.4...v0.4.5
[0.4.4]: https://github.com/fukuyori/Spindle/compare/v0.4.3...v0.4.4
[0.4.3]: https://github.com/fukuyori/Spindle/compare/v0.4.2...v0.4.3
[0.4.2]: https://github.com/fukuyori/Spindle/compare/v0.4.1...v0.4.2
[0.4.1]: https://github.com/fukuyori/Spindle/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/fukuyori/Spindle/compare/v0.3.5...v0.4.0
[0.3.5]: https://github.com/fukuyori/Spindle/compare/v0.3.4...v0.3.5
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
