all: libsystemuiplugin_tklock.so

clean:
	$(RM) libsystemuiplugin_tklock.so

install: libsystemuiplugin_tklock.so
	install -d $(DESTDIR)/usr/lib/systemui
	install -m 644 libsystemuiplugin_tklock.so $(DESTDIR)/usr/lib/systemui
	install -d $(DESTDIR)/usr/share/themes/alpha/backgrounds
	install -m 644 share/themes/alpha-lockslider-portrait.png $(DESTDIR)/usr/share/themes/alpha/backgrounds/lockslider-portrait.png

libsystemuiplugin_tklock.so: gp-tklock.c visual-tklock.c osso-systemui-tklock.c
	$(CC) $^ -o $@ -shared -Wall -I./include -fPIC $(CFLAGS) $(LDFLAGS) $(shell pkg-config --libs --cflags osso-systemui hildon-1 gconf-2.0 alarm libnotify gtk+-2.0 dbus-1 glib-2.0 sqlite3) -ltime -L/usr/lib/hildon-desktop -Wl,-soname -Wl,$@ -Wl,-rpath -Wl,/usr/lib/hildon-desktop

.PHONY: all clean install
