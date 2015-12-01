#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <malloc/malloc.h>
#include <sys/time.h>
#include <mach/mach_time.h>

#include "arch/arch.h"
#include "main.h"

static mach_timebase_info_data_t timebase;

/**
 *
 */
int
get_system_concurrency(void)
{
  int mib[2];
  int ncpu;
  size_t len;
  
  mib[0] = CTL_HW;
  mib[1] = HW_NCPU;
  len = sizeof(ncpu);
  sysctl(mib, 2, &ncpu, &len, NULL, 0);
  
  return ncpu;
}


/**
 *
 */
size_t
arch_malloc_size(void *ptr)
{
    return malloc_size(ptr);
}

/**
 *
 */
const char *
arch_get_system_type(void)
{
    return "iOS";
}


/**
 *
 */
void
arch_exit(void)
{
    exit(0);
}


/**
 *
 */
int64_t
arch_get_avtime(void)
{
  int64_t now = mach_absolute_time();
  return now * timebase.numer / (timebase.denom * 1000);
}

INITIALIZER(get_the_timebase) {
  mach_timebase_info(&timebase);
}

/**
 *
 */
int
arch_stop_req(void)
{
    return 0;
}


size_t
fwrite$UNIX2003(const void *restrict ptr, size_t size, size_t nitems, FILE *restrict stream)
{
    return fwrite(ptr, size, nitems, stream);
}
