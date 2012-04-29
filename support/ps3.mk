TITLE := Showtime
VERSION := $(shell support/getver.sh)
SFO := $(PSL1GHT)/host/bin/sfo.py
PKG := $(PSL1GHT)/host/bin/pkg.py
ICON0 := $(TOPDIR)/support/ps3icon.png
APPID		:=	HTSS00003
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000

SFOXML          := $(TOPDIR)/support/sfo.xml

EBOOT=${BUILDDIR}/EBOOT.BIN

ELF=${BUILDDIR}/showtime.elf
SELF=${BUILDDIR}/showtime.self

${EBOOT}: support/ps3/eboot.c
	$(CC) $(CFLAGS_com) $(CFLAGS) $(CFLAGS_cfg)  -o $@ $< ${LDFLAGS_EBOOT}
	${STRIP} $@
	sprxlinker $@

${ELF}: ${BUILDDIR}/showtime.bundle
	${STRIP} -o $@ $<
	sprxlinker $@

${SELF}: ${ELF}
	make_self $< $@

$(BUILDDIR)/pkg/USRDIR/showtime.self: ${SELF}
	cp $< $@

$(BUILDDIR)/pkg/USRDIR/EBOOT.BIN: ${EBOOT}
	@mkdir -p $(BUILDDIR)/pkg/USRDIR
	make_self_npdrm $< $@ $(CONTENTID)

$(BUILDDIR)/showtime.pkg: $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN $(BUILDDIR)/pkg/USRDIR/showtime.self
	cp $(ICON0) $(BUILDDIR)/pkg/ICON0.PNG
	$(SFO) --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) $(BUILDDIR)/pkg/PARAM.SFO
	$(PKG) --contentid $(CONTENTID) $(BUILDDIR)/pkg/ $@

$(BUILDDIR)/showtime_geohot.pkg: $(BUILDDIR)/showtime.pkg
	cp $< $@
	package_finalize $@

pkg: $(BUILDDIR)/showtime.pkg $(BUILDDIR)/showtime_geohot.pkg
self: ${SELF}

install: $(BUILDDIR)/showtime.pkg
	cp $< $(PS3INSTALL)/showtime.pkg
	sync

$(BUILDDIR)/dist/showtime-$(VERSION).self: ${SELF}
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILDDIR)/dist/showtime-$(VERSION).pkg: $(BUILDDIR)/showtime.pkg
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILDDIR)/dist/showtime_geohot-$(VERSION).pkg: $(BUILDDIR)/showtime_geohot.pkg
	@mkdir -p $(dir $@)
	cp $< $@

dist:  $(BUILDDIR)/dist/showtime-$(VERSION).self $(BUILDDIR)/dist/showtime-$(VERSION).pkg $(BUILDDIR)/dist/showtime_geohot-$(VERSION).pkg

upgrade: ${SELF}
	curl --data-binary @$< http://$(PS3HOST):42000/showtime/replace
