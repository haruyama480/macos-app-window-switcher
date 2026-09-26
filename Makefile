CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?= -framework AppKit -framework ApplicationServices -framework CoreFoundation
IDENT ?= com.github.haruyama480.app-window-switcher
SIGN_ID ?= $(shell security find-identity -v -p codesigning | sed -n 's/.*"\(Apple Development:[^"]*\)".*/\1/p' | head -1)
ifeq ($(SIGN_ID),)
SIGN_ID := -
endif
APP = bin/AppWindowSwitcher.app

.PHONY: all clean

all: bin/app-window-switcher

bin/app-window-switcher: app-window-switcher.c packaging/Info.plist packaging/app-window-switcher.sh
	mkdir -p $(APP)/Contents/MacOS
	$(CC) $(CFLAGS) -x objective-c -o $(APP)/Contents/MacOS/app-window-switcher app-window-switcher.c $(LDFLAGS)
	cp packaging/Info.plist $(APP)/Contents/Info.plist
	codesign -s "$(SIGN_ID)" --identifier $(IDENT) --force $(APP)
	cp packaging/app-window-switcher.sh $@
	chmod +x $@

clean:
	rm -rf bin
