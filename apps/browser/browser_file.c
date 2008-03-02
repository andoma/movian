/*
 *  File browser
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

#include <pthread.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include "showtime.h"
#include "browser.h"
#include "browser_file.h"
#include "browser_probe.h"


/**
 * browser_file_scan_thread
 */
static void *
browser_file_scan_thread(void *arg)
{
  browser_node_t *bn = arg, *c;

  char buf[1000];
  struct stat st;
  struct dirent *d;
  DIR *dir;
  int type;

  if((dir = opendir(bn->bn_url)) != NULL) {
    while((d = readdir(dir)) != NULL) {

      if(d->d_name[0] == '.')
	continue; /* skip dot files */
      
      snprintf(buf, sizeof(buf), "%s/%s", bn->bn_url, d->d_name);

      if(stat(buf, &st))
	continue;

      type = d->d_type;
      if(d->d_type == DT_LNK) {
	/* Resolve what the link point to */
	switch(st.st_mode & S_IFMT) {
	case S_IFDIR:
	  type = DT_DIR;
	  break;
	case S_IFREG:
	  type = DT_REG;
	  break;
	default:
	  continue;
	}
      }

      switch(type) {
      case DT_DIR:
	type = BN_DIR;
	break;
    
      case DT_REG:
	type = BN_FILE;
	break;

      default:
	continue;
      }

      c = browser_node_add_child(bn, buf, type);

      if(type == BN_FILE)
	browser_probe_enqueue(c);

      browser_node_deref(c);
    }
    closedir(dir);
  }
  browser_node_deref(bn);
  return NULL;
}





/**
 * Start scanning of a node (which more or less has to be a directory 
 * for this to work, but we expect the caller to know about that).
 *
 * We spawn a new thread to make this quick and fast
 */
static void
browser_file_scan(browser_node_t *bn)
{
  pthread_t ptid;
  pthread_attr_t attr;
  browser_node_ref(bn);

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  pthread_create(&ptid, &attr, browser_file_scan_thread, bn);
}









browser_protocol_t browser_file_protocol = {
  .bp_scan = browser_file_scan,
};

