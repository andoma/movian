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
#include <fat.h>

#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "showtime.h"
#include "arch.h"

#include <stdarg.h>
#include "prop.h"

extern char *htsversion;
extern int concurrency;

void *wii_xfb[2];
GXRModeObj *wii_rmode;

extern void net_setup(void);


/**
 *
 */
void
arch_init(void)
{
  concurrency = 1;

  // Initialise the video system
  VIDEO_Init();

  // Initialise the audio system
  AUDIO_Init(NULL);
  
  // This function initialises the attached controllers
  WPAD_Init();

  // Obtain the preferred video mode from the system
  // This will correspond to the settings in the Wii menu
  wii_rmode = VIDEO_GetPreferredMode(NULL);

  // Allocate memory for the display in the uncached region
  wii_xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(wii_rmode));
  wii_xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(wii_rmode));
	
  // Initialise the console, required for printf
  console_init(wii_xfb[0], 20, 20,
	       wii_rmode->fbWidth,wii_rmode->xfbHeight,
	       wii_rmode->fbWidth*VI_DISPLAY_PIX_SZ);
	
  // Set up the video registers with the chosen mode
  VIDEO_Configure(wii_rmode);
	
  // Tell the video hardware where our display memory is
  VIDEO_SetNextFramebuffer(wii_xfb[0]);
	
  // Make the display visible
  VIDEO_SetBlack(FALSE);

  // Flush the video register changes to the hardware
  VIDEO_Flush();

  // Wait for Video setup to complete
  VIDEO_WaitVSync();
  if(wii_rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

  // The console understands VT terminal escape codes
  // This positions the cursor on row 2, column 0
  // we can use variables for this with format codes too
  // e.g. printf ("\x1b[%d;%dH", row, column );
  printf("\x1b[2;0H");

  printf("Showtime %s, starting...\n", htsversion);
  printf("screen size = %d x %d\n", wii_rmode->viWidth, wii_rmode->viHeight);
  VIDEO_WaitVSync();

  if (!fatInitDefault())
    printf("fatInitDefault failure\n");

  printf("Initializing network\n");
  net_setup();}
