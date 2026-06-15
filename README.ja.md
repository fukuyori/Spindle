# Spindle (C++ / Qt)

*[English](README.md) | 日本語*

ネイティブなマルチプラットフォーム EPUB リーダー。オリジナルの
[Spindle](https://github.com/fukuyori/Spindle)（TypeScript + Tauri）を
C++ / Qt で再実装したものです。

リーダー部に Qt Widgets ＋ **Qt WebEngine** を採用し、各書籍を**その本自身の
CSS のまま忠実に描画**します（EPUB が指定していれば日本語の**縦書き**にも対応）。
EPUB 内のリソースは独自 URL スキーム `epub://` 経由でエンジンに渡されるため、
各章は著者が用意したスタイルシート・フォント・画像をそのまま読み込みます。
Windows / macOS / Linux に対応します。

## 機能

- **EPUB 2/3** の読み込み（miniz によるメモリ内解凍、OPF / spine /
  メタデータを Qt XML で解析）。
- **目次** — EPUB 3 の `nav` を優先、NCX にフォールバック、入れ子対応。
- **忠実な描画** — Qt WebEngine ＋ `epub://` スキームにより、縦書き・横書き、
  出版社の CSS、埋め込みフォント・画像を再現。
- **読書操作** — 章送り、フォント拡大縮小、ライト / セピア / ダークのテーマ、
  目次サイドバーの表示/非表示。
- **全文検索** — 書籍全体を検索し、章ごとにスニペット表示・本文内ジャンプ。
- **ハイライトとノート** — テキストを選択して 6 色から着色。マークは本文中に
  描画（縦書きでも動作）。ノートの追加・編集、一覧、削除に対応し、書籍ごとに
  JSON 保存。
- **Markdown / JSON** でのハイライト入出力（オリジナル Spindle と形式互換）。
- **Kindle ノート**（HTML）の取り込み — 多段マッチングで章内オフセットに対応付け。
- **青空文庫 XHTML** 形式での章書き出し（ルビ保持、画像は data URI で埋め込み）。
- **ローカル AI 翻訳** — [Ollama](https://ollama.com) 連携。章全体の 3 モード
  （原文 / 対訳併記 / 訳文）に加え、選択範囲だけをその場で翻訳。モデル・翻訳先
  言語・エンドポイントを設定可能。翻訳結果は**本・言語ごとにキャッシュ**して
  ディスク保存するので再読は即時。
- **翻訳 EPUB 書き出し** — キャッシュから**対訳／訳文の `.epub`** を生成（未翻訳
  段落は書き出し時に翻訳）。出力の言語メタデータは翻訳先言語に設定。
- **XHTML ソース表示** — 現在の章を描画表示と生マークアップで切り替え。
- **読みやすい左右余白**と開閉できるサイドバー。
- **複数の本を同時に** — EPUB ごとに独立したウィンドウで開く。
- **ドラッグ＆ドロップ**・コマンドライン引数・「このアプリで開く」に対応。

## 使い方

**ファイル → EPUB を開く** メニュー、ウィンドウへの `.epub` のドロップ、または
コマンドライン引数（複数可）でファイルを開きます。本ごとに別ウィンドウで開くので、
複数冊を同時に読めます。

| 操作 | ショートカット |
|------|----------------|
| 次の章 | `Space` / `→` |
| 前の章 | `←` |
| 検索にフォーカス | `Cmd` / `Ctrl` + `F` |

- **サイドバー**：**☰ 目次** ボタンで目次サイドバーを開閉します。
- **ハイライト**：テキストを選択 → ポップアップで色を選択。既存のハイライトを
  クリックすると、色変更・ノート編集・削除ができます。
- **翻訳**：**🌐** をクリックし、モード（原文 / 対訳併記 / 訳文）を選び、モデルと
  翻訳先言語を設定して「再翻訳」。または、テキストを選択して **🌐 翻訳** を選ぶと
  その選択範囲だけを翻訳します（結果ポップアップは Escape か外側クリックで閉じる）。
  選択したモデルがインストール済みの Ollama が起動している必要があります
  （既定モデルは `qwen2.5`）。
- **翻訳した本の書き出し**：**翻訳 → 対訳 EPUB を書き出し／訳文 EPUB を書き出し**
  で、キャッシュした翻訳から新しい `.epub` を生成します（未翻訳の段落は書き出し時に
  翻訳。進捗ダイアログでキャンセル可）。
- **ソース表示**：**</> XML** ボタンで章の生 XHTML を表示します。

## ビルド

**Qt 6**（Widgets, Network, Xml, WebEngineWidgets, WebChannel）、
CMake 3.21 以上、C++17 コンパイラが必要です。

### macOS（Homebrew）

```sh
brew install qt
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/spindle.app          # または ./build/spindle.app/Contents/MacOS/spindle
```

### Linux

```sh
# ディストリの qt6-base / qt6-xml / qt6-webengine を入れてから:
cmake -S . -B build
cmake --build build
./build/spindle
```

### Windows

Qt 6（オンラインインストーラ）を導入し、対応するキットを指定します:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
cmake --build build --config Release
```

ビルド補助スクリプトもあります：`scripts/build.sh`（macOS / Linux）、
`scripts/build.ps1`（Windows）。

## パッケージング

[`scripts/`](scripts/README.md) の補助スクリプトで配布用にビルド・同梱できます
（出力は `dist/`）:

```sh
./scripts/package-macos.sh                       # → Spindle-<ver>-macOS.dmg
./scripts/package-linux.sh                       # → AppImage + .deb / .tar.gz
pwsh scripts/package-windows.ps1 -QtPrefix ...   # → 携帯版 .zip（NSIS があれば setup.exe）
```

各 deploy ツールが Qt WebEngine ランタイムを同梱し、アプリを自己完結化します。

> **配布ビルドには公式 Qt が必要です。** Homebrew の Qt は開発には最適ですが、
> `macdeployqt` が WebEngine アプリを完全には同梱できず、生成された `.app` の
> 本文が空白描画になることがあります。配布用 macOS ビルドには
> [公式インストーラ](https://www.qt.io/download-qt-installer) または
> `aqtinstall` で Qt を導入し、
> `CMAKE_PREFIX_PATH=/path/to/Qt/6.x/macos ./scripts/package-macos.sh` を実行して
> ください。Windows / Linux でも同様に完全な Qt キットを使用します。macOS の
> `.dmg` は未署名です。広く配布する場合は `codesign` ／ notarize してください。

## CI / リリース

[`.github/workflows/build.yml`](.github/workflows/build.yml) が push/PR ごとに
公式 Qt（`aqtinstall` 経由）で 3 プラットフォームをビルド・パッケージします:

- **Linux** → AppImage + `.deb` / `.tar.gz`
- **macOS** → `.dmg`
- **Windows** → 携帯版 `.zip` + NSIS `setup.exe`

各実行で成果物がアップロードされます。`v0.1.3` のようなタグを push すると、
全パッケージを添付した GitHub Release も自動作成されます。

```sh
git tag v0.1.3 && git push origin v0.1.3
```

## プロジェクト構成

```
src/
├── epub/    ZIP(miniz) + EPUB 解析(OPF/spine/nav/NCX)、パスユーティリティ
├── core/    章テキスト、検索、Kindle マッチャ、Markdown、青空文庫書き出し
├── model/   ハイライトモデル + JSON 永続化
├── web/     epub:// スキームハンドラ、QWebChannel ブリッジ
├── net/     Ollama 翻訳クライアント
└── ui/      メインウィンドウ（QWebEngineView リーダー + サイドバー/ツールバー）
resources/   reader.js(注入用)、アプリアイコン、Qt リソースファイル
packaging/   Linux .desktop エントリ
scripts/     ビルド・パッケージングスクリプト(scripts/README.md 参照)
third_party/ vendor 化した miniz
```

## サードパーティ

- [miniz](https://github.com/richgel999/miniz)（MIT）— `third_party/miniz` に同梱。
