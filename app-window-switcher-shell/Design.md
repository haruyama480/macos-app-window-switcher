# app-window-switcher 設計（osascript 版）

この文書は `app-window-switcher-shell/app-window-switcher` の動作です。起動が約 0.4 秒だったため、本体はリポジトリ直下の C 実装に移しました。



| 項目 | 値 |
|---|---|
| Title | app-window-switcher: 同一アプリ内ウィンドウ循環コマンド |
| Date | 2026-09-26 |
| Status | Accepted (v0) |
| Command | `app-window-switcher` |
| Platform | macOS（`osascript` / System Events） |

## 概要

前面アプリの標準ウィンドウを、位置順で next / prev するワンショットコマンド。実装はリポジトリ直下の shell スクリプトで、ウィンドウの取得、選択、raise は、そのスクリプトが 1 回だけ呼ぶ `osascript` に埋め込んだ AppleScript が行う。ビルドは不要。macOS アプリ（`.app`）にはしない。ホットキー、設定ファイル、常駐プロセス、状態ファイルは持たない。skhd などから絶対パスで起動する。

## 動作

対象は System Events の frontmost プロセス。そのプロセスのウィンドウのうち、次を両方満たすものだけを循環に入れる。

- subrole が `AXStandardWindow`
- 最小化されていない（`AXMinimized` が読めない窓は可視として残す）

ダイアログ、シート、浮き窓、subrole が空の窓は入らない。最小化は解除しない。

並びは呼び出しごとに計算する。

1. x の昇順（左から右）
2. y の昇順（上から下）
3. タイトルの昇順
4. System Events が返した順（この呼び出しの中での添字）

座標はポイントを整数に丸めた値。原点より左のディスプレイは x が小さく、先に来る。位置や大きさが読めない窓は 0 として扱う。タイトルの改行とタブは空白にする。

現在の窓は、対象を位置順に並べたときの、`AXMain` が真である最初の窓。対象にメイン窓が無ければ、System Events が返した順で最も前の対象窓を現在とする。

- `next` は現在の次。末尾の次は先頭。
- `prev` は現在の前。先頭の前は末尾。
- 対象が 0 枚または 1 枚のとき、`next` / `prev` は何もせず成功。

raise は同じ `osascript` プロセスの中で、列挙時に保持したウィンドウ参照に対して行う。先に `AXRaise`、続けて `AXMain` を真にする。アプリの activate はしない。

## コマンド

```
app-window-switcher <command>
```

| コマンド | 動作 |
|---|---|
| `next` | 位置順の次の窓を上げる。成功時の標準出力は空 |
| `prev` | 位置順の前の窓を上げる。成功時の標準出力は空 |
| `list` | 対象窓を位置順に 1 行 1 窓で標準出力へ書く。raise しない |
| `help`, `-h`, `--help` | 使い方を標準出力へ書いて成功 |

`list` の 1 行は次の形。`title` は行末までで、値に `=` が含まれてもよい。

```
1 main=1 x=0 y=25 w=800 h=600 title=README.md
2 main=0 x=820 y=25 w=700 h=500 title=Untitled
```

`index` は位置順の 1 始まり。`main` は `AXMain` の 0/1。対象が 0 枚の `list` は空の成功。対象が 1 枚でもその 1 行は出す。

引数が無い、引数が 2 つ以上、または未知のコマンドは、使い方を標準エラーへ書いて終了コード 2。

| 終了コード | 意味 |
|---|---|
| 0 | 成功。対象 0〜1 枚の `next` / `prev` を含む |
| 1 | `osascript` が失敗した、または `osascript` が無い |
| 2 | 使い方 |

## 権限が拒否されたとき

このコマンド用の `.app` は作らない。`doctor` は作らない。成功時は無言。

System Events が拒否したときは、標準エラーに次の案内を書く。続けて空行と、`osascript` の標準エラーを書く。起動元（skhd やターミナル）に、Accessibility と Automation（System Events の制御）の許可が要る。

```
app-window-switcher: System Events で窓を操作できません。
次を確認してください。
  System Settings → Privacy & Security → Accessibility
  System Settings → Privacy & Security → Automation
このコマンドを起動したアプリ（skhd やターミナル）に許可が必要です。
```

判定に使うのは、終了コードが 0 以外かつ標準エラーに `-25211`、`-1743`、`assistive`、`not authorized` のいずれかを含む場合。それ以外の失敗は `osascript` の標準エラーをそのまま書く。標準エラーが空なら、失敗したことだけを 1 行で書く。

## ファイル

```
app-window-switcher    # 実行ビット付き shell。AppleScript を内包する
Design.md
README.md
```

依存は macOS の `/bin/sh` と `osascript`。状態は `$TMPDIR` にも残さない。

## 制限

- 連打が重なると、各起動が同じ「今のメイン窓の次」を選ぶことがある。ロックはしない。
- 他の Space の窓は、System Events がその呼び出しで返したものだけが対象。Space の切り替えはしない。
- パレットやセキュア入力のあいだは、キーボードフォーカスのあるアプリと frontmost プロセスがずれることがある。このコマンドは frontmost プロセスを使う。
- `osascript` を 1 回起動する。キーリピートの間隔より遅くなることがある。
- 同一アプリの単位は frontmost プロセス。別プロセスの窓は別の循環になる。
