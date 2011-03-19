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

$(APPDIR)/Contents/Info.plist: support/osx/Info.plist
	mkdir -p `dirname $@`
	cp $< $@

$(APPDIR)/Contents/Resources/hts.icns: support/osx/hts.icns
	mkdir -p `dirname $@`
	cp $< $@
        
$(APPDIR)/Contents/Resources/MainMenu.nib: support/osx/MainMenu.xib
	mkdir -p `dirname $@`
	ibtool --compile $@ $<

ifdef SPOTIFY_FRAMEWORK
Showtime.app: $(APPDIR)/Contents/Frameworks/libspotify.framework/libspotify

$(APPDIR)/Contents/Frameworks/libspotify.framework/libspotify:
	mkdir -p `dirname $@`
	cp -a \
	  "${SPOTIFY_FRAMEWORK}/Versions" \
	  "${SPOTIFY_FRAMEWORK}/libspotify" \
	  "`dirname $@`"
endif

.PHONY: Showtime.dmg
Showtime.dmg:
	support/osxchecknonsyslink.sh ${PROG}
	support/mkdmg ${APPDIR} Showtime support/osx/hts.icns Showtime.dmg

