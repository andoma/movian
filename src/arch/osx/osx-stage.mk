${APPDIR}/Contents/MacOS/$(APPNAME): ${APPPROG} \
			${APPDIR}/Contents/Info.plist \
			${APPDIR}/Contents/Resources/hts.icns \
			${APPDIR}/Contents/Resources/MainMenu.nib
	@echo "APP\t$@"
	@mkdir -p $(dir $@)
	@cp $< $@

${APPDIR}/Contents/%.nib: support/$(APPNAME).app/Contents/%.xib
	@mkdir -p $(dir $@)
	@ibtool --compile $@ $<

${APPDIR}/Contents/%: support/$(APPNAME).app/Contents/%
	@mkdir -p $(dir $@)
	@cp $< $@
