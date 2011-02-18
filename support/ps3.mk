TITLE := Showtime
SELF := $(PSL1GHT)/host/bin/fself.py
SFO := $(PSL1GHT)/host/bin/sfo.py
PKG := $(PSL1GHT)/host/bin/pkg.py
ICON0 := $(TOPDIR)/support/ps3icon.png
APPID		:=	HTSS00003
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000

SFOXML          := $(PSL1GHT)/host/bin/sfo.xml

BIN=${BUILDDIR}/showtime.elf


${BIN}: ${BUILDDIR}/showtime
	${STRIP} -o $@ $<
	sprxlinker $@

$(BUILDDIR)/showtime.self: ${BIN}
	$(SELF) $< $@

$(BUILDDIR)/showtime.pkg: ${BIN}
	@mkdir -p $(BUILDDIR)/pkg
	@mkdir -p $(BUILDDIR)/pkg/USRDIR
	@cp $(ICON0) $(BUILDDIR)/pkg/ICON0.PNG
	make_self_npdrm ${BIN} $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN $(CONTENTID)

	$(SFO) --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) $(BUILDDIR)/pkg/PARAM.SFO
	$(PKG) --contentid $(CONTENTID) $(BUILDDIR)/pkg/ $@
	package_finalize $(BUILDDIR)/showtime.pkg

pkg: $(BUILDDIR)/showtime.pkg
self: $(BUILDDIR)/showtime.self

install: $(BUILDDIR)/showtime.pkg
	cp $< $(PS3INSTALL)/showtime.pkg
	sync
