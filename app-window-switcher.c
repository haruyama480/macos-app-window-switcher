#include <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach-o/dyld.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <unistd.h>

enum {
	ATTR_SUBROLE = 0,
	ATTR_POSITION,
	ATTR_SIZE,
	ATTR_TITLE,
	ATTR_MINIMIZED,
	ATTR_MAIN,
	ATTR_COUNT
};

typedef struct {
	AXUIElementRef el;
	int z;
	int x, y, w, h;
	bool main;
	char *title;
} Win;

static void usage(FILE *out) {
	fputs(
		"使い方: app-window-switcher <command>\n"
		"\n"
		"コマンド:\n"
		"  next     前面アプリの次のウィンドウを上げる\n"
		"  prev     前面アプリの前のウィンドウを上げる\n"
		"  list     対象ウィンドウを位置順に表示する\n"
		"  help     この使い方を表示する\n",
		out);
}

static void exe_path(char *out, size_t n) {
	char buf[PATH_MAX];
	uint32_t sz = sizeof buf;
	if (_NSGetExecutablePath(buf, &sz) != 0) {
		snprintf(out, n, "(unknown)");
		return;
	}
	if (!realpath(buf, out)) snprintf(out, n, "%s", buf);
}

static void client_path(char *out, size_t n) {
	char exe[PATH_MAX];
	exe_path(exe, sizeof exe);
	char *marker = strstr(exe, ".app/Contents/MacOS/");
	if (!marker) {
		snprintf(out, n, "%s", exe);
		return;
	}
	size_t len = (size_t)(marker - exe) + 4;
	if (len >= n) len = n - 1;
	memcpy(out, exe, len);
	out[len] = '\0';
}

static int permission_denied(int axerr, bool have_err) {
	char path[PATH_MAX];
	client_path(path, sizeof path);
	fprintf(stderr,
		"app-window-switcher: アクセシビリティが許可されていません。\n"
		"システム設定 → プライバシーとセキュリティ → アクセシビリティ\n"
		"に、AppWindowSwitcher を追加してください。\n"
		"  %s\n",
		path);
	if (have_err) fprintf(stderr, "\nAXError %d\n", axerr);
	return 1;
}

