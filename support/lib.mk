# Make helper for building libraries

.SUFFIXES: .so

AR?=ar
LD?=ld
RANLIB?=ranlib


CFLAGS += -g -Wall -Werror -funsigned-char -O2 $(LIB_CFLAGS)

ifeq ($(LIB_BUILD_MODE), shared)
.OBJDIR=        ${BUILDDIR}/ext/$(L)/obj-shared
LIB = $(L).so
else
.OBJDIR=        ${BUILDDIR}/ext/$(L)/obj-static
LIB = $(L).a
endif


OBJS = $(patsubst %.c,%.o, $(SRCS))
DEPS= ${OBJS:%.o=%.d}

all:	$(LIB)

install: $(SHLIB) $(LIB) install-headers
	mkdir -p $(LIBS_INSTALL)
	install $(.OBJDIR)/$(LIB) $(LIBS_INSTALL)

HEADERS_T=$(HEADERS:.h=.h.t)

.PHONY: install-headers
.PHONY: ${HEADERS_T}

install-headers: ${HEADERS_T}
${HEADERS_T}:
	@rt=$(@:.t=) && bn=$(notdir $(@:.t=)) &&\
	(cmp -s $${rt} ${INCLUDES_INSTALL}/$${bn}> /dev/null 2>&1 ||\
	(echo "installing $${bn} in ${INCLUDES_INSTALL}/$${bn}"; \
	mkdir -p  ${INCLUDES_INSTALL}; \
	cp $${rt} ${INCLUDES_INSTALL}/$${bn} ))

ifeq ($(LIB_BUILD_MODE), shared)

${LIB}: $(OBJS)
	cd $(.OBJDIR) && $(CC) -shared -o $@ $(OBJS)

else

${LIB}: $(OBJS)
	cd $(.OBJDIR) && $(AR) rc $@ $(OBJS)
	cd $(.OBJDIR) && $(RANLIB) $@

endif

.c.o:
	mkdir -p $(.OBJDIR) && cd $(.OBJDIR) && $(CC) -MD $(CFLAGS) -c -o $@ $(CURDIR)/$<

clean:
	rm -rf core* obj-*
	find . -name "*~" | xargs rm -f

vpath %.o ${.OBJDIR}
vpath ${LIB} ${.OBJDIR}

# include dependency files if they exist
$(addprefix ${.OBJDIR}/, ${DEPS}): ;
$(addprefix ${.OBJDIR}/, ${SHDEPS}): ;
-include $(addprefix ${.OBJDIR}/, ${DEPS})
-include $(addprefix ${.OBJDIR}/, ${SHDEPS})
