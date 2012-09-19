/*
 *  Linux specific stuff
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



#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

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

static prop_t *p_sys;
static int isvalid;
static int64_t last_idle[17];
static int64_t last_tot[17];
static callout_t timer;
static prop_t *p_cpuroot;
static prop_t *p_cpu[17];
static prop_t *p_load[16];

/**
 *
 */
static int
cpu_monitor_do(void)
{
  int ret = 0, id;
  char data[1000];
  uint64_t v1, v2, v3, v4, v5, v6, v7, di, tot, dt;
  char buf[100];
  char s1[22];
  FILE *f;

  f = fopen("/proc/stat", "r");
  if(f == NULL)
    return 0;

  while(fgets(data, sizeof(data), f) != NULL) {
    if(sscanf(data, "%20s %"PRId64" %"PRId64" %"PRId64" "
	      "%"PRId64" %"PRId64" %"PRId64" %"PRId64, 
	      s1, &v1, &v2, &v3, &v4, &v5, &v6, &v7) != 8)
      continue;
    if(strncmp(s1, "cpu", 3))
      continue;

    tot = v1+v2+v3+v4+v5+v6+v7;


    if(!strcmp(s1, "cpu")) {
      id = 16;
    } else {
      id = atoi(s1 + 3);
      if(id < 0 || id > 16)
	continue;
    }
    
    if(isvalid) {
      di = v4  - last_idle[id];
      dt = tot - last_tot[id];

      if(id < 16) {

	if(p_cpu[id] == NULL) {
	  p_cpu[id] = prop_create(p_cpuroot, NULL);

	  snprintf(buf, sizeof(buf), "CPU%d", id);
	  prop_set_string(prop_create(p_cpu[id], "name"), buf);
	  p_load[id] = prop_create(p_cpu[id], "load");
	}
	float v = 1.0 - ((float)di / (float)dt);
	if(v < 0) v = 0;
	else if(v > 1) v = 1;

	prop_set_float(p_load[id], v);
      }
    }
    last_idle[id] = v4;
    last_tot[id] = tot;
  }
  isvalid = 1;
  prop_set_int(prop_create(prop_create(p_sys, "cpuinfo"),
			   "available"), 1);
  fclose(f);
  return ret;
}


/**
 *
 */
static int
meminfo_do(void)
{
  int ret = 0;
  char data[1000];
  uint64_t v1;
  char s1[64];
  FILE *f;
  prop_t *mem = prop_create(p_sys, "mem");

  struct mallinfo mi = mallinfo();

  prop_set_int(prop_create(mem, "arena"), (mi.hblks + mi.arena) / 1024);
  prop_set_int(prop_create(mem, "unusedChunks"), mi.ordblks);
  prop_set_int(prop_create(mem, "activeMem"), mi.uordblks / 1024);
  prop_set_int(prop_create(mem, "inactiveMem"), mi.fordblks / 1024);

  f = fopen("/proc/meminfo", "r");
  if(f == NULL)
    return 0;

  while(fgets(data, sizeof(data), f) != NULL) {
    if(sscanf(data, "%60s %"PRId64, s1, &v1) != 2)
      continue;

    if(!strcmp(s1, "MemTotal:"))
      prop_set_int(prop_create(mem, "systotal"), v1);
    else if(!strcmp(s1, "MemFree:"))
      prop_set_int(prop_create(mem, "sysfree"), v1);

  }

  fclose(f);
  return ret;
}


/**
 *
 */
static void 
timercb(callout_t *c, void *aux)
{
  callout_arm(&timer, timercb, NULL, 1);
  cpu_monitor_do();
  meminfo_do();
}


/**
 *
 */
void
linux_init_monitors(void)
{
  p_sys = prop_create(prop_get_global(), "system");
  p_cpuroot = prop_create(prop_create(p_sys, "cpuinfo"), "cpus");
  timercb(NULL, NULL);
}

