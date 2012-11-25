all: libsystemuiplugin_tklock.so

clean:
	$(RM) libsystemuiplugin_tklock.so

install: libsystemuiplugin_tklock.so
	install -d $(DESTDIR)/usr/lib/systemui
	install -m 644 libsystemuiplugin_tklock.so $(DESTDIR)/usr/lib/systemui

libsystemuiplugin_tklock.so: osso-systemui-tklock.c
	$(CC) $^ -o $@ -shared -Wall $(CFLAGS) $(LDFLAGS) $(shell pkg-config --libs --cflags hildon-1 gconf-2.0 alarm libnotify gtk+-2.0 dbus-1 glib-2.0 sqlite3) -ltime -lhildon-plugins-notify-sv -L/usr/lib/hildon-desktop -Wl,-soname -Wl,$@ -Wl,-rpath -Wl,/usr/lib/hildon-desktop

.PHONY: all clean install
