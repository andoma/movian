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



#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/capability.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include <sys/syscall.h>

#include "showtime.h"
#include "misc/callout.h"
#include "prop/prop.h"

#include "arch/posix/posix.h"
#include "arch/arch.h"

#include "linux.h"

/**
 *
 */
const char *
showtime_get_system_type(void)
{
#if defined(__i386__)
  return "Linux/i386";
#elif defined(__x86_64__)
  return "Linux/x86_64";
#elif defined(__arm__)
  return "Linux/arm";
#else
  return "Linux/other";
#endif
}


/**
 *
 */
int
get_system_concurrency(void)
{
  cpu_set_t mask;
  int i, r = 0;

  memset(&mask, 0, sizeof(mask));
  sched_getaffinity(0, sizeof(mask), &mask);
  for(i = 0; i < CPU_SETSIZE; i++)
    if(CPU_ISSET(i, &mask))
      r++;
  return r?:1;
}



/**
 *
 */
void
linux_check_capabilities(void)
{
  struct __user_cap_header_struct x;
  struct __user_cap_data_struct s[3];

  x.version = _LINUX_CAPABILITY_VERSION_3;
  x.pid = getpid();

  if(syscall(SYS_capget, &x, s)) {
    perror("linux_check_capabilities");
    return;
  }

  if(s[0].effective & (1 << CAP_SYS_NICE)) {
    extern int posix_set_thread_priorities;
    posix_set_thread_priorities = 1;
  }
}


/**
 *
 */
void
arch_sync_path(const char *path)
{
  int fd = open(path, O_RDONLY);
  if(fd == -1)
    return;
  syscall(SYS_syncfs, fd);
  close(fd);
}
