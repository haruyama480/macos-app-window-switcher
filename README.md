# macos-app-window-switcher

キーボードフォーカスのあるアプリの標準ウィンドウを、位置順で next / prev するコマンド。Accessibility API を直接呼ぶ C プログラムで、窓 1 枚の属性は 1 回で読む。

設計: [Design.md](Design.md)

`osascript` 版は [app-window-switcher-shell](app-window-switcher-shell/) に残してある。

## ビルド

```
make
```

成果物は `bin/app-window-switcher`。

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

`bin/app-window-switcher` は、自分自身を `bin/AppWindowSwitcher.app` として起動し直します。ターミナルや skhd からバイナリを直接 AX 呼び出しすると、許可の判定対象は親プロセスのままになり、このプログラムのスイッチは使われません。

初回は次を実行してください。許可ダイアログは最大 60 秒開いたままになります。AppWindowSwitcher を許可すると、その実行の続きで `list` が出ます。署名は Apple Development 証明書です。ad-hoc のときに入れた許可は、この署名には引き継がれません。

```
bin/app-window-switcher list
```

再ビルドすると ad-hoc 署名の cdhash が変わるので、許可を入れ直すことがあります。

- システム設定 → プライバシーとセキュリティ → アクセシビリティ

一覧に実行ファイルが出ないときは、起動元の skhd やターミナルを追加する。`.app` は作らない。

## skhd

ホットキーはこのコマンドには無い。skhd からは絶対パスで呼ぶ。バッククォートの文字そのものは skhd のパーサが壊れるので、keycode を使う。US ANSI の grave は `0x32`。別の配列は `skhd --observe` で確認する。

OS の「次のウィンドウにフォーカスを移動」（⌘`）が同じキーを取っているときは、システム設定で無効にするか別のキーへ移す。

```
cat << EOF
alt + shift + ctrl - 0x32 : $(pwd)/bin/app-window-switcher next
alt + shift + ctrl + shift - 0x32 : $(pwd)/bin/app-window-switcher prev
EOF
```

このコマンドが `AppWindowSwitcher.app` を `open` します。skhd に `open` を直接書かなくて構いません。

## 制限

連打が重なると、同じ「次」を二度選ぶことがある。他の Space へは移動しない。`AXMain` を更新しないアプリでは、先の窓に進まない。
