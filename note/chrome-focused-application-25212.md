# Chrome で前面アプリケーション取得が -25212 になる

| 項目 | 値 |
|---|---|
| Date | 2026-09-28 |
| Status | 調査のみ。コードは変えていない |
| 環境 | macOS 26.6.2 (25G83)、Google Chrome 153.0.8010.53 |
| 対象 | `app-window-switcher.c` の `AXFocusedApplication` 取得 |

## 症状

Ghostty、VS Code、TextEdit では `next` / `list` が動く。Chrome では成功するときと、次で失敗するときがある。

```
app-window-switcher: 前面のアプリケーションを取得できません (-25212)。
```

失敗した回はウィンドウを切り替えられない。成功した回の `list` は、Chrome の標準ウィンドウを位置順に出せていた。

このエラーは skhd（`alt + shift + ctrl - 0x32` で `bin/app-window-switcher next`）からも、ターミナルの `sleep 1 && bin/app-window-switcher list` からも出る。起動元の違いではない。

## エラーの意味

`-25212` は `kAXErrorNoValue` である。属性はサポートされているが、その時点では値がない。

権限不足ではない。権限不足はこのプログラムでは `-25211`（`kAXErrorAPIDisabled`）か、TCC の判定対象が親のままのときの `-25204`（`kAXErrorCannotComplete`）になる。

## コマンドが止まる場所

`load_eligible` は、ウィンドウ一覧の前にシステム全体の要素から `kAXFocusedApplicationAttribute` を 1 回だけ読む。失敗するとそこで戻る。

```223:231:app-window-switcher.c
	AXError axerr = AXUIElementCopyAttributeValue(sys, kAXFocusedApplicationAttribute, &app);
	if (axerr != kAXErrorSuccess || !app) {
		CFRelease(sys);
		if (axerr == kAXErrorAPIDisabled) {
			permission_denied(axerr, true);
			return -2;
		}
		snprintf(err, errsz, "前面のアプリケーションを取得できません (%d)。", (int)axerr);
		return -1;
	}
```

この直前に `connect_window_server` が `NSApplication` を初期化し、activation policy を accessory にしている。メッセージングのタイムアウトは 1 秒。`-25212` はタイムアウトではなく、すぐに「値がない」と返っている。

再試行はしない。`NSWorkspace` の前面アプリへ切り替える処理もない。

## 測り方

製品の `bin/AppWindowSwitcher.app` は置き換えていない。同じ bundle id `com.github.haruyama480.app-window-switcher` のプローブを `/tmp` に作り、`open -n -g` で起動した。アクセシビリティの許可は製品アプリと同じ判定で通った。計測後にプローブは消した。

プローブは製品と同じ順で、`NSApplication` の初期化と accessory への変更の直後に `AXFocusedApplication` を読んだ。失敗したときは、前面プロセスの PID から `AXUIElementCreateApplication` し、ウィンドウ一覧と focused window、main window を別途読んだ。

## 計測結果

Chrome を前面にして約 2 秒待ったあとの、そのセッションで Chrome に対する最初の問い合わせ。

| 読み取り | 結果 |
|---|---|
| `NSWorkspace` の前面アプリ | Google Chrome、pid 19336、active |
| メニューバーの所有者 | Google Chrome |
| `AXFocusedApplication` | **-25212** |
| `AXFocusedUIElement` | **-25212** |
| `AXEnhancedUserInterface` | false |
| PID から作ったアプリ要素 | `AXApplication`。`AXFrontmost` は true |
| その要素の `AXWindows` | 取得できた（この時点では 1 枚） |
| その要素の focused window / main window | 取得できた |
| その要素の focused UI element | **-25212** |
| 約 50ms 後の `AXFocusedApplication` | 成功。pid 19336 |
| 約 50ms 後の focused UI element | `AXWebArea` |

同じプロセスで、アプリ要素に触れた約 50ms 後にはフォーカス中のアプリが取れるようになっていた。続けて実行した製品の `list` は終了コード 0 で、Chrome の窓を出した。

そのあとの 7 回と、Ghostty と Chrome を切り替えた直後の 4 回は、最初の `AXFocusedApplication` から成功した。focused UI element は `AXWebArea` のままだった。温まったあとは、前面に戻した直後でも `-25212` には戻らなかった。

Ghostty が前面のとき、`NSApplication` 初期化の直後の最初の問い合わせは成功した。focused UI element は `AXTextArea`、窓は 2 枚。`setActivationPolicy:` の前後でフォーカスは外れなかった。

Chrome のメインプロセスの起動引数には `--no-startup-window` があった。activation policy は regular で、窓は存在した。今回の `-25212` の原因ではない。

## 結論

Chrome が画面上の前面アプリであっても、アクセシビリティのフォーカスをまだ出していないあいだは、システム全体の `AXFocusedApplication` が `-25212` になる。macOS はその状態を「フォーカス中のアプリが無い」と返す。

Chrome は、支援技術が問い合わせるまでアクセシビリティの木を作らない。最初の問い合わせはフォーカス要素が無いので `-25212` になり、その問い合わせをきっかけに木ができる。数十ミリ秒後の次の問い合わせでは `AXWebArea` が見え、`AXFocusedApplication` も成功する。

このコマンドは最初の 1 回で終了する。木ができる前の回はウィンドウ一覧まで進まず、スイッチできない。木ができたあとの回は成功する。ページ遷移やレンダラの入れ替えでフォーカス要素が再び無くなると、また同じ失敗になる。

失敗した瞬間でも、前面アプリの PID から作った要素では窓を読めていた。止まっているのは窓の列挙ではなく、その前の「フォーカス中のアプリ」の取得である。

Ghostty、VS Code、TextEdit は、問い合わせを待たずにフォーカス中のアプリを返す。同じ 1 回の取得で足りる。

## 今回やっていないこと

修正は入れていない。`-25212` のときに前面アプリの PID へ切り替える案は、この計測では窓が取れていたが、製品の動作は変えていない。
