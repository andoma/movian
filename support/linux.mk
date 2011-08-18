
# Ubuntu and Fedora uses different path
# checking if it is a fedora release

fedora := $(wildcard /etc/fedora-release)

MAN=man/showtime.1
DESKTOP=support/gnome/showtime.desktop
ICON=support/gnome/showtime.svg

ifeq ($(strip $(fedora)),)
	# ubuntu
	prefix ?= $(INSTALLPREFIX)/local
else
	# fedora
	prefix ?= $(INSTALLPREFIX)
endif

INSTBIN= $(prefix)/bin
INSTDESKTOP= $(prefix)/share/applications
INSTICON= $(prefix)/share/icons/hicolor/scalable/apps


ifeq ($(strip $(fedora)),)
	# ubuntu
	INSTMAN= $(prefix)/share/man1
else
	# fedora
	INSTMAN= $(prefix)/share/man/man1
endif

   
install: ${PROG} ${MAN} ${DESKTOP} ${ICON}
	mkdir -p ${DESTDIR}$(INSTBIN)
	install -s ${PROG} ${DESTDIR}$(INSTBIN)

	mkdir -p ${DESTDIR}$(INSTMAN)
	install ${MAN} ${DESTDIR}$(INSTMAN)

	mkdir -p ${DESTDIR}$(INSTDESKTOP)
	install ${DESKTOP} ${DESTDIR}$(INSTDESKTOP)

	mkdir -p ${DESTDIR}$(INSTICON)
	install ${ICON} ${DESTDIR}$(INSTICON)

	gtk-update-icon-cache $(prefix)/share/icons/hicolor/


uninstall:
	rm -f ${DESTDIR}$(INSTBIN)/showtime
	rm -f ${DESTDIR}$(INSTMAN)/showtime.1
	rm -f ${DESTDIR}${INSTDESKTOP}/showtime.desktop
	rm -f ${DESTDIR}${INSTICON}/showtime.svg
	gtk-update-icon-cache $(prefix)/share/icons/hicolor/

