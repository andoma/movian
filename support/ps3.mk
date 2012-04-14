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


${BIN}.bundle: ${BUILDDIR}/showtime.bundle
	${STRIP} -o $@ $<
	sprxlinker $@

${BIN}.zipbundle: ${BUILDDIR}/showtime.zipbundle
	${STRIP} -o $@ $<
	sprxlinker $@

$(BUILDDIR)/showtime.self: ${BIN}.bundle
	$(SELF) $< $@

$(BUILDDIR)/pkg/USRDIR/EBOOT.BIN: ${BIN}.zipbundle
	@mkdir -p $(BUILDDIR)/pkg/USRDIR
	make_self_npdrm $< $@ $(CONTENTID)

$(BUILDDIR)/showtime.pkg: $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN
	cp $(ICON0) $(BUILDDIR)/pkg/ICON0.PNG
	cp ${BUILDDIR}/zipbundles/*.zip $(BUILDDIR)/pkg/USRDIR/
	$(SFO) --title "$(TITLE)" --appid "$(APPID)" -f $(SFOXML) $(BUILDDIR)/pkg/PARAM.SFO
	$(PKG) --contentid $(CONTENTID) $(BUILDDIR)/pkg/ $@

$(BUILDDIR)/showtime_geohot.pkg: $(BUILDDIR)/showtime.pkg
	cp $< $@
	package_finalize $@

pkg: $(BUILDDIR)/showtime.pkg $(BUILDDIR)/showtime_geohot.pkg
self: $(BUILDDIR)/showtime.self

install: $(BUILDDIR)/showtime.pkg
	cp $< $(PS3INSTALL)/showtime.pkg
	sync

$(BUILDDIR)/dist/showtime-$(VERSION).self: $(BUILDDIR)/showtime.self
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILDDIR)/dist/showtime-$(VERSION).pkg: $(BUILDDIR)/showtime.pkg
	@mkdir -p $(dir $@)
	cp $< $@

$(BUILDDIR)/dist/showtime_geohot-$(VERSION).pkg: $(BUILDDIR)/showtime_geohot.pkg
	@mkdir -p $(dir $@)
	cp $< $@

dist:  $(BUILDDIR)/dist/showtime-$(VERSION).self $(BUILDDIR)/dist/showtime-$(VERSION).pkg $(BUILDDIR)/dist/showtime_geohot-$(VERSION).pkg

$(BUILDDIR)/devupgrade/EBOOT.BIN: ${BIN}.bundle
	@mkdir -p $(dir $@)
	make_self_npdrm $< $@ $(CONTENTID)

upgrade: $(BUILDDIR)/devupgrade/EBOOT.BIN
	curl --data-binary @$< http://$(PS3HOST):42000/showtime/replace
