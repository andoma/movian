
prefix ?= $(INSTALLPREFIX)
INSTBIN= $(prefix)/bin
INSTMAN= $(prefix)/share/man1
INSTSHARE= $(prefix)/share/hts/showtime

install: ${PROG}
	mkdir -p $(INSTBIN)
	cd $(.OBJDIR) && install -s ${PROG} $(INSTBIN)

	mkdir -p $(INSTMAN)
	cd man && install ${MAN} $(INSTMAN)

	find themes -type d |grep -v .svn | awk '{print "$(INSTSHARE)/"$$0}' | xargs mkdir -p 
	find themes -type f |grep -v .svn | awk '{print $$0 " $(INSTSHARE)/"$$0}' | xargs -n2 cp

uninstall:
	rm -f $(INSTBIN)/${PROG}
	rm -f $(INSTMAN)/${MAN}
	rm -rf $(INSTSHARE)
