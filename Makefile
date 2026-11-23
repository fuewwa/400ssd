all: 400ssd

400ssd: build/main.o build/pty.o build/term.o build/util.o build/win.o
	gcc -o 400ssd build/main.o build/pty.o build/term.o build/util.o build/win.o $$(pkg-config --libs x11 xft fontconfig)

build/%.o: src/%.c include/pty.h include/term.h include/util.h include/win.h config.h
	@mkdir -p build
	gcc -Iinclude -I. -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -DVERSION=\"0.1.0\" $$(pkg-config --cflags x11 xft fontconfig) -std=c11 -Os -Wall -Wextra -pedantic -Wno-unused-const-variable -c -o $@ $<

config.h: config.def.h
	cp config.def.h config.h

clean:
	rm -rf build 400ssd 400ssd-0.1.0.tar.gz

install: all
	mkdir -p $(DESTDIR)/usr/bin
	install -m 755 400ssd $(DESTDIR)/usr/bin/400ssd
	mkdir -p $(DESTDIR)/usr/share/man/man1
	sed "s/VERSION/0.1.0/g" docs/400ssd.1 > $(DESTDIR)/usr/share/man/man1/400ssd.1
	chmod 644 $(DESTDIR)/usr/share/man/man1/400ssd.1
	mkdir -p $(DESTDIR)/usr/share/applications
	install -m 644 docs/400ssd.desktop $(DESTDIR)/usr/share/applications/400ssd.desktop
	if [ -z "$(DESTDIR)" ]; then \
		if command -v mandb >/dev/null 2>&1; then mandb -q || true; \
		elif command -v makewhatis >/dev/null 2>&1; then makewhatis /usr/share/man || true; fi; \
		if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database -q /usr/share/applications || true; fi; \
	fi

uninstall:
	rm -f $(DESTDIR)/usr/bin/400ssd
	rm -f $(DESTDIR)/usr/share/man/man1/400ssd.1
	rm -f $(DESTDIR)/usr/share/applications/400ssd.desktop
	if [ -z "$(DESTDIR)" ]; then \
		if command -v mandb >/dev/null 2>&1; then mandb -q || true; fi; \
		if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database -q /usr/share/applications || true; fi; \
	fi

.PHONY: all clean install uninstall
