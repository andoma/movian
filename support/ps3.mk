TITLE := Showtime
VERSION := $(shell support/getver.sh)
SELF := $(PSL1GHT)/host/bin/fself.py
SFO := $(PSL1GHT)/host/bin/sfo.py
PKG := $(PSL1GHT)/host/bin/pkg.py
ICON0 := $(TOPDIR)/support/ps3icon.png
APPID		:=	HTSS00003
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000

SFOXML          := $(TOPDIR)/support/sfo.xml

BIN=${BUILDDIR}/showtime.elf


${BIN}: ${BUILDDIR}/showtime
	${STRIP} -o $@ $<
	sprxlinker $@

$(BUILDDIR)/showtime.self: ${BIN}
	$(SELF) $< $@

$(BUILDDIR)/pkg/USRDIR/EBOOT.BIN: ${BIN}
	@mkdir -p $(BUILDDIR)/pkg
	@mkdir -p $(BUILDDIR)/pkg/USRDIR
	make_self_npdrm ${BIN} $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN $(CONTENTID)

$(BUILDDIR)/showtime.pkg: $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN
	make_self_npdrm ${BIN} $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN $(CONTENTID)

	@cp $(ICON0) $(BUILDDIR)/pkg/ICON0.PNG
	$(SFO) --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) $(BUILDDIR)/pkg/PARAM.SFO
	$(PKG) --contentid $(CONTENTID) $(BUILDDIR)/pkg/ $@
	package_finalize $(BUILDDIR)/showtime.pkg

pkg: $(BUILDDIR)/showtime.pkg
self: $(BUILDDIR)/showtime.self

install: $(BUILDDIR)/showtime.pkg
	cp $< $(PS3INSTALL)/showtime.pkg
	sync

$(BUILDDIR)/dist/showtime-$(VERSION).self: $(BUILDDIR)/showtime.self
	cp $< $@

$(BUILDDIR)/dist/showtime-$(VERSION).pkg: $(BUILDDIR)/showtime.pkg
	cp $< $@

$(BUILDDIR)/dist:
	mkdir -p $@

dist:  $(BUILDDIR)/dist $(BUILDDIR)/dist/showtime-$(VERSION).self $(BUILDDIR)/dist/showtime-$(VERSION).pkg

upgrade: $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN
	 curl --data-binary @$< http://$(PS3HOST):42000/showtime/replace
