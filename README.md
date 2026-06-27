# Spindle (C++ / Qt)

*English | [日本語](README.ja.md)*

Spindle is a native, cross-platform EPUB reader built with C++ and Qt.

Built with Qt Widgets + **Qt WebEngine** for the reading view, so each book is
rendered with full fidelity to its own CSS — including Japanese vertical writing
(縦書き) when the EPUB calls for it. EPUB resources are served to the engine
through a custom `epub://` URL scheme, so chapters load their stylesheets,
fonts, and images exactly as authored. Targets Windows, macOS, and Linux.

## Features

- **EPUB 2/3** loading (in-memory unzip via miniz; OPF / spine / metadata
  parsed with Qt XML).
- **Table of contents** — EPUB 3 `nav` document preferred, NCX fallback,
  nested entries.
- **Full-fidelity rendering** via Qt WebEngine + the `epub://` scheme:
  vertical & horizontal layouts, publisher CSS, embedded fonts and images.
- **Reading controls** — chapter navigation, font zoom, a font picker that can
  override the book's own fonts, light / sepia / dark themes, and a collapsible
  table-of-contents sidebar. The theme, window size, and translation view are
  restored on the next launch. The 8 most recently opened EPUBs can be shown in
  the left pane for quick reopening; after a history item is opened, the pane
  stays visible and switches back to the table of contents.
- **Full-text search** across the whole book with chapter-grouped snippets and
  in-page jump.
- **Highlights & notes** — select text to highlight with a 6-colour picker;
  colored marks render in place (works in vertical text); add/edit notes,
  list, and delete. Highlights are anchored to a document-order block and a
  character range within the chosen side (original *or* translation) and are
  shown only on that side (character-precise, never mirrored). The list marks
  each one ［原］/［訳］ and jumps to its block even when that side isn't shown.
  Saved next to the EPUB.
- **Markdown / JSON** highlight export & import.
- **Kindle Notebook** (HTML) import, matched onto chapter blocks.
- **Aozora Bunko XHTML** export of the current chapter (ruby preserved, images
  inlined as data URIs).