static bool wait_until_trusted(void) {
	const void *keys[] = {kAXTrustedCheckOptionPrompt};
	const void *values[] = {kCFBooleanTrue};
	CFDictionaryRef options = CFDictionaryCreate(NULL, keys, values, 1,
		&kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	Boolean trusted = AXIsProcessTrustedWithOptions(options);
	CFRelease(options);
	for (int i = 0; !trusted && i < 60; i++) {
		sleep(1);
		trusted = AXIsProcessTrusted();
	}
	return trusted;
}

static bool is_missing(CFTypeRef value) {
	if (!value || value == kCFNull) return true;
	if (CFGetTypeID(value) == AXValueGetTypeID() &&
		AXValueGetType((AXValueRef)value) == kAXValueAXErrorType) {
		return true;
	}
	return false;
}

static bool is_standard(CFTypeRef value) {
	if (is_missing(value) || CFGetTypeID(value) != CFStringGetTypeID()) return false;
	return CFStringCompare(value, kAXStandardWindowSubrole, 0) == kCFCompareEqualTo;
}

static bool is_true_bool(CFTypeRef value) {
	if (is_missing(value) || CFGetTypeID(value) != CFBooleanGetTypeID()) return false;
	return CFBooleanGetValue(value);
}

static char *title_from(CFTypeRef value) {
	if (is_missing(value) || CFGetTypeID(value) != CFStringGetTypeID()) return strdup("");
	CFStringRef s = (CFStringRef)value;
	CFIndex max = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
	char *buf = malloc((size_t)max);
	if (!buf) return strdup("");
	if (!CFStringGetCString(s, buf, max, kCFStringEncodingUTF8)) {
		free(buf);
		return strdup("");
	}
	for (char *p = buf; *p; p++) {
		if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
	}
	return buf;
}

static void read_point(CFTypeRef value, int *x, int *y) {
	*x = 0;
	*y = 0;
	if (is_missing(value) || CFGetTypeID(value) != AXValueGetTypeID()) return;
	CGPoint p;
	if (!AXValueGetValue((AXValueRef)value, kAXValueCGPointType, &p)) return;
	*x = (int)lround(p.x);
	*y = (int)lround(p.y);
}

static void read_size(CFTypeRef value, int *w, int *h) {
	*w = 0;
	*h = 0;
	if (is_missing(value) || CFGetTypeID(value) != AXValueGetTypeID()) return;
	CGSize s;
	if (!AXValueGetValue((AXValueRef)value, kAXValueCGSizeType, &s)) return;
	*w = (int)lround(s.width);
	*h = (int)lround(s.height);
}

static CFArrayRef attribute_names(void) {
	const void *attrs[ATTR_COUNT] = {
		kAXSubroleAttribute,
		kAXPositionAttribute,
		kAXSizeAttribute,
		kAXTitleAttribute,
		kAXMinimizedAttribute,
		kAXMainAttribute,
	};
	return CFArrayCreate(NULL, attrs, ATTR_COUNT, &kCFTypeArrayCallBacks);
}

static int cmp_win(const void *va, const void *vb) {
	const Win *a = va;
	const Win *b = vb;
	if (a->x != b->x) return (a->x < b->x) ? -1 : 1;
	if (a->y != b->y) return (a->y < b->y) ? -1 : 1;
	int title = strcmp(a->title, b->title);
	if (title != 0) return title;
	if (a->z != b->z) return (a->z < b->z) ? -1 : 1;
	return 0;
}

static int current_index(const Win *wins, int n) {
	for (int i = 0; i < n; i++) {
		if (wins[i].main) return i;
	}
	int best = 0;
	for (int i = 1; i < n; i++) {
		if (wins[i].z < wins[best].z) best = i;
	}
	return best;
}

static void free_wins(Win *wins, int n) {
	for (int i = 0; i < n; i++) {
		if (wins[i].el) CFRelease(wins[i].el);
		free(wins[i].title);
	}
	free(wins);
}

static bool push_win(Win **wins, int *n, int *cap, Win w) {
	if (*n == *cap) {
		int next = *cap == 0 ? 8 : *cap * 2;
		Win *grown = realloc(*wins, (size_t)next * sizeof(Win));
		if (!grown) return false;
		*wins = grown;
		*cap = next;
	}
	(*wins)[(*n)++] = w;
	return true;
}

static void connect_window_server(void) {
	static bool connected = false;
	if (connected) return;
	NSApplication *app = [NSApplication sharedApplication];
	[app setActivationPolicy:NSApplicationActivationPolicyAccessory];
	connected = true;
}

static int load_eligible(Win **out, int *out_n, char *err, size_t errsz) {
	*out = NULL;
	*out_n = 0;
	connect_window_server();
	AXUIElementRef sys = AXUIElementCreateSystemWide();
	if (!sys) {
		snprintf(err, errsz, "アクセシビリティの接続に失敗しました。");
		return -1;
	}
	AXUIElementSetMessagingTimeout(sys, 1.0f);

	CFTypeRef app = NULL;
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
	AXUIElementSetMessagingTimeout((AXUIElementRef)app, 1.0f);

	CFTypeRef listed = NULL;
	axerr = AXUIElementCopyAttributeValue((AXUIElementRef)app, kAXWindowsAttribute, &listed);
	if (axerr != kAXErrorSuccess || !listed || CFGetTypeID(listed) != CFArrayGetTypeID()) {
		if (listed) CFRelease(listed);
		CFRelease(app);
		CFRelease(sys);
		snprintf(err, errsz, "ウィンドウ一覧を取得できません (%d)。", (int)axerr);
		return -1;
	}

	CFArrayRef attrs = attribute_names();
	CFArrayRef all = (CFArrayRef)listed;
	CFIndex count = CFArrayGetCount(all);
	Win *wins = NULL;
	int n = 0;
	int cap = 0;
	bool oom = false;

	for (CFIndex i = 0; i < count; i++) {
		AXUIElementRef el = (AXUIElementRef)CFArrayGetValueAtIndex(all, i);
		if (!el || CFGetTypeID(el) != AXUIElementGetTypeID()) continue;

		CFArrayRef values = NULL;
		axerr = AXUIElementCopyMultipleAttributeValues(el, attrs, 0, &values);
		if (axerr != kAXErrorSuccess || !values || CFArrayGetCount(values) < ATTR_COUNT) {
			if (values) CFRelease(values);
			continue;
		}

		CFTypeRef subrole = CFArrayGetValueAtIndex(values, ATTR_SUBROLE);
		CFTypeRef minimized = CFArrayGetValueAtIndex(values, ATTR_MINIMIZED);
		if (!is_standard(subrole) || is_true_bool(minimized)) {
			CFRelease(values);
			continue;
		}

		Win w;
		memset(&w, 0, sizeof w);
		w.el = el;
		CFRetain(el);
		w.z = (int)i;
		w.main = is_true_bool(CFArrayGetValueAtIndex(values, ATTR_MAIN));
		w.title = title_from(CFArrayGetValueAtIndex(values, ATTR_TITLE));
		read_point(CFArrayGetValueAtIndex(values, ATTR_POSITION), &w.x, &w.y);
		read_size(CFArrayGetValueAtIndex(values, ATTR_SIZE), &w.w, &w.h);
		CFRelease(values);
		if (!push_win(&wins, &n, &cap, w)) {
			CFRelease(w.el);
			free(w.title);
			oom = true;
			break;
		}
	}

	CFRelease(attrs);
	CFRelease(all);
	CFRelease(app);
	CFRelease(sys);

	if (oom) {
		free_wins(wins, n);
		snprintf(err, errsz, "メモリが不足しています。");
		return -1;
	}
	if (n > 1) qsort(wins, (size_t)n, sizeof(Win), cmp_win);
	*out = wins;
	*out_n = n;
	return 0;
}

static void print_list(const Win *wins, int n) {
	for (int i = 0; i < n; i++) {
		printf("%d main=%d x=%d y=%d w=%d h=%d title=%s\n",
			i + 1, wins[i].main ? 1 : 0, wins[i].x, wins[i].y, wins[i].w, wins[i].h, wins[i].title);
	}
}

static int raise_win(const Win *w) {
	AXError axerr = AXUIElementPerformAction(w->el, kAXRaiseAction);
	if (axerr != kAXErrorSuccess) {
		fprintf(stderr, "app-window-switcher: ウィンドウを前面にできません (%d)。\n", (int)axerr);
		return 1;
	}
	axerr = AXUIElementSetAttributeValue(w->el, kAXMainAttribute, kCFBooleanTrue);
	if (axerr != kAXErrorSuccess) {
		fprintf(stderr, "app-window-switcher: AXMain を設定できません (%d)。\n", (int)axerr);
		return 1;
	}
	return 0;
}

static int run_command(const char *cmd) {
	Win *wins = NULL;
	int n = 0;
	char err[256];
	int loaded = load_eligible(&wins, &n, err, sizeof err);
	if (loaded == -2) return 1;
	if (loaded != 0) {
		fprintf(stderr, "app-window-switcher: %s\n", err);
		return 1;
	}
	if (strcmp(cmd, "list") == 0) {
		print_list(wins, n);
		free_wins(wins, n);
		return 0;
	}
	if (n <= 1) {
		free_wins(wins, n);
		return 0;
	}
	int current = current_index(wins, n);
	int target = current;
	if (strcmp(cmd, "next") == 0) {
		target = current + 1;
		if (target >= n) target = 0;
	} else {
		target = current - 1;
		if (target < 0) target = n - 1;
	}
	int status = raise_win(&wins[target]);
	free_wins(wins, n);
	return status;
}

static int direct_main(const char *dir, const char *cmd) {
	char out_path[PATH_MAX], err_path[PATH_MAX], status_path[PATH_MAX];
	snprintf(out_path, sizeof out_path, "%s/stdout", dir);
	snprintf(err_path, sizeof err_path, "%s/stderr", dir);
	snprintf(status_path, sizeof status_path, "%s/status", dir);
	freopen(out_path, "w", stdout);
	freopen(err_path, "w", stderr);
	int status;
	if (strcmp(cmd, "next") != 0 && strcmp(cmd, "prev") != 0 && strcmp(cmd, "list") != 0) {
		fprintf(stderr, "未知のコマンド: %s\n", cmd);
		usage(stderr);
		status = 2;
	} else if (!wait_until_trusted()) {
		status = permission_denied(0, false);
	} else {
		status = run_command(cmd);
	}
	fflush(stdout);
	fflush(stderr);
	FILE *sf = fopen(status_path, "w");
	if (sf) {
		fprintf(sf, "%d\n", status);
		fclose(sf);
	}
	return status;
}

int main(int argc, char **argv) {
	if (argc == 4 && strcmp(argv[1], "--direct") == 0) return direct_main(argv[2], argv[3]);
	if (argc == 2 && (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
		usage(stdout);
		return 0;
	}
	if (argc < 2) {
		usage(stderr);
		return 2;
	}
	if (argc > 2) {
		fprintf(stderr, "引数が多すぎます。\n");
		usage(stderr);
		return 2;
	}
	const char *cmd = argv[1];
	if (strcmp(cmd, "next") != 0 && strcmp(cmd, "prev") != 0 && strcmp(cmd, "list") != 0) {
		fprintf(stderr, "未知のコマンド: %s\n", cmd);
		usage(stderr);
		return 2;
	}
	if (!wait_until_trusted()) return permission_denied(0, false);
	return run_command(cmd);
}
