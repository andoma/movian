${APPDIR}/Contents/MacOS/$(APPNAME): ${APPPROG} \
			${APPDIR}/Contents/Info.plist \
			${APPDIR}/Contents/Resources/hts.icns
	@echo "APP\t$@"
	@mkdir -p $(dir $@)
	@cp $< $@

${APPDIR}/Contents/%: support/$(APPNAME).app/Contents/%
	@mkdir -p $(dir $@)
	@cp $< $@
