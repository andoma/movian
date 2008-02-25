/*
 *  Generic browser
 *  Copyright (C) 2007 Andreas Öman
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
#include <pthread.h>

#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "browser.h"


void
browser_message_destroy(browser_message_t *bm)
{
  free((void *)bm->bm_url);
  mediaprobe_free(&bm->bm_mi);
  free(bm);
}

static void
browser_message_destroy2(void *aux)
{
  abort();
  browser_message_destroy(aux);
}


void
browser_message_enqueue(browser_interface_t *bi, int action, int id,
			int parent_id, const char *url,
			mediainfo_t *mi)
{
  browser_message_t *bm = calloc(1, sizeof(browser_message_t));
  inputevent_t ie;

  bm->bm_action = action;
  bm->bm_entry_id = id;
  bm->bm_parent_id = parent_id;
  bm->bm_url = strdup(url);
  if(mi != NULL)
    mediaprobe_dup(&bm->bm_mi, mi);
  
  ie.type = INPUT_SPECIAL;
  ie.u.ptr = bm;
  ie.freefunc = browser_message_destroy2;
  input_postevent(bi->bi_mailbox, &ie);
}
