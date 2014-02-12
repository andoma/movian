#
#  Showtime mediacenter
#  Copyright (C) 2007-2011 Andreas Öman
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.

.NOTPARALLEL:

.SUFFIXES:
SUFFIXES=

C ?= ${CURDIR}

include ${C}/config.default
BUILDDIR ?= ${C}/build.${BUILD}
OPTFLAGS ?= -O2
include ${BUILDDIR}/config.mak


export C
export BUILDDIR
export OPTFLAGS

.PHONY: all ${C}/config.default ${BUILDDIR}/config.mak

all: ${STAMPS}
	${MAKE} -f ${C}/showtime.mk

%:: ${STAMPS}
	${MAKE} -f ${C}/showtime.mk $@



$(BUILDDIR)/stamps/%.stamp:
	${MAKE} -f ${C}/ext/$*.mk build
	@mkdir -p $(dir $@)
	touch $@
