/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#include <stdlib.h>
#include <sys/sysctl.h>

#include <mach/mach.h>
#include <mach/task.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <mach/host_info.h>
#include <mach/task_info.h>

#include "main.h"
#include "misc/callout.h"
#include "prop/prop.h"

static prop_t *p_sys;
static unsigned int *last_total;
static unsigned int *last_idle;
static unsigned int cpu_count;
static callout_t timer;
static prop_t *p_cpuroot;
static prop_t **p_cpu;
static prop_t **p_load;




static void
mem_monitor_do(void)
{
  int mib[6];
  mib[0] = CTL_HW;
  mib[1] = HW_PAGESIZE;

  int pagesize;
  size_t length;
  length = sizeof(pagesize);
  if(sysctl (mib, 2, &pagesize, &length, NULL, 0) < 0)
    return;

  mach_msg_type_number_t count = HOST_VM_INFO_COUNT;

  vm_statistics_data_t vmstat;
  if(host_statistics (mach_host_self (), HOST_VM_INFO,
                      (host_info_t) &vmstat, &count) != KERN_SUCCESS)
    return;

  int total =
    vmstat.wire_count +
    vmstat.active_count +
    vmstat.inactive_count +
    vmstat.free_count;

  prop_t *mem = prop_create(p_sys, "mem");

  prop_set(mem, "systotal", PROP_SET_INT, total / 1024 * pagesize);
  prop_set(mem, "sysfree",  PROP_SET_INT, vmstat.free_count / 1024 * pagesize);

  task_basic_info_64_data_t info;
  unsigned size = sizeof(info);
  task_info(mach_task_self(), TASK_BASIC_INFO_64, (task_info_t) &info, &size);

  prop_set(mem, "activeMem", PROP_SET_INT, (int)(info.resident_size / 1024));
}




static void
cpu_monitor_do(void)
{
  kern_return_t r;
  processor_info_t pinfo;
  mach_msg_type_number_t msg_count;
  unsigned int cpu_count_temp;
  char buf[100];
 
  r = host_processor_info(mach_host_self (),
                          PROCESSOR_CPU_LOAD_INFO,
                          &cpu_count_temp,
                          (processor_info_array_t *)&pinfo,
                          &msg_count);
  if(r != KERN_SUCCESS)
    return;
  
  if(cpu_count != cpu_count_temp) {
    vm_deallocate(mach_task_self(),
                  (vm_address_t)pinfo,
                  (vm_size_t)sizeof(*pinfo) * msg_count);
    return;
  }
  
  int i;
  for(i = 0; i < cpu_count; i++) {
    if(p_cpu[i] == NULL) {
      p_cpu[i] = prop_create(p_cpuroot, NULL);
      
      snprintf(buf, sizeof(buf), "CPU%d", i);
      prop_set_string(prop_create(p_cpu[i], "name"), buf);
      p_load[i] = prop_create(p_cpu[i], "load");      
    }
    
    processor_info_t pi = pinfo + (CPU_STATE_MAX * i);
    
    unsigned int total = (pi[CPU_STATE_USER] +
                          pi[CPU_STATE_SYSTEM] +
                          pi[CPU_STATE_NICE] +
                          pi[CPU_STATE_IDLE]);
    unsigned int idle = pi[CPU_STATE_IDLE];
    
    unsigned int di = idle - last_idle[i];
    unsigned int dt = total - last_total[i];
    last_idle[i] = idle;
    last_total[i] = total;
    
    prop_set_float(p_load[i], 1.0 - ((float)di / (float)dt));
  }
  
  vm_deallocate(mach_task_self(),
                (vm_address_t)pinfo,
                (vm_size_t)sizeof(*pinfo) * msg_count);
}

static void 
timercb(callout_t *c, void *aux)
{
  callout_arm(&timer, timercb, NULL, 1);
  cpu_monitor_do();
  mem_monitor_do();
}

static void
darwin_init_cpu_monitor(void)
{
  kern_return_t r;
  processor_info_t pinfo;
  mach_msg_type_number_t msg_count;

  p_sys = prop_create(prop_get_global(), "system");

  r = host_processor_info(mach_host_self (),
                          PROCESSOR_CPU_LOAD_INFO,
                          &cpu_count,
                          (processor_info_array_t *)&pinfo,
                          &msg_count);
  if(r != KERN_SUCCESS) {
    TRACE(TRACE_ERROR, "darwin",
          "host_processor_info(PROCESSOR_CPU_LOAD_INFO) failed %d", r);
    return;
  }
  
  p_cpu = calloc(cpu_count, sizeof(prop_t *));
  p_load  = calloc(cpu_count, sizeof(prop_t *));
  last_total = calloc(cpu_count, sizeof(unsigned int));
  last_idle = calloc(cpu_count, sizeof(unsigned int));
  
  prop_set_int(prop_create(prop_create(p_sys, "cpuinfo"), "available"), 1);
  p_cpuroot =  prop_create(prop_create(p_sys, "cpuinfo"), "cpus");
  
  vm_deallocate(mach_task_self(),
                (vm_address_t)pinfo,
                (vm_size_t)sizeof(*pinfo) * msg_count);
  
  cpu_monitor_do();
  mem_monitor_do();
  callout_arm(&timer, timercb, NULL, 1);
}

INITME(INIT_GROUP_API, darwin_init_cpu_monitor, NULL, 0);
