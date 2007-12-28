/*
 *  Browser interface
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

#ifndef BROWSER_H
#define BROWSER_H

#include "mediaprobe.h"
#include "hid/input.h"

TAILQ_HEAD(browser_entry_queue, browser_entry);

typedef struct browser_message {
  enum {
    BM_ADD,
    BM_UPDATE,
    BM_DELETE,
    BM_FOLDERICON,
    BM_SCAN_COMPLETE,
  } bm_action;

  int bm_entry_id;
  int bm_parent_id;

  const char *bm_url;

  mediainfo_t bm_mi;

} browser_message_t;

void browser_message_destroy(browser_message_t *bm);


typedef struct browser_interface {
  /* Frontend to Backend methods */
  
  void *(*bi_open)(struct browser_interface *bi, const char *path,
		   int id, int doprobe);

  void (*bi_close)(void *dir);

  void (*bi_probe)(void *dir, int run);

  void (*bi_destroy)(struct browser_interface *bi);

  /* Inpute event mailbox where backend should drop notifications */

  ic_t *bi_mailbox;

} browser_interface_t;

void browser_message_enqueue(browser_interface_t *bi, int action, int id,
			     int parent_id, const char *url,
			     mediainfo_t *mi);

void browser_scan_dir(browser_interface_t *bi, const char *path);

#endif /* BROWSER_H */
