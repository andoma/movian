/**
 *  Crash handling
 *  Copyright (C) 2010 Andreas Öman
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
 */

#include "arch.h"


#if defined(__i386__) || defined(__x86_64__)

// Only do this on x86 for now

#define _GNU_SOURCE
#include <link.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <ucontext.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "showtime.h"

#define TRAPMSG(fmt...) trace(TRACE_NO_PROP, TRACE_ERROR, "CRASH", fmt)

#define MAXFRAMES 100

static char line1[200];
static char tmpbuf[1024];
static char libs[2048];

static void
sappend(char *buf, size_t l, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buf + strlen(buf), l - strlen(buf), fmt, ap);
  va_end(ap);
}



static void 
traphandler(int sig, siginfo_t *si, void *UC)
{
  ucontext_t *uc = UC;
  static void *frames[MAXFRAMES];
  int nframes = backtrace(frames, MAXFRAMES);
  Dl_info dli;
  int i;
  const char *reason = NULL;

  TRAPMSG("Signal: %d in %s ", sig, line1);

  switch(sig) {
  case SIGSEGV:
    switch(si->si_code) {
    case SEGV_MAPERR:  reason = "Address not mapped"; break;
    case SEGV_ACCERR:  reason = "Access error"; break;
    }
    break;

  case SIGFPE:
    switch(si->si_code) {
    case FPE_INTDIV:  reason = "Integer division by zero"; break;
    }
    break;
  }

  TRAPMSG("Fault address %p (%s)",
	       si->si_addr, reason ?: "N/A");

  TRAPMSG("Loaded libraries: %s ", libs);
  snprintf(tmpbuf, sizeof(tmpbuf), "Register dump [%d]: ", NGREG);

  for(i = 0; i < NGREG; i++) {
#if __WORDSIZE == 64
    sappend(tmpbuf, sizeof(tmpbuf), "%016llx ", uc->uc_mcontext.gregs[i]);
#else
    sappend(tmpbuf, sizeof(tmpbuf), "%08x ", uc->uc_mcontext.gregs[i]);
#endif
  }
  TRAPMSG("%s", tmpbuf);

  TRAPMSG("STACKTRACE");

  for(i = 0; i < nframes; i++) {

    
    if(dladdr(frames[i], &dli)) {

      if(dli.dli_sname != NULL && dli.dli_saddr != NULL) {
      	TRAPMSG("%s+0x%tx  (%s)",
		     dli.dli_sname,
		     frames[i] - dli.dli_saddr,
		     dli.dli_fname);
	continue;
      }

      if(dli.dli_fname != NULL && dli.dli_fbase != NULL) {
      	TRAPMSG("%s %p",
 		     dli.dli_fname,
		     frames[i]);
	continue;
      }


      TRAPMSG("%p", frames[i]);
    }
  }
}



static int
callback(struct dl_phdr_info *info, size_t size, void *data)
{
  if(info->dlpi_name[0])
    sappend(libs, sizeof(libs), "%s ", info->dlpi_name);
  return 0;
}


void
trap_init(void)
{
  uint8_t digest[20];
  struct sigaction sa, old;
  char path[256];

  memset(digest, 0, sizeof(digest));

  
  snprintf(line1, sizeof(line1),
	   "PRG: Showtime (%s) "
	   "[%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
	   "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x] "
	   "CWD: %s ", htsversion_full,
	   digest[0],
	   digest[1],
	   digest[2],
	   digest[3],
	   digest[4],
	   digest[5],
	   digest[6],
	   digest[7],
	   digest[8],
	   digest[9],
	   digest[10],
	   digest[11],
	   digest[12],
	   digest[13],
	   digest[14],
	   digest[15],
	   digest[16],
	   digest[17],
	   digest[18],
	   digest[19],
	   getcwd(path, sizeof(path)));

  dl_iterate_phdr(callback, NULL);
  

  memset(&sa, 0, sizeof(sa));

  sigset_t m;
  sigemptyset(&m);
  sigaddset(&m, SIGSEGV);
  sigaddset(&m, SIGBUS);
  sigaddset(&m, SIGILL);
  sigaddset(&m, SIGABRT);
  sigaddset(&m, SIGFPE);

  sa.sa_sigaction = traphandler;
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigaction(SIGSEGV, &sa, &old);
  sigaction(SIGBUS,  &sa, &old);
  sigaction(SIGILL,  &sa, &old);
  sigaction(SIGABRT, &sa, &old);
  sigaction(SIGFPE,  &sa, &old);

  sigprocmask(SIG_UNBLOCK, &m, NULL);
}

#else

void
trap_init(const char *ver)
{

}
#endif
