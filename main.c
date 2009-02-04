/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2009 Andreas Öman
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


#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libavformat/avformat.h>

#include "showtime.h"
#include "event.h"
#include "prop.h"
#include "arch/arch.h"

#include "audio/audio.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_imageloader.h"
#include "fileaccess/fa_rawloader.h"
#include "navigator.h"
#include "settings.h"
#include "ui/ui.h"
#include "ui/keymapper.h"
#include "playqueue.h"
#include "keyring.h"

hts_mutex_t ffmutex;
int concurrency;
const char *default_theme_path = SHOWTIME_DEFAULT_THEME_PATH;

/**
 * Showtime main
 */
int
main(int argc, char **argv)
{
  struct timeval tv;
  const char *settingspath = NULL;
  int returncode;

  gettimeofday(&tv, NULL);
  srand(tv.tv_usec);

  /* We read options ourselfs getopt() is broken on some (nintento wii)
     targets */

  argv++;
  argc--;

  while(argc > 0) {
    if(!strcmp(argv[0], "-s") && argc > 1) {
      settingspath = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else if(!strcmp(argv[0], "-t") && argc > 1) {
      default_theme_path = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else
      break;
  }

  /* Initialize property tree */
  prop_init();

  /* Architecture specific init */
  arch_init();

  /* Initialize (and optionally load) settings */
  hts_settings_init("showtime", settingspath);

  /* Initialize keyring */
  keyring_init();

  /* Initialize settings */
  settings_init();

  /* Initialize event dispatcher */
  event_init();

  /* Initialize libavcodec & libavformat */
  hts_mutex_init(&ffmutex);
  av_log_set_level(AV_LOG_QUIET);
  av_register_all();

  /* Initialize fileaccess subsystem */
  fileaccess_init();

  /* Initialize playqueue */
  playqueue_init();

  /* Initialize navigator */
  nav_init();

  /* Initialize audio subsystem */
  audio_init();

  /* Initialize user interfaces */
  ui_init();

  /* Load initial URL */
  if(argc > 0)
    nav_open(argv[0]);
  else
    nav_open("page://mainmenu");

  /* Goto UI control dispatcher */
  returncode = ui_main_loop();

  return returncode;
}
