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
- **Reading controls** — chapter navigation, font zoom, light / sepia / dark
  themes, and a collapsible table-of-contents sidebar.
- **Full-text search** across the whole book with chapter-grouped snippets and
  in-page jump.
- **Highlights & notes** — select text to highlight with a 6-colour picker;
  colored marks render in place (works in vertical text); add/edit notes,
  list, and delete. Highlights are anchored to a document-order block and a
  character range within the chosen side (original *or* translation), so they
  stay correct in all three views — the made side is shown character-precise and
  the other side as a whole-block tint. Saved next to the EPUB.
- **Markdown / JSON** highlight export & import.
- **Kindle Notebook** (HTML) import, matched onto chapter blocks.
- **Aozora Bunko XHTML** export of the current chapter (ruby preserved, images
  inlined as data URIs).
- **Local AI translation** via [Ollama](https://ollama.com) — whole-chapter
  modes (original / bilingual / translation-only) plus on-the-spot translation
  of a selection; configurable model, target language and endpoint. Results are
  **cached per book and language** next to the EPUB, so re-reading is instant.
- **Translation glossary** — an optional `<book>.epub.glossary.json` fixes the
  target wording of chosen terms (names, jargon) for consistent translations.
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

- **Sidebar:** toggle the table-of-contents sidebar with the **☰ 目次** button.
- **Highlight:** select text → pick a colour from the popup. Click an existing
  highlight to change its colour, edit its note, or delete it.
- **Translation:** click **🌐**, choose a mode (original / bilingual /
  translation), set the model + target language, and press 再翻訳; or select
  text and choose **🌐 翻訳** to translate just that selection (the result popup
  closes on Escape or an outside click). Requires a running Ollama instance with
  the chosen model installed (default `qwen2.5`).
- **Export a translated book:** **翻訳 → 対訳 EPUB を書き出し / 訳文 EPUB を書き出し**
  builds a new `.epub` from the cached translations (any not-yet-translated
  paragraphs are translated first, with a cancelable progress dialog).
- **Source view:** the **</> XML** button shows the chapter's raw XHTML.

### Sidecar files

Per-book data is written next to the `.epub` (not in an app data directory):

| File | Contents |
|------|----------|
| `<book>.epub.spindle.highlights.json` | highlights & notes |
| `<book>.epub.spindle.<lang>.json` | translation cache for a target language |
| `<book>.epub.glossary.json` | optional glossary (you create/edit this) |

#### Glossary format

Create `<book>.epub.glossary.json` next to the book (for `Changeling.epub`, the
file is `Changeling.epub.glossary.json`). It fixes how chosen terms are
translated, per target language:

```json
{
  "version": 1,
  "langs": {
    "ja": [
      { "source": "Changeling", "target": "チェンジリング", "note": "character name" },
      { "source": "the Order", "target": "教団" }
    ],
    "en": [
      { "source": "妖精", "target": "fae" }
    ]
  }
}
```

| Field | Required | Meaning |
|-------|----------|---------|
| `version` | yes | format version, currently `1` |
| `langs` | yes | object keyed by target language code (`ja`, `en`, …) — the same code used as the translation target |
| `langs.<code>[]` | — | list of term entries for that target language |
| `source` | yes | the term as it appears in the original text |
| `target` | yes | the wording to use in the translation |
| `note` | no | a short hint passed to the model (e.g. `"character name"`) |

Notes:
- The entries for the **current target language** are added to the translation
  prompt, so each language has its own list; switching the target language loads
  that language's section.
- Entries with an empty `source` or `target` are ignored.
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
- **Windows** → portable `.zip` + NSIS `setup.exe`

Artifacts are uploaded for every run. Push a tag like `v0.2.1` to also publish a
GitHub Release with all packages attached.

```sh
git tag v0.2.1 && git push origin v0.2.1
```

## Project layout

```
src/
├── epub/    ZIP (miniz) + EPUB parsing (OPF/spine/nav/NCX), path utilities
├── core/    chapter text, search, Kindle matcher, Markdown, Aozora export
├── model/   highlight model + JSON persistence
├── web/     epub:// scheme handler, QWebChannel bridge
├── net/     Ollama translation client
└── ui/      main window (QWebEngineView reader + sidebar/toolbar)
resources/   reader.js (injected), app icon, Qt resource file
packaging/   Linux .desktop entry
scripts/     build & packaging scripts (see scripts/README.md)
third_party/ vendored miniz
```

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the version history.

## Third-party

- [miniz](https://github.com/richgel999/miniz) (MIT) — vendored in `third_party/miniz`.
