MAN = man/showtime.1
DESKTOP = support/gnome/showtime.desktop
ICON = support/gnome/showtime.svg

INSTDESKTOP= ${DESTDIR}$(prefix)/share/applications
INSTICON= ${DESTDIR}$(prefix)/share/icons/hicolor/scalable/apps


install: ${PROG} ${MAN} ${DESKTOP} ${ICON}
	install -D ${PROG} ${bindir}/showtime
	install -D ${MAN} ${mandir}/showtime.1

	install -D ${DESKTOP} ${INSTDESKTOP}/showtime.desktop
	install -D ${ICON} ${INSTICON}/showtime.svg

#	gtk-update-icon-cache $(prefix)/share/icons/hicolor/


uninstall:
	rm -f ${bindir)/showtime
	rm -f ${mandir)/showtime.1
	rm -f ${INSTDESKTOP}/showtime.desktop
	rm -f ${INSTICON}/showtime.svg

#	gtk-update-icon-cache $(prefix)/share/icons/hicolor/

