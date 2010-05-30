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


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "linux.h"
#include "misc/callout.h"
#include "prop.h"



static int isvalid;
static int64_t last_time[8][8];
static callout_t timer;
static prop_t *p_cpuroot;
static prop_t *p_cpu[8];
static prop_t *p_load[8];

static void
cpu_monitor_do(void)
{
  int i, j, fd, r;
  char buf[PATH_MAX];
  char data[100];
  uint64_t v, d;
  uint64_t sum;

  for(i = 0; i < 8; i++) {
    sum = 0;
    for(j = 0; j < 8; j++) {
      snprintf(buf, sizeof(buf),
	       "/sys/devices/system/cpu/cpu%d/cpuidle/state%d/time", i, j);
      fd = open(buf, O_RDONLY);
      if(fd == -1)
	break;
      r = read(fd, data, sizeof(data));
      close(fd);
      if(r < 1)
	break;
      data[r] = 0;
      v = strtoll(data, NULL, 10);

      if(isvalid) {
	d = v - last_time[i][j];
	sum += d;
      }
      last_time[i][j] = v;
    }
    if(j == 0)
      break;

    if(p_cpu[i] == NULL) {
      p_cpu[i] = prop_create(p_cpuroot, NULL);

      snprintf(buf, sizeof(buf), "CPU%d", i);
      prop_set_string(prop_create(p_cpu[i], "name"), buf);
      p_load[i] = prop_create(p_cpu[i], "load");
    }

    prop_set_float(p_load[i], 1 - (sum / 1000000.0));
  }
  isvalid=1;
}


static void 
timercb(callout_t *c, void *aux)
{
  callout_arm(&timer, timercb, NULL, 1);
  cpu_monitor_do();
}

void
linux_init_cpu_monitor(void)
{
  prop_t *p;

  p = prop_create(prop_get_global(), "cpuinfo");
  prop_set_int(prop_create(p, "available"), 1);

  p_cpuroot = prop_create(p, "cpus");

  cpu_monitor_do();
  callout_arm(&timer, timercb, NULL, 1);
}