- **Local AI translation** via [Ollama](https://ollama.com) — whole-chapter
  modes (original / bilingual / translation-only) plus on-the-spot translation
  of a selection; configurable model, target language and endpoint. Translation
  paragraphs can be tinted a chosen color, and chapter translation runs up to two
  requests in parallel. Results are **cached per book and language** next to the
  EPUB, so re-reading is instant. When the book is already in the target language
  the view stays original (translation is disabled).
- **Local AI summaries** via Ollama — summarize the current chapter or selected
  text in the translation target language. Chapter summaries can be reopened
  from a sidecar file, regenerated on demand, rendered as Markdown, and saved at
  brief / standard / detailed levels with a summary model separate from the
  translation model. The book glossary is also applied to summaries when it
  matches the current target language.
- **Translation glossary** — an optional `<book>.glossary.json` fixes the
  target wording of chosen terms (names, jargon) for consistent translations
  and summaries. Only entries that appear in the current text are sent to the
  model.
- **Translated EPUB export** — generate a bilingual or translation-only `.epub`
  from the cache (missing paragraphs are translated on export); the output's
  language metadata is set to the target language.
- **XHTML source view** — toggle the current chapter between the rendered view
  and its raw markup.
- **Comfortable reading margins** and a collapsible sidebar.
- **Multiple books at once** — drag a `.epub` onto a window to open it in that
  window if it has no book yet, otherwise in a new one; command-line arguments
  and "open with" are also supported.

## Usage

Open an EPUB from **ファイル → EPUB を開く**, by dragging a `.epub` onto a
window (including onto the reading area), or by passing one or more paths on the
command line. A drop opens in the current window if it has no book yet, otherwise
in a new window, so you can read several at once.

| Action | Shortcut |
|--------|----------|
| Next chapter | `Space` / `→` |
| Previous chapter | `←` |
| Focus search | `Cmd` / `Ctrl` + `F` |

- **Sidebar:** toggle the left pane with **サイドバー**. Use **履歴** to show the
  recent EPUB list in that pane; opening a history item keeps the pane visible
  and switches it back to the table of contents.
- **Highlight:** select text → pick a colour from the popup (which also offers
  Copy, Translate, Summarize, and Search the web). Click an existing highlight
  to copy it, search it on the web, change its colour, edit its note, or delete it.
- **Translation:** choose **AI → 翻訳設定…**, choose a mode (original / bilingual /
  translation), set the model + target language, and press 再翻訳; or select
  text and choose **🌐 翻訳** to translate just that selection (the result popup
  closes on Escape or an outside click). Requires a running Ollama instance with
  the chosen model installed (default `qwen2.5`).
  - **Parallel translation:** chapter translation sends up to 2 requests at once,
    but the speedup only applies if the Ollama server allows parallelism. The
    macOS Ollama app often runs with `OLLAMA_NUM_PARALLEL=1` (and doesn't reliably
    pick up `launchctl setenv`). To enable it, quit the app and start the server
    yourself with the variable set:
    ```sh
    OLLAMA_NUM_PARALLEL=2 ollama serve
    ```
    Spindle's default endpoint `http://localhost:11434` then works unchanged. On a
    single GPU the gain is partial (both requests share it), not a full 2×.
- **Summary:** choose **AI → 現在の章を要約** or **要約 → 現在の章を要約**.
  Existing saved summaries for the current chapter, target language, and detail
  level are opened immediately; use **現在の章を再要約** to regenerate. The
  summary model is configured from **要約 → 設定…**.
- **Export a translated book:** **翻訳 → 対訳 EPUB を書き出し / 訳文 EPUB を書き出し**
  builds a new `.epub` from the cached translations (any not-yet-translated
  paragraphs are translated first, with a cancelable progress dialog). If Ollama
  stops without producing text, Spindle saves the exact source paragraph and
  request conditions to a diagnostic file next to the EPUB.
- **Source view:** the **XML** button shows the chapter's raw XHTML.

### Sidecar files

Per-book data is written next to the `.epub` (not in an app data directory):

For a book `Foo.epub`, files are named after the base name `Foo`:

| File | Contents |
|------|----------|
| `<book>.highlights.json` | highlights & notes |
| `<book>.<lang>.json` | translation cache for a target language (e.g. `Foo.ja.json`) |
| `<book>.glossary.json` | optional glossary (you create/edit this) |
| `<book>.summaries.json` | saved chapter summaries |

#### Glossary format

Create `<book>.glossary.json` next to the book (for `Changeling.epub`, the
file is `Changeling.glossary.json`). One file holds a single
source→target language pair and fixes how chosen terms are translated:

```json
{
  "source_lang": "la",
  "target_lang": "ja",
  "entries": [
    { "src": "Caesar", "dst": "カエサル", "note": "name" },
    { "src": "Gallia", "dst": "ガリア" }
  ]
}
```

| Field | Required | Meaning |
|-------|----------|---------|
| `source_lang` | no | the original language code (informational) |
| `target_lang` | yes | target language code (`ja`, `en`, …); the glossary applies only when this matches the language you are translating into |
| `entries` | yes | list of term mappings |
| `src` | yes | the term as it appears in the original text |
| `dst` | yes | the wording to use in the translation |
| `note` | no | a short hint passed to the model (e.g. `"name"`) |

Notes:
- The glossary is used only when `target_lang` matches the current translation
  / summary target (a missing `target_lang` applies to any target).
- Only entries whose `src` appears in the current paragraph, selection, chapter,
  or summary text are included in the Ollama prompt.
- Entries with an empty `src` or `dst` are ignored.
- Enforcement is via the prompt (the model is told to use these translations), so
  it is a strong preference, not a hard substitution — wording may still adapt to
  grammar/inflection. The file is read when the book is opened or the target
  language changes (edit it, then reopen the book or switch language to reload).

## Build

Requires **Qt 6** (Widgets, Network, Xml, WebEngineWidgets, WebChannel),
CMake ≥ 3.21, and a C++17 compiler.

### macOS (Homebrew)

```sh
brew install qt
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/spindle.app          # or: ./build/spindle.app/Contents/MacOS/spindle
```

### Linux

```sh
# Install qt6-base / qt6-xml / qt6-webengine via your distro, then:
cmake -S . -B build
cmake --build build
./build/spindle
```

### Windows

Install Qt 6 (online installer), then configure with the matching kit:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
cmake --build build --config Release
```

There are also convenience build scripts: `scripts/build.sh` (macOS / Linux)
and `scripts/build.ps1` (Windows).

## Packaging

Convenience scripts in [`scripts/`](scripts/README.md) build and bundle Spindle
for distribution (output in `dist/`):

```sh
./scripts/package-macos.sh                       # → Spindle-<ver>-macOS.dmg
./scripts/package-linux.sh                       # → AppImage + .deb / .tar.gz
pwsh scripts/package-windows.ps1 -QtPrefix ...   # → portable .zip (+ NSIS setup.exe)
pwsh scripts/package-windows-inno.ps1 -QtPrefix ... # → Inno Setup setup.exe
```

The deploy tools bundle the Qt WebEngine runtime so the app is self-contained.

> **Release builds need the official Qt.** Homebrew's Qt is great for
> development, but `macdeployqt` cannot fully bundle a Qt WebEngine app from it
> (the packaged `.app` can render blank). For a distributable macOS build,
> install Qt from the [official installer](https://www.qt.io/download-qt-installer)
> or `aqtinstall` and run `CMAKE_PREFIX_PATH=/path/to/Qt/6.x/macos ./scripts/package-macos.sh`.
> The same applies on Windows/Linux: use a complete Qt kit. macOS `.dmg`s are
> unsigned — `codesign`/notarize for wider distribution.

## Continuous integration / releases

[`.github/workflows/build.yml`](.github/workflows/build.yml) builds and packages
all three platforms on every push/PR using the official Qt (via `aqtinstall`):

- **Linux** → AppImage + `.deb` / `.tar.gz`
- **macOS** → `.dmg`
- **Windows** → portable `.zip` + NSIS / Inno Setup `setup.exe`

Artifacts are uploaded for every run. Push a tag like `v0.3.4` to also publish a
GitHub Release with all packages attached.

```sh
git tag v0.3.4 && git push origin v0.3.4
```

## Project layout

```
src/
├── epub/    ZIP (miniz) + EPUB parsing (OPF/spine/nav/NCX), path utilities
├── core/    chapter text, search, Kindle matcher, Markdown, Aozora export
├── model/   highlight / summary models + JSON persistence
├── web/     epub:// scheme handler, QWebChannel bridge
├── net/     Ollama translation / summary client
└── ui/      main window (QWebEngineView reader + sidebar/toolbar)
resources/   reader.js (injected), app icon, Qt resource file
packaging/   Linux .desktop entry
scripts/     build & packaging scripts (see scripts/README.md)
third_party/ vendored miniz
```

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the version history.

## License

Spindle is licensed under the [Apache License 2.0](LICENSE).

## Third-party

- [miniz](https://github.com/richgel999/miniz) (MIT) — vendored in `third_party/miniz`.
