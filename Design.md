# app-window-switcher 設計

| 項目 | 値 |
|---|---|
| Title | app-window-switcher: 同一アプリ内ウィンドウ循環コマンド |
| Date | 2026-09-26 |
| Status | Accepted (C) |
| Command | `app-window-switcher` |
| Platform | macOS（Accessibility API） |

## 概要

キーボードフォーカスのあるアプリの標準ウィンドウを、位置順で next / prev するワンショットコマンド。実装は C。窓の属性は `AXUIElementCopyMultipleAttributeValues` で、窓 1 枚につき 1 回読む。macOS アプリ（`.app`）にはしない。ホットキー、設定ファイル、常駐プロセス、状態ファイルは持たない。skhd などから絶対パスで起動する。

`osascript` 版は起動が約 0.4 秒だった。あのコマンドは [app-window-switcher-shell](app-window-switcher-shell/) に残してある。

## 動作

対象は Accessibility の focused application。そのプロセスの `kAXWindowsAttribute` のうち、次を両方満たすものだけを循環に入れる。

- subrole が `AXStandardWindow`
- 最小化されていない（`AXMinimized` が読めない窓は可視として残す）

ダイアログ、シート、浮き窓、subrole が空の窓は入らない。最小化は解除しない。属性の一括取得に失敗した窓は、その回の対象から外す。

1 枚の窓について、次を 1 回の `AXUIElementCopyMultipleAttributeValues` で読む。失敗した属性は欠けるものとして扱う。

- `AXSubrole`
- `AXPosition`
- `AXSize`
- `AXTitle`
- `AXMinimized`
- `AXMain`

並びは呼び出しごとに計算する。

1. x の昇順（左から右）
2. y の昇順（上から下）
3. タイトルのバイト順
4. Accessibility が返した順（この呼び出しの中での添字）

座標はポイントを整数に丸めた値。原点より左のディスプレイは x が小さく、先に来る。位置や大きさが読めない窓は 0 として扱う。タイトルの改行とタブは空白にする。

現在の窓は、対象を位置順に並べたときの、`AXMain` が真である最初の窓。対象にメイン窓が無ければ、Accessibility が返した順で最も前の対象窓を現在とする。

- `next` は現在の次。末尾の次は先頭。
- `prev` は現在の前。先頭の前は末尾。
- 対象が 0 枚または 1 枚のとき、`next` / `prev` は何もせず成功。

raise は、列挙時に保持した `AXUIElementRef` に対して行う。先に `AXRaise`、続けて `AXMain` を真にする。アプリの activate はしない。

メッセージングのタイムアウトは 1 秒。システム全体の要素に先に設定する。

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
| 1 | Accessibility の失敗、または権限がない |
| 2 | 使い方 |

## 権限が拒否されたとき

このコマンド用の `.app` は作らない。`doctor` は作らない。成功時は無言。プロンプトは出さない。

バイナリをそのまま実行すると、TCC の判定対象は親（skhd やターミナル）になる。親が許可済みでも、ad-hoc な子からの AX メッセージは `-25204` で失敗する。親の許可行を変えても、この失敗は変わらない。

そのためコマンドは `bin/AppWindowSwitcher.app` を `open -n -g -W` で起動し直す。Launch Services 経由だと判定対象は `com.github.haruyama480.app-window-switcher` 自身になる。未許可のときは許可ダイアログを出す。

```
app-window-switcher: アクセシビリティが許可されていません。
システム設定 → プライバシーとセキュリティ → アクセシビリティ
に、次のプログラムを追加してください。
  /path/to/app-window-switcher
一覧に出ないときは、このコマンドを起動したアプリ（skhd やターミナル）を追加してください。
```

System Events を使わないので、Automation の許可は要らない。

## ファイル

```
app-window-switcher.c
Makefile
bin/app-window-switcher          # make の成果物
app-window-switcher-shell/       # osascript 版
Design.md
README.md
```

依存は macOS SDK の ApplicationServices と CoreFoundation。状態は `$TMPDIR` にも残さない。

## 制限

- 連打が重なると、各起動が同じ「今のメイン窓の次」を選ぶことがある。ロックはしない。
- 他の Space の窓は、Accessibility がその呼び出しで返したものだけが対象。Space の切り替えはしない。
- パレットやセキュア入力のあいだは、キーボードフォーカスと前面プロセスがずれることがある。このコマンドは focused application を使う。
- 同一アプリの単位は focused application のプロセス。別プロセスの窓は別の循環になる。
- `AXMain` を更新しないアプリでは、毎回同じ隣が選ばれる。
