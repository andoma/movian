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

#if defined(__i386__) || defined(__x86_64__) || defined(__arm__)

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
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "showtime.h"
#include "linux.h"

#define TRAPMSG(fmt...) TRACE(TRACE_EMERG, "CRASH", fmt)

#define MAXFRAMES 100

static char line1[200];
static char libs[2048];
static char self[PATH_MAX];

static void
sappend(char *buf, size_t l, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buf + strlen(buf), l - strlen(buf), fmt, ap);
  va_end(ap);
}


/**
 *
 */
static int
add2lineresolve(const char *binary, void *addr, char *buf0, size_t buflen)
{
  char *buf = buf0;
  int fd[2], r, f;
  const char *argv[5];
  pid_t p;
  char addrstr[30], *cp;

  if(access("/usr/bin/addr2line", X_OK))
    return -1;

  argv[0] = "addr2line";
  argv[1] = "-e";
  argv[2] = binary;
  argv[3] = addrstr;
  argv[4] = NULL;

  snprintf(addrstr, sizeof(addrstr), "%p", (void *)((intptr_t)addr-1));

  if(pipe(fd) == -1)
    return -1;

  if((p = fork()) == -1)
    return -1;

  if(p == 0) {
    close(0);
    close(2);
    close(fd[0]);
    dup2(fd[1], 1);
    close(fd[1]);
    if((f = open("/dev/null", O_RDWR)) == -1)
      _exit(1);

    dup2(f, 0);
    dup2(f, 2);
    close(f);

    execve("/usr/bin/addr2line", (char *const *) argv, environ);
    _exit(0);
  }

  close(fd[1]);
  *buf = 0;
  while(buflen > 1) {
    r = read(fd[0], buf, buflen);
    if(r < 1)
      break;

    buf += r;
    buflen -= r;
    *buf = 0;
    cp = strchr(buf0, '\n');
    if(cp != NULL) {
      *cp = 0;
      break;
    }
  }
  close(fd[0]);
  return 0;
}


/**
 *
 */
static void
addr2text(char *out, size_t outlen, void *ptr)
{
  Dl_info dli;
  char buf[256];
  int r = dladdr(ptr, &dli);
  
  if(r && dli.dli_sname != NULL && dli.dli_saddr != NULL) {
    snprintf(out, outlen, "%s+0x%tx  (%s)",
	     dli.dli_sname, ptr - dli.dli_saddr, dli.dli_fname);
    return;
  }
  
  if(self[0] && !add2lineresolve(self, ptr, buf, sizeof(buf))) {
    snprintf(out, outlen, "%s %p", buf, ptr);
    return;
  }

  if(dli.dli_fname != NULL && dli.dli_fbase != NULL) {
    snprintf(out, outlen, "%s %p", dli.dli_fname, ptr);
    return;
  }
  snprintf(out, outlen, "%p", ptr);
}



static void 
traphandler(int sig, siginfo_t *si, void *UC)
{
  ucontext_t *uc = UC;
  static void *frames[MAXFRAMES];
  char buf[256];
  int nframes = backtrace(frames, MAXFRAMES);
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

  addr2text(buf, sizeof(buf), si->si_addr);

  TRAPMSG("Fault address %s (%s)", buf, reason ?: "N/A");

  TRAPMSG("Loaded libraries: %s ", libs);

#if defined(__arm__) 
  TRAPMSG("   trap_no = %08lx", uc->uc_mcontext.trap_no);
  TRAPMSG("error_code = %08lx", uc->uc_mcontext.error_code);
  TRAPMSG("   oldmask = %08lx", uc->uc_mcontext.oldmask);
  TRAPMSG("        R0 = %08lx", uc->uc_mcontext.arm_r0);
  TRAPMSG("        R1 = %08lx", uc->uc_mcontext.arm_r1);
  TRAPMSG("        R2 = %08lx", uc->uc_mcontext.arm_r2);
  TRAPMSG("        R3 = %08lx", uc->uc_mcontext.arm_r3);
  TRAPMSG("        R4 = %08lx", uc->uc_mcontext.arm_r4);
  TRAPMSG("        R5 = %08lx", uc->uc_mcontext.arm_r5);
  TRAPMSG("        R6 = %08lx", uc->uc_mcontext.arm_r6);
  TRAPMSG("        R7 = %08lx", uc->uc_mcontext.arm_r7);
  TRAPMSG("        R8 = %08lx", uc->uc_mcontext.arm_r8);
  TRAPMSG("        R9 = %08lx", uc->uc_mcontext.arm_r9);
  TRAPMSG("       R10 = %08lx", uc->uc_mcontext.arm_r10);
  TRAPMSG("        FP = %08lx", uc->uc_mcontext.arm_fp);
  TRAPMSG("        IP = %08lx", uc->uc_mcontext.arm_ip);
  TRAPMSG("        SP = %08lx", uc->uc_mcontext.arm_sp);
  TRAPMSG("        LR = %08lx", uc->uc_mcontext.arm_lr);
  TRAPMSG("        PC = %08lx", uc->uc_mcontext.arm_pc);
  TRAPMSG("      CPSR = %08lx", uc->uc_mcontext.arm_cpsr);
  TRAPMSG("fault_addr = %08lx", uc->uc_mcontext.fault_address);

#else
  char tmpbuf[1024];
  snprintf(tmpbuf, sizeof(tmpbuf), "Register dump [%d]: ", NGREG);
  for(i = 0; i < NGREG; i++) {
#if __WORDSIZE == 64
    sappend(tmpbuf, sizeof(tmpbuf), "%016llx ", uc->uc_mcontext.gregs[i]);
#else
    sappend(tmpbuf, sizeof(tmpbuf), "%08x ", uc->uc_mcontext.gregs[i]);
#endif
  }
  TRAPMSG("%s", tmpbuf);
#endif

  TRAPMSG("STACKTRACE (%d frames)", nframes);

  for(i = 0; i < nframes; i++) {
    addr2text(buf, sizeof(buf), frames[i]);
    TRAPMSG("%s", buf);
  }
}



static int
callback(struct dl_phdr_info *info, size_t size, void *data)
{
  if(info->dlpi_name[0])
    sappend(libs, sizeof(libs), "%s ", info->dlpi_name);
  return 0;
}


/**
 *
 */
void
trap_init(void)
{
  struct sigaction sa, old;
  char path[256];
  int r;

  r = readlink("/proc/self/exe", self, sizeof(self) - 1);
  if(r == -1)
    self[0] = 0;
  else
    self[r] = 0;

  snprintf(line1, sizeof(line1),
	   "PRG: Showtime (%s) EXE: %s, CWD: %s ", htsversion_full,
	   self, getcwd(path, sizeof(path)));

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

void trap_init(void);

void
trap_init(void)
{

}
#endif
