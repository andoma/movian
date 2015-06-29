${APPDIR}/Contents/MacOS/showtime: ${APPPROG} \
			${APPDIR}/Contents/Info.plist \
			${APPDIR}/Contents/Resources/hts.icns \
			${APPDIR}/Contents/Resources/MainMenu.nib \
			${SPOTIFY_INST_FILES}
	@echo "APP\t$@"
	@mkdir -p $(dir $@)
	@cp $< $@

${APPDIR}/Contents/%.nib: support/Showtime.app/Contents/%.xib
	@mkdir -p $(dir $@)
	@ibtool --compile $@ $<

${APPDIR}/Contents/%: support/Showtime.app/Contents/%
	@mkdir -p $(dir $@)
	@cp $< $@

$(SPOTIFY_INST_PATH)/%: ${SPOTIFY_FRAMEWORK}/%
	@mkdir -p $(dir $@)
	@rm -f $@
	@cp -a $< $@
