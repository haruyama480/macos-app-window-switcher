#!/bin/sh
# Relaunch inside AppWindowSwitcher.app so TCC judges this app, not the parent.
set -eu

here=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
app="$here/AppWindowSwitcher.app"

if [ ! -d "$app" ]; then
	echo "app-window-switcher: $app がありません。make を実行してください。" >&2
	exit 1
fi

if [ "$#" -eq 1 ]; then
	case "$1" in
		help|-h|--help)
			cat <<'EOF'
使い方: app-window-switcher <command>

コマンド:
  next     前面アプリの次のウィンドウを上げる
  prev     前面アプリの前のウィンドウを上げる
  list     対象ウィンドウを位置順に表示する
  help     この使い方を表示する
EOF
			exit 0
			;;
	esac
fi

if [ "$#" -eq 0 ]; then
	echo "使い方: app-window-switcher <command>" >&2
	exit 2
fi
if [ "$#" -ne 1 ]; then
	echo "引数が多すぎます。" >&2
	exit 2
fi

case "$1" in
	next|prev|list) ;;
	*)
		echo "未知のコマンド: $1" >&2
		exit 2
		;;
esac

dir=$(mktemp -d "${TMPDIR:-/tmp}/app-window-switcher.XXXXXX")
# -W is omitted. Launch Services cannot get a PID for this agent app
# (GetProcessPID returns procNotFound, -600) because it exits immediately.
open -n -g "$app" --args --direct "$dir" "$1"

i=0
while [ ! -f "$dir/status" ] && [ "$i" -lt 900 ]; do
	i=$((i + 1))
	sleep 0.1
done

status=1
if [ -f "$dir/status" ]; then
	status=$(cat "$dir/status")
else
	echo "app-window-switcher: アプリが結果を返しませんでした。" >&2
fi
if [ -f "$dir/stdout" ]; then
	cat "$dir/stdout"
fi
if [ -f "$dir/stderr" ]; then
	cat "$dir/stderr" >&2
fi
rm -rf "$dir"
exit "$status"
