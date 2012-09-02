/*
 *  Text rendering
 *  Copyright (C) 2007 - 2012 Andreas Öman
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "showtime.h"
#include "prop/prop.h"
#include "text.h"
#include "notifications.h"
#include "fileaccess/fileaccess.h"

LIST_HEAD(font_list, font);

typedef struct font {
  LIST_ENTRY(font) f_link;
  char *f_title;

  prop_t *f_status;
  prop_t *f_prop_installed;

  prop_t *f_prop_uifont;
  prop_t *f_prop_subfont;

  rstr_t *f_installed_path;

} font_t;

static htsmsg_t *store;
static hts_mutex_t font_mutex;
static struct font_list fonts;

/**
 *
 */
static font_t *
font_find(const char *title)
{
  font_t *f;
  LIST_FOREACH(f, &fonts, f_link)
    if(!strcasecmp(title, f->f_title))
      return f;

  f = calloc(1, sizeof(font_t));
  
  f->f_title = strdup(title);
  f->f_status = prop_create_root(NULL);
  f->f_prop_installed = prop_create(f->f_status, "installed");
  f->f_prop_uifont    = prop_create(f->f_status, "uifont");
  f->f_prop_subfont   = prop_create(f->f_status, "subfont");
  LIST_INSERT_HEAD(&fonts, f, f_link);
  return f;
}


/**
 *
 */
static void
clear_font_prop(int which)
{
  font_t *f;
  LIST_FOREACH(f, &fonts, f_link)
    prop_set_int(which ? f->f_prop_subfont : f->f_prop_uifont, 0);

}


/**
 *
 */
static void
font_install(font_t *f, const char *url)
{
  char errbuf[256];
  char path[512];
  if(f->f_installed_path != NULL)
    return;

  size_t size;
  char *buf = fa_load(url, &size, NULL,
		      errbuf, sizeof(errbuf), NULL, 0, NULL, NULL);
  if(buf == NULL) {
    notify_add(NULL, NOTIFY_ERROR, NULL, 5, _("Unable to load %s -- %s"),
	       url, errbuf);
    return;
  }

  snprintf(path, sizeof(path), "%s/installedfonts", gconf.persistent_path);
  mkdir(path, 0770);

  snprintf(path, sizeof(path), "%s/installedfonts/%s",
	   gconf.persistent_path, f->f_title);
  unlink(path);
  
  int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0660);
  if(fd == -1) {

    notify_add(NULL, NOTIFY_ERROR, NULL, 5,
	       _("Unable to open %s for writing"), path);
    free(buf);
    return;
  }
  
  size_t r = write(fd, buf, size);
  free(buf);
  if(close(fd) || r != size) {
    notify_add(NULL, NOTIFY_ERROR, NULL, 5,
	       _("Unable to write to %s"), path);
    return;
  }

  f->f_installed_path = rstr_alloc(path);
  prop_set_int(f->f_prop_installed, 1);
  TRACE(TRACE_DEBUG, "fontstash", "Wrote %s to %s", f->f_title, path);
}


/**
 *
 */
static void
use_font(font_t *f, const char *url)
{
  char tmp[256];
  const char *msgs[3];
  rstr_t *fmt = _("Use font %s for");
  rstr_t *ui = _("User interface");
  rstr_t *subs = _("Subtitles");

  snprintf(tmp, sizeof(tmp), rstr_get(fmt), f->f_title);
  
  msgs[0] = rstr_get(ui);
  msgs[1] = rstr_get(subs);
  msgs[2] = NULL;

  int r = message_popup(tmp, MESSAGE_POPUP_CANCEL, msgs);
  rstr_release(fmt);
  rstr_release(ui);
  rstr_release(subs);

  if(r == MESSAGE_POPUP_CANCEL)
    return;

  font_install(f, url);

  if(r == 1) {
    clear_font_prop(0);
    htsmsg_delete_field(store, "uifont");
    htsmsg_add_str(store, "uifont", f->f_title);
    freetype_set_default_ui_font(rstr_get(f->f_installed_path));
    prop_set_int(f->f_prop_uifont, 1);
  }
  htsmsg_store_save(store, "fontstash");
}


/**
 *
 */
static void
font_event(void *opaque, prop_event_t event, ...)
{
  font_t *f = opaque;
  va_list ap;
  prop_sub_t *s;
  event_t *e;
  prop_t *p;

  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    s = va_arg(ap, prop_sub_t *);
    prop_unsubscribe(s);
    break;

  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    p = va_arg(ap, prop_t *);

    if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
      if(!strcmp(e->e_payload, "use")) {
	rstr_t *package = prop_get_string(p, "url", NULL);
	use_font(f, rstr_get(package));
	rstr_release(package);
      }
    }
    break;
  }
}


/**
 *
 */
void
fontstash_props_from_title(struct prop *prop, const char *url,
			   const char *title)
{
  font_t *f = font_find(title);

  prop_link(f->f_status, prop_create(prop, "status"));

  prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_SINGLETON,
		 PROP_TAG_CALLBACK, font_event, f,
		 PROP_TAG_ROOT, prop,
		 PROP_TAG_MUTEX, &font_mutex,
		 NULL);
}


/**
 *
 */
void
fontstash_init(void)
{
  if((store = htsmsg_store_load("fontstash")) == NULL)
    store = htsmsg_create_map();

  hts_mutex_init(&font_mutex);

  char path[512];

  snprintf(path, sizeof(path), "file://%s/installedfonts",
	   gconf.persistent_path);

  fa_dir_t *fd = fa_scandir(path, NULL, 0);

  if(fd == NULL)
    return;

  const char *uifont  = htsmsg_get_str(store, "uifont");
  const char *subfont = htsmsg_get_str(store, "subfont");

  fa_dir_entry_t *fde;
  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    font_t *f = font_find(rstr_get(fde->fde_filename));
    f->f_installed_path = rstr_dup(fde->fde_url);
    prop_set_int(f->f_prop_installed, 1);

    if(uifont && !strcmp(f->f_title, uifont)) {
      freetype_set_default_ui_font(rstr_get(f->f_installed_path));
      prop_set_int(f->f_prop_uifont, 1);
    }

    if(subfont && !strcmp(f->f_title, subfont)) {
      freetype_set_default_ui_font(rstr_get(f->f_installed_path));
      prop_set_int(f->f_prop_subfont, 1);
    }
  }
  fa_dir_free(fd);
}
