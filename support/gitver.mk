
ifeq ($(shell uname),Darwin)
GITVER_MD5 := md5
else
GITVER_MD5 := md5sum
endif

GITVER_VARGUARD = $(1)_GUARD_$(shell echo $($(1)) | ${GITVER_MD5} | cut -d ' ' -f 1)

GIT_DESCRIBE_OUTPUT := $(shell git describe --dirty --abbrev=5 2>/dev/null | sed  -e 's/-/./g')

${BUILDDIR}/version_git.h: ${BUILDDIR}/gitver/$(call GITVER_VARGUARD,GIT_DESCRIBE_OUTPUT)
	echo >$@ "#define VERSION_GIT \"${GIT_DESCRIBE_OUTPUT}\""

${BUILDDIR}/gitver/$(call GITVER_VARGUARD,GIT_DESCRIBE_OUTPUT):
	@rm -rf "${BUILDDIR}/gitver"
	@mkdir -p "${BUILDDIR}/gitver"
	@touch $@
