TITLE := Showtime
SFO := $(PSL1GHT)/bin/sfo.py
PKG := $(PSL1GHT)/bin/pkg.py
ICON0 := $(TOPDIR)/support/ps3icon.png
APPID		:=	HTSS00003
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000

SFOXML          := $(PSL1GHT)/bin/sfo.xml

BIN=${BUILDDIR}/showtime.elf


${BIN}: ${BUILDDIR}/showtime
	${STRIP} -o $@ $<
	${PSL1GHT}/bin/sprxlinker $@

$(BUILDDIR)/showtime.pkg: ${BIN}
	@mkdir -p $(BUILDDIR)/pkg
	@mkdir -p $(BUILDDIR)/pkg/USRDIR
	@cp $(ICON0) $(BUILDDIR)/pkg/ICON0.PNG
	make_self_npdrm ${BIN} $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN $(CONTENTID)

	$(SFO) --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) $(BUILDDIR)/pkg/PARAM.SFO
	$(PKG) --contentid $(CONTENTID) $(BUILDDIR)/pkg/ $(BUILDDIR)/showtime.pkg
	package_finalize $(BUILDDIR)/showtime.pkg

pkg: $(BUILDDIR)/showtime.pkg

install: $(BUILDDIR)/showtime.pkg
	cp $< $(PS3INSTALL)/showtime.pkg
	sync
