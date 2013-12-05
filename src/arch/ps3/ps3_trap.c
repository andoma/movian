/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include <stdio.h>

#include "showtime.h"
#include "fileaccess/fileaccess.h"
#include "arch/halloc.h"
#include "ps3.h"
#include "ps3_threads.h"

static char *symbuf;

void
load_syms(void)
{
  char sympath[256];
  char errbuf[256];

  snprintf(sympath, sizeof(sympath), "%s/showtime.syms", showtime_dataroot());

  my_trace("sympath: %s\n", sympath);

  fa_handle_t *fh = fa_open(sympath, errbuf, sizeof(errbuf));

  if(fh == NULL) {
    my_trace("Unable to open symbol file %s -- %s",
	  sympath, errbuf);
    return;
  }

  int size = fa_fsize(fh);
  char *buf = halloc(size + 1);

  int r = fa_read(fh, buf, size);
  if(r != size) {
    my_trace("Unable to read %d bytes", size);
    hfree(buf, size+1);
  } else {
    buf[size] = 0;
    my_trace("Loaded symbol table %d bytes to %p",
	  size, buf);
    symbuf = buf;
  }
  fa_close(fh);
}





#define BT_MAX    64
#define BT_IGNORE 1

static int
backtrace(void **vec)
{

#define	BT_FRAME(i)							\
  if ((i) < BT_IGNORE + BT_MAX) {					\
    void *p;								\
    if (__builtin_frame_address(i) == 0)				\
      return i - BT_IGNORE;						\
    p = __builtin_return_address(i);					\
    if (p == NULL || (intptr_t)p < 0x11000)				\
      return i - BT_IGNORE;						\
    if (i >= BT_IGNORE) {						\
      vec[i - BT_IGNORE] = p-4;						\
    }									\
  } else								\
    return i - BT_IGNORE;

	BT_FRAME(0)
	BT_FRAME(1)
	BT_FRAME(2)
	BT_FRAME(3)
	BT_FRAME(4)
	BT_FRAME(5)
	BT_FRAME(6)
	BT_FRAME(7)
	BT_FRAME(8)
	BT_FRAME(9)

	BT_FRAME(10)
	BT_FRAME(11)
	BT_FRAME(12)
	BT_FRAME(13)
	BT_FRAME(14)
	BT_FRAME(15)
	BT_FRAME(16)
	BT_FRAME(17)
	BT_FRAME(18)
	BT_FRAME(19)

	BT_FRAME(20)
	BT_FRAME(21)
	BT_FRAME(22)
	BT_FRAME(23)
	BT_FRAME(24)
	BT_FRAME(25)
	BT_FRAME(26)
	BT_FRAME(27)
	BT_FRAME(28)
	BT_FRAME(29)

	BT_FRAME(30)
	BT_FRAME(31)
	BT_FRAME(32)
	BT_FRAME(33)
	BT_FRAME(34)
	BT_FRAME(35)
	BT_FRAME(36)
	BT_FRAME(37)
	BT_FRAME(38)
	BT_FRAME(39)

	BT_FRAME(40)
	BT_FRAME(41)
	BT_FRAME(42)
	BT_FRAME(43)
	BT_FRAME(44)
	BT_FRAME(45)
	BT_FRAME(46)
	BT_FRAME(47)
	BT_FRAME(48)
	BT_FRAME(49)

	BT_FRAME(50)
	BT_FRAME(51)
	BT_FRAME(52)
	BT_FRAME(53)
	BT_FRAME(54)
	BT_FRAME(55)
	BT_FRAME(56)
	BT_FRAME(57)
	BT_FRAME(58)
	BT_FRAME(59)

	BT_FRAME(60)
	BT_FRAME(61)
	BT_FRAME(62)
	BT_FRAME(63)
	BT_FRAME(64)
	  return 64;
}


void
__assert_func(const char *file, int line,
	      const char *func, const char *failedexpr);

void
__assert_func(const char *file, int line,
	      const char *func, const char *failedexpr)
{
  panic("Assertion failed %s:%d in %s (%s)", file, line, func, failedexpr);
}


static void
resolve_syms(void **ptr, const char **symvec, int *symoffset, int frames)
{
  char *s = symbuf;

  int i;
  for(i = 0; i < frames; i++) {
    symvec[i] = NULL;
    symoffset[i] = 0;
  }

  if(s == NULL)
    return;
  
  while(s) {
    int64_t addr = strtol(s, NULL, 16);
    if(addr > 0x10000) {
      for(i = 0; i < frames; i++) {
	int64_t a0 = (intptr_t)ptr[i];
	if(a0 >= addr) {
	  symvec[i] = s + 17;
	  symoffset[i] = a0 - addr;
	}
      }
    }

    s = strchr(s, '\n');
    if(s == NULL)
      return;
    *s++ = 0;
  }
}

void
panic(const char *fmt, ...)
{
  va_list ap;
  void *vec[64];
  const char *sym[64];
  int symoffset[64];

  va_start(ap, fmt);
  tracev(0, TRACE_EMERG, "PANIC", fmt, ap);
  va_end(ap);

  int frames = backtrace(vec);
  int i;
  resolve_syms(vec, sym, symoffset, frames);
  for(i = 0; i < frames; i++) 
    if(sym[i])
      TRACE(TRACE_EMERG, "BACKTRACE", "%p: %s+0x%x", vec[i], sym[i], symoffset[i]);
    else
      TRACE(TRACE_EMERG, "BACKTRACE", "%p", vec[i]);

  hts_thread_t tid = hts_thread_current();

  TRACE(TRACE_EMERG, "PANIC", "Thread list (self=0x%lx)", tid);

  thread_info_t *ti;
  LIST_FOREACH(ti, &threads, link) 
    TRACE(TRACE_EMERG, "PANIC", "0x%lx: %s", ti->id, ti->name);
  exit(1);
}
