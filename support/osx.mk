${PROG}: Showtime.app

clean: Showtime.app-clean

Showtime.app-clean:
	rm -rf ${APPDIR}

Showtime.app: \
	${APPDIR}/Contents/MacOS \
	${APPDIR}/Contents/Info.plist \
	${APPDIR}/Contents/Resources/hts.icns \
	${APPDIR}/Contents/Resources/MainMenu.nib

.PHONY: ${APPDIR}/Contents/MacOS
${APPDIR}/Contents/MacOS:
	mkdir -p $@

$(APPDIR)/Contents/Info.plist: osx/Info.plist
	mkdir -p `dirname $@`
	cp $< $@

$(APPDIR)/Contents/Resources/hts.icns: osx/hts.icns
	mkdir -p `dirname $@`
	cp $< $@
        
$(APPDIR)/Contents/Resources/MainMenu.nib: osx/MainMenu.xib
	mkdir -p `dirname $@`
	ibtool --compile $@ $<

.PHONY: Showtime.dmg
Showtime.dmg:
	support/osxchecknonsyslink.sh ${PROG}
	support/mkdmg ${APPDIR} Showtime osx/hts.icns Showtime.dmg

