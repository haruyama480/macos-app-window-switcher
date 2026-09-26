# macos-app-window-switcher

キーボードフォーカスのあるアプリの標準ウィンドウを、位置順で next / prev するコマンド。Accessibility API を直接呼ぶ C プログラムで、窓 1 枚の属性は 1 回で読む。

設計: [Design.md](Design.md)

`osascript` 版は [app-window-switcher-shell](app-window-switcher-shell/) に残してある。

## ビルド

```
make
```

成果物は次の二つです。

- `bin/app-window-switcher` はシェルスクリプトです。
- `bin/AppWindowSwitcher.app` は署名済みアプリです。

## シェルスクリプトと .app

この二つは同じ処理の複製ではありません。片方を消すと、残った方だけでは動きません。

| ファイル | 役割 |
|---|---|
| `bin/app-window-switcher` | 引数を受け、`.app` を `open` し、結果の標準出力と終了コードを返す |
| `bin/AppWindowSwitcher.app` | Accessibility の呼び出しと、窓の選択・raise を行う |

アクセシビリティの許可は、起動したプログラム自身に付きます。ターミナルや skhd が中のバイナリを直接実行すると、判定対象は親のままで、AX の呼び出しは `-25204` になります。`.app` を `open` で起動すると、判定対象が `com.github.haruyama480.app-window-switcher` になります。だから `.app` は消せません。

`open` だけでは `list` の出力を受け取れません。`open -W` はこのアプリのプロセス番号を取れず、`GetProcessPID()` が「プロセスがない」（`-600`）を返します。シェルスクリプトは `open -W` を使わず、アプリが書いた結果を待って表示します。`list` の表示や終了コードを使うなら、シェルスクリプトも消せません。

skhd から `next` と `prev` だけ呼ぶなら、シェルスクリプトの代わりに次を書けます。

```
open -n -g /ABSOLUTE/PATH/bin/AppWindowSwitcher.app --args next
open -n -g /ABSOLUTE/PATH/bin/AppWindowSwitcher.app --args prev
```

## 使い方

```
bin/app-window-switcher next
bin/app-window-switcher prev
bin/app-window-switcher list
```

`next` / `prev` は成功すると標準出力には何も書かない。`list` は対象ウィンドウを位置順に 1 行ずつ出す。

```
1 main=1 x=0 y=25 w=800 h=600 title=README.md
2 main=0 x=820 y=25 w=700 h=500 title=Untitled
```

対象は、focused application の `AXStandardWindow` のうち最小化されていない窓。左から右、同じ位置なら上から下に並べ、端で循環する。0 枚または 1 枚のときは `next` / `prev` は何もしない。

## 権限

初回は次を実行してください。許可ダイアログは最大 60 秒開いたままになります。AppWindowSwitcher を許可すると、その実行の続きで `list` が出ます。

```
bin/app-window-switcher list
```

許可するのは `bin/AppWindowSwitcher.app` です。場所は「システム設定 → プライバシーとセキュリティ → アクセシビリティ」です。署名は Apple Development 証明書で、identifier は `com.github.haruyama480.app-window-switcher` です。同じ証明書と同じ identifier で再ビルドした許可は引き継がれます。

## skhd

ホットキーはこのコマンドには無い。skhd からは絶対パスで呼ぶ。バッククォートの文字そのものは skhd のパーサが壊れるので、keycode を使う。US ANSI の grave は `0x32`。別の配列は `skhd --observe` で確認する。

OS の「次のウィンドウにフォーカスを移動」（⌘`）が同じキーを取っているときは、システム設定で無効にするか別のキーへ移す。

```
cat << EOF
alt + shift + ctrl - 0x32 : $(pwd)/bin/app-window-switcher next
alt + shift + ctrl + shift - 0x32 : $(pwd)/bin/app-window-switcher prev
EOF
```

このコマンドはシェルスクリプトです。中で `AppWindowSwitcher.app` を `open` します。`next` と `prev` だけなら、上の「シェルスクリプトと .app」にある `open` を skhd に直接書いても同じです。

## 制限

連打が重なると、同じ「次」を二度選ぶことがある。他の Space へは移動しない。`AXMain` を更新しないアプリでは、先の窓に進まない。
