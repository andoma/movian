/*
 *  Arch specifics for Nintendo Wii
 *
 *  Copyright (C) 2008 Andreas Öman
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

#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <di/di.h>
#include <fat.h>
#include <network.h>
#include <errno.h>
#include <sdcard/wiisd_io.h>

#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>

#include "threads.h"

#include "showtime.h"
#include "arch.h"

#include <stdarg.h>
#include "prop/prop.h"

#include "service.h"
#include "misc/callout.h"
#include "notifications.h"

extern int concurrency;

void *wii_xfb[2];
GXRModeObj wii_vmode;

extern void net_setup(void);
extern char *remote_logtarget;

int rlog_socket = -1;

static hts_mutex_t log_mutex;

int wii_sd_mounted;

static callout_t memlogger;

static void 
memlogger_fn(callout_t *co, void *aux)
{
  callout_arm(&memlogger, memlogger_fn, NULL, 1);

  TRACE(TRACE_DEBUG, "Wii", "Avail mem: Arena1 = %d, Arena2: %d",
	SYS_GetArena1Size(), SYS_GetArena2Size());
}


static void 
panic(const char *str)
{
  TRACE(TRACE_ERROR, "Wii", str);
  sleep(1);
  abort();
}

void
hts_mutex_init(hts_mutex_t *m)
{
  if(LWP_MutexInit(m, 0)) {
    panic("Unable to create mutex");
  }
}


void
hts_cond_init(hts_cond_t *c)
{
  if(LWP_CondInit(c)) {
    panic("Unable to create condvar");
  }
}


void
hts_thread_create_detached(const char *name, void *(*f)(void *), void *arg)
{
  lwp_t threadid;

  if(LWP_CreateThread(&threadid, (void *)f, arg, NULL, 32768, 80)) {
    panic("Unable to create thread");
  } else {
    TRACE(TRACE_DEBUG, "Wii", "Created thread %s: 0x%08x", name, threadid);

  }
}

void hts_thread_create_joinable(const char *name, hts_thread_t *p, 
				void *(*f)(void *), void *arg)
{
  if(LWP_CreateThread(p, (void *)f, arg, NULL, 32768, 80)) {
    panic("Unable to create thread");
  } else {
    TRACE(TRACE_DEBUG, "Wii", "Created thread %s: 0x%08x", name, *p);
  }
}


/**
 *
 */
int
hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delta)
{
  struct timespec ts;
  ts.tv_sec  =  delta / 1000;
  ts.tv_nsec = (delta % 1000) * 1000000;
  return LWP_CondTimedWait(*c, *m, &ts) == ETIMEDOUT;
}

/**
 *
 */
void
arch_init(void)
{
  // Must be very early as it does weird tricks with the system
  DI_Init();

  hts_mutex_init(&log_mutex);

  concurrency = 1;

  // Initialise the video system
  VIDEO_Init();

  // Initialise the audio system
  AUDIO_Init(NULL);
  
  // This function initialises the attached controllers
  WPAD_Init();

  // Obtain the preferred video mode from the system
  // This will correspond to the settings in the Wii menu
  VIDEO_GetPreferredMode(&wii_vmode);
  // Overscan slightly
  wii_vmode.viWidth = 678;
  wii_vmode.viXOrigin = (720 - 678) / 2;

  // Allocate memory for the display in the uncached region
  wii_xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(&wii_vmode));
  wii_xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(&wii_vmode));
	
  // Initialise the console, required for printf
  console_init(wii_xfb[0], 20, 20,
	       wii_vmode.fbWidth, wii_vmode.xfbHeight,
	       wii_vmode.fbWidth*VI_DISPLAY_PIX_SZ);
	
  // Set up the video registers with the chosen mode
  VIDEO_Configure(&wii_vmode);
	
  // Tell the video hardware where our display memory is
  VIDEO_SetNextFramebuffer(wii_xfb[0]);
	
  // Make the display visible
  VIDEO_SetBlack(FALSE);

  // Flush the video register changes to the hardware
  VIDEO_Flush();

  // Wait for Video setup to complete
  VIDEO_WaitVSync();
  if(wii_vmode.viTVMode & VI_NON_INTERLACE)
    VIDEO_WaitVSync();

  // The console understands VT terminal escape codes
  // This positions the cursor on row 2, column 0
  // we can use variables for this with format codes too
  // e.g. printf ("\x1b[%d;%dH", row, column );
  printf("\x1b[2;0H");

  printf("Showtime %s, starting...\n", htsversion);

  VIDEO_WaitVSync();

  net_setup();

  wii_sd_mounted = fatMountSimple("sd", &__io_wiisd);

  if(remote_logtarget != NULL) {

    int net_try_setup(void);

    net_try_setup();

    if((rlog_socket = net_socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {

      struct sockaddr_in sin;
      int r;
      memset(&sin, 0, sizeof(sin));
      sin.sin_family = AF_INET;
      sin.sin_len = sizeof(sin);
      sin.sin_port = htons(514);
      r = inet_aton(remote_logtarget, &sin.sin_addr);

      if((r = net_connect(rlog_socket, 
			  (struct sockaddr *)&sin, sizeof(sin))) < 0) {
	rlog_socket = -1;
	printf("Failed to set remote logging destination, error = %d\n", r);
	sleep(2);
      }

    } else {
      printf("Failed to create remote logging socket, error = %d\n", 
	     rlog_socket);
      sleep(2);
    }
  }

  TRACE(TRACE_INFO, "Wii", "Wii arch specific code initialized");

  callout_arm(&memlogger, memlogger_fn, NULL, 1);
}


static const char *months[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};


/**
 *
 */
static void
rlog(const char *buf, const char *subsys, int level)
{
  static char packet[1024];
  time_t now;
  struct tm *tm;
  int syslog_level;

  time(&now);
  tm = localtime(&now);

#define SYSLOG_PRIO 23 // local7


  switch(level) {
  case TRACE_ERROR: syslog_level = 3; break;
  case TRACE_INFO:  syslog_level = 6; break;
  case TRACE_DEBUG: syslog_level = 7; break;
  default:
    syslog_level = 7;
    break;
  }

  snprintf(packet, sizeof(packet), "<%d>%s %2d %02d:%02d:%02d %s [%s] %s", 
	   SYSLOG_PRIO * 8 + syslog_level,
	   months[tm->tm_mon], tm->tm_mday, tm->tm_hour, 
	   tm->tm_min, tm->tm_sec,
	   "showtimeWii",
	   subsys, buf);

  net_send(rlog_socket, packet, strlen(packet), 0);
}



extern int trace_level;

/**
 *
 */
void
trace_arch(int level, const char *prefix, const char *buf)
{
  if(rlog_socket >= 0)
    rlog(buf, prefix, level);
}

/**
 *
 */
void
arch_exit(int retcode)
{
  if(wii_sd_mounted) {
    TRACE(TRACE_DEBUG, "Wii", "Unmounting front SD card");
    fatUnmount("sd");
  }
  exit(retcode);
}


/**
 *
 */
void
arch_sd_init(void)
{
  if(wii_sd_mounted)
    service_create("Front SD card", "file://sd:/",
		   SVC_TYPE_OTHER, NULL, 0);
}
/**
 *
 */
int64_t
showtime_get_ts(void)
{
  extern long long gettime();
  long long t = gettime();
  return (t * 1000LL) / 60750LL;
}


/**
 *
 */
void
arch_set_cachepath(void)
{
  char buf[PATH_MAX];

  snprintf(buf, sizeof(buf), "sd:/showtimecache");
  showtime_cache_path = strdup(buf);
}
