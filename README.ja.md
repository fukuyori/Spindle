# Spindle (C++ / Qt)

*[English](README.md) | 日本語*

C++ / Qt で作られた、ネイティブなマルチプラットフォーム EPUB リーダーです。

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
- **読書操作** — 章送り、フォント拡大縮小、本のフォントを上書きできるフォント選択、
  ライト / セピア / ダークのテーマ、目次サイドバーの表示/非表示。テーマ・ウィンドウ
  サイズ・翻訳表示モードは次回起動時に復元されます。
- **全文検索** — 書籍全体を検索し、章ごとにスニペット表示・本文内ジャンプ。
- **ハイライトとノート** — テキストを選択して 6 色から着色。マークは本文中に
  描画（縦書きでも動作）。ノートの追加・編集、一覧、削除に対応。ハイライトは
  「文書順のブロック番号 + 選んだ側（原文 *または* 訳文）のテキスト内の文字範囲」で
  保持し、**引いた側にのみ文字単位で表示**します（反対側には表示しません）。一覧では
  各項目に ［原］/［訳］ を表示し、その側が非表示でも該当ブロックへジャンプします。
  保存先は EPUB と同じフォルダ。
- **Markdown / JSON** でのハイライト入出力。
- **Kindle ノート**（HTML）の取り込み — 章のブロックに対応付け。
- **青空文庫 XHTML** 形式での章書き出し（ルビ保持、画像は data URI で埋め込み）。
- **ローカル AI 翻訳** — [Ollama](https://ollama.com) 連携。章全体の 3 モード
  （原文 / 対訳併記 / 訳文）に加え、選択範囲だけをその場で翻訳。モデル・翻訳先
  言語・エンドポイントを設定可能。訳文の文字色を選択でき、章翻訳は最大2件を並行
  実行。翻訳結果は**本・言語ごとに** EPUB の隣にキャッシュするので再読は即時。
  本の言語と翻訳先が同じ場合は原文表示に固定されます（翻訳は無効）。
- **翻訳用語集** — 任意の `<本>.glossary.json` で、指定した用語（人名・専門
  用語など）の訳語を固定し、翻訳の表記ゆれを抑えます。
- **翻訳 EPUB 書き出し** — キャッシュから**対訳／訳文の `.epub`** を生成（未翻訳
  段落は書き出し時に翻訳）。出力の言語メタデータは翻訳先言語に設定。
- **XHTML ソース表示** — 現在の章を描画表示と生マークアップで切り替え。
- **読みやすい左右余白**と開閉できるサイドバー。
- **複数の本を同時に** — ウィンドウに `.epub` をドロップすると、本がまだ開かれて
  いなければそのウィンドウで、開いていれば新しいウィンドウで開きます。コマンド
  ライン引数・「このアプリで開く」にも対応。

## 使い方

**ファイル → EPUB を開く** メニュー、ウィンドウ（本文エリア含む）への `.epub` の
ドロップ、またはコマンドライン引数（複数可）でファイルを開きます。ドロップは、本が
未読み込みのウィンドウならそのウィンドウで、すでに開いていれば新しいウィンドウで
開くので、複数冊を同時に読めます。

| 操作 | ショートカット |
|------|----------------|
| 次の章 | `Space` / `→` |
| 前の章 | `←` |
| 検索にフォーカス | `Cmd` / `Ctrl` + `F` |

- **サイドバー**：**☰ 目次** ボタンで目次サイドバーを開閉します。
- **ハイライト**：テキストを選択 → ポップアップで色を選択（コピー・翻訳も可能）。
  既存のハイライトをクリックすると、コピー・色変更・ノート編集・削除ができます。
- **翻訳**：**🌐** をクリックし、モード（原文 / 対訳併記 / 訳文）を選び、モデルと
  翻訳先言語を設定して「再翻訳」。または、テキストを選択して **🌐 翻訳** を選ぶと
  その選択範囲だけを翻訳します（結果ポップアップは Escape か外側クリックで閉じる）。
  選択したモデルがインストール済みの Ollama が起動している必要があります
  （既定モデルは `qwen2.5`）。
  - **並行翻訳**：章の翻訳は最大2件を同時に投げますが、速くなるのは Ollama サーバが
    並行処理を許可している場合だけです。macOS の Ollama アプリは
    `OLLAMA_NUM_PARALLEL=1` のことが多く（`launchctl setenv` も確実には反映されません）。
    有効にするにはアプリを終了し、変数を付けて自分でサーバを起動します:
    ```sh
    OLLAMA_NUM_PARALLEL=2 ollama serve
    ```
    Spindle 側は既定の `http://localhost:11434` のままで動きます。GPU が1枚の場合は
    2つのリクエストが同じ GPU を共有するため、効果は2倍ではなく部分的です。
- **翻訳した本の書き出し**：**翻訳 → 対訳 EPUB を書き出し／訳文 EPUB を書き出し**
  で、キャッシュした翻訳から新しい `.epub` を生成します（未翻訳の段落は書き出し時に
  翻訳。進捗ダイアログでキャンセル可）。
- **ソース表示**：**</> XML** ボタンで章の生 XHTML を表示します。

### サイドカーファイル

本ごとのデータは、アプリのデータフォルダではなく `.epub` と同じフォルダに保存されます:

`Foo.epub` の場合、ベース名 `Foo` を使ったファイル名になります:

| ファイル | 内容 |
|----------|------|
| `<本>.highlights.json` | ハイライトとノート |
| `<本>.<lang>.json` | 翻訳キャッシュ（言語ごと。例 `Foo.ja.json`） |
| `<本>.glossary.json` | 用語集（任意・自分で作成／編集） |

#### 用語集の書式

本と同じフォルダに `<本>.glossary.json` を作成します（`Changeling.epub` なら
`Changeling.glossary.json`）。1ファイルにつき1つの「原語→訳語」言語ペアを持ち、
指定した用語の訳し方を固定します:

```json
{
  "source_lang": "la",
  "target_lang": "ja",
  "entries": [
    { "src": "Caesar", "dst": "カエサル", "note": "人名" },
    { "src": "Gallia", "dst": "ガリア" }
  ]
}
```

| フィールド | 必須 | 意味 |
|------------|------|------|
| `source_lang` | 任意 | 原語の言語コード（参考情報） |
| `target_lang` | 必須 | 翻訳先言語コード（`ja` / `en` …）。翻訳先がこれと一致するときだけ用語集が適用される |
| `entries` | 必須 | 用語マッピングの配列 |
| `src` | 必須 | 原文中に現れる語 |
| `dst` | 必須 | 翻訳で使う訳語 |
| `note` | 任意 | モデルへ渡す短い補足（例 `"人名"`） |

補足:
- `target_lang` が現在の翻訳先と一致するときのみ使われます（`target_lang` 省略時は
  どの翻訳先でも適用）。
- `src` または `dst` が空のエントリは無視されます。
- 適用はプロンプト経由（モデルに「この訳語を使うように」と指示）なので、強い指示で
  あって機械的な置換ではありません。文法・活用に合わせて表記が変わることがあります。
  ファイルは本を開いたとき／翻訳先言語を変えたときに読み込まれます（編集後は本を
  開き直すか言語を切り替えると反映されます）。

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

各実行で成果物がアップロードされます。`v0.2.6` のようなタグを push すると、
全パッケージを添付した GitHub Release も自動作成されます。

```sh
git tag v0.2.6 && git push origin v0.2.6
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

## 変更履歴

バージョンごとの変更点は [CHANGELOG.md](CHANGELOG.md) を参照してください。

## サードパーティ

- [miniz](https://github.com/richgel999/miniz)（MIT）— `third_party/miniz` に同梱。
