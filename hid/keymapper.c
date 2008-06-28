/*
 *  Key mapper
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

#include <assert.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <libhts/hts_strtab.h>
#include "showtime.h"
#include "input.h"
#include "keymapper.h"

#define KEYDESC_HASH_SIZE 101

static unsigned int
keydesc_hashstr(const char *s)
{
  unsigned int hash = 0;
  unsigned char c;
  while(*s) {
    c = *s++;
    hash += (c << 5) + c;
  }
  return hash % KEYDESC_HASH_SIZE;
}

static struct hid_keycode_list keycodes;
static struct hid_keydesc_list keydescs[KEYDESC_HASH_SIZE];

/**
 * Resolve a keydesc into a keycode
 */
void
keymapper_resolve(inputevent_t *ie)
{
  unsigned int hash = keydesc_hashstr(ie->u.keydesc);
  hid_keydesc_t *hkd;
  hid_keycode_t *hkc;

  input_root_event(ie);

  LIST_FOREACH(hkd, &keydescs[hash], hkd_hash_link)
    if(!strcmp(hkd->hkd_desc, ie->u.keydesc))
      break;

  if(hkd == NULL)
    return;

  hkc = hkd->hkd_hkc;
  input_key_down(hkc->hkc_code);
}


/**
 *
 */
hid_keycode_t *
keymapper_find_by_code(input_key_t val)
{
  hid_keycode_t *hkc;

  LIST_FOREACH(hkc, &keycodes, hkc_link) 
    if(hkc->hkc_code == val)
      break;

  if(hkc == NULL) {
    hkc = malloc(sizeof(hid_keycode_t));
    hkc->hkc_code = val;
    LIST_INIT(&hkc->hkc_descs);
    LIST_INSERT_HEAD(&keycodes, hkc, hkc_link);
  }
  return hkc;
}


/**
 *
 */
hid_keydesc_t *
keymapper_find_by_desc(const char *str)
{
  unsigned int hash = keydesc_hashstr(str);
  hid_keydesc_t *hkd;

  LIST_FOREACH(hkd, &keydescs[hash], hkd_hash_link)
    if(!strcmp(hkd->hkd_desc, str))
      break;

  if(hkd == NULL) {
    hkd = malloc(sizeof(hid_keydesc_t));
    hkd->hkd_desc = strdup(str);
    hkd->hkd_hkc = NULL;
    LIST_INSERT_HEAD(&keydescs[hash], hkd, hkd_hash_link);
  }
  return hkd;
}


/**
 *
 */
void
keymapper_map(hid_keydesc_t *hkd, hid_keycode_t *hkc)
{
  if(hkd->hkd_hkc != NULL)
    LIST_REMOVE(hkd, hkd_keycode_link);

  hkd->hkd_hkc = hkc;
  LIST_INSERT_HEAD(&hkc->hkc_descs, hkd, hkd_keycode_link);
}

/**
 *
 */
void
keymapper_save(void)
{
  hid_keycode_t *hkc;
  hid_keydesc_t *hkd;
  char buf[256];
  FILE *fp;
  const char *codename;

  snprintf(buf, sizeof(buf), "%s/keymap", settingsdir);

  if((fp = fopen(buf, "w+")) == NULL)
    return;

  LIST_FOREACH(hkc, &keycodes, hkc_link) {
    if((codename = keycode2str(hkc->hkc_code)) == NULL)
      continue;

    LIST_FOREACH(hkd, &hkc->hkc_descs, hkd_keycode_link) 
      fprintf(fp, "%s = %s\n", codename, hkd->hkd_desc);
  }
  fclose(fp);
}


/**
 *
 */
void
keymapper_load(void)
{
  hid_keycode_t *hkc;
  hid_keydesc_t *hkd;
  char buf[256];
  char buf2[256];
  FILE *fp;
  input_key_t val;

  snprintf(buf, sizeof(buf), "%s/keymap", settingsdir);

  if((fp = fopen(buf, "r")) == NULL)
    return;

  while(fscanf(fp, "%s = %[^\n]", buf, buf2) == 2) {
    fprintf(stderr, "%s -> %s\n", buf, buf2);

    if((val = keystr2code(buf)) == -1)
      continue;

    hkc = keymapper_find_by_code(val);
    hkd = keymapper_find_by_desc(buf2);
    keymapper_map(hkd, hkc);

  }
  fclose(fp);
}
