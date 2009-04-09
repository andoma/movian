apifunctions.h:	api.h Makefile
	grep <$< SP_LIBEXPORT|grep -v "#" | sed "s/^SP_LIBEXPORT(\(.*\))\ \(.*\)(\(.*\));/static \1(*f_\2)(\3);/" >$@

	echo >>$@ "static const char *resolvesym(void *handle) {"
	grep <$< SP_LIBEXPORT|grep -v "#" | sed "s/^SP_LIBEXPORT(\(.*\))\ \(.*\)(\(.*\));/if((f_\2=dlsym(handle,\"\2\"))==NULL) return \"\2\";/"  >>$@
	echo >>$@ "return NULL;}"
