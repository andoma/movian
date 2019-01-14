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

#include "main.h"
#include "prop/prop.h"
#include "prop/prop_nodefilter.h"
#include "prop/prop_concat.h"
#include "text.h"
#include "notifications.h"
#include "fileaccess/fileaccess.h"
#include "backend/backend.h"
#include "settings.h"
#include "htsmsg/htsmsg_store.h"

LIST_HEAD(font_list, font);

typedef struct font {
  LIST_ENTRY(font) f_link;
  char *f_title;

  prop_t *f_status;
  prop_t *f_prop_installed;

  prop_t *f_prop_mainfont;
  prop_t *f_prop_condfont;
  prop_t *f_prop_subfont;

  rstr_t *f_installed_path;

  prop_t *f_prop_model;

} font_t;

static htsmsg_t *store;
static hts_mutex_t font_mutex;
static struct font_list fonts;
static prop_t *font_prop_main, *font_prop_cond;
static prop_t *font_prop_subs;
static prop_t *fontstash_installed_root;
static prop_t *fontstash_browse_nodes;

char font_subs[256];

static void font_make_installed(font_t *f);

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
  f->f_prop_mainfont  = prop_create(f->f_status, "mainfont");
  f->f_prop_condfont  = prop_create(f->f_status, "condfont");
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
  LIST_FOREACH(f, &fonts, f_link) {
    switch(which) {
    case 0:
      prop_set_int(f->f_prop_mainfont, 0);
      break;
    case 1:
      prop_set_int(f->f_prop_condfont, 0);
      break;
    case 2:
      prop_set_int(f->f_prop_subfont, 0);
      break;
    }
  }
}


/**
 *
 */
static void
font_install(font_t *f, const char *url)
{
  char errbuf[256];
  char path[URL_MAX];
  if(f->f_installed_path != NULL)
    return;

  snprintf(path, sizeof(path),
           "file://%s/installedfonts/%s", gconf.persistent_path, f->f_title);

  if(fa_copy(path, url, errbuf, sizeof(errbuf))) {
    notify_add(NULL, NOTIFY_ERROR, NULL, 5,
	       _("Unable to install font: %s"), errbuf);
    return;
  }

  f->f_installed_path = rstr_alloc(path);
  prop_set_int(f->f_prop_installed, 1);
  TRACE(TRACE_DEBUG, "fontstash", "Wrote %s to %s", f->f_title, path);
  font_make_installed(f);
}


/**
 *
 */
static void
use_font(font_t *f, const char *url)
{
  char tmp[256];
  const char *msgs[4];
  rstr_t *fmt = _("Use font %s for");
  rstr_t *ui = _("User interface");
  rstr_t *cond = _("Narrow text");
  rstr_t *subs = _("Subtitles");

  snprintf(tmp, sizeof(tmp), rstr_get(fmt), f->f_title);
  
  msgs[0] = rstr_get(ui);
  msgs[1] = rstr_get(cond);
  msgs[2] = rstr_get(subs);
  msgs[3] = NULL;

  int r = message_popup(tmp, MESSAGE_POPUP_CANCEL, msgs);
  rstr_release(fmt);
  rstr_release(ui);
  rstr_release(cond);
  rstr_release(subs);

  if(r == MESSAGE_POPUP_CANCEL)
    return;

  font_install(f, url);
  
  switch(r) {
  case 1:
    clear_font_prop(0);
    htsmsg_delete_field(store, "mainfont");
    htsmsg_add_str(store, "mainfont", f->f_title);
    prop_set_rstring(font_prop_main, f->f_installed_path);
    prop_set_int(f->f_prop_mainfont, 1);
    break;
  case 2:
    clear_font_prop(1);
    htsmsg_delete_field(store, "condfont");
    htsmsg_add_str(store, "condfont", f->f_title);
    prop_set_rstring(font_prop_cond, f->f_installed_path);
    prop_set_int(f->f_prop_condfont, 1);
    break;
  case 3:
    clear_font_prop(2);
    htsmsg_delete_field(store, "subfont");
    htsmsg_add_str(store, "subfont", f->f_title);
    prop_set_rstring(font_prop_subs, f->f_installed_path);
    snprintf(font_subs, sizeof(font_subs), "%s", rstr_get(f->f_installed_path));
    prop_set_int(f->f_prop_subfont, 1);
    break;
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

    if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
      const event_payload_t *ep = (const event_payload_t *)e;
      const char *install = mystrbegins(ep->payload, "use:");
      if(install != NULL)
	use_font(f, install);
    }
    break;
  }
}



/**
 *
 */
static void
font_make_installed(font_t *f)
{
  prop_t *p = f->f_prop_model = prop_create_root(NULL);
  
  prop_set(p, "type", PROP_SET_STRING, "font");
  prop_setv(p, "metadata", "title", NULL, PROP_SET_STRING, f->f_title);
  prop_set(p, "url", PROP_SET_RSTRING, f->f_installed_path);
  prop_link(f->f_status, prop_create(p, "status"));
  if(prop_set_parent(p, fontstash_installed_root))
    abort();

  prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_SINGLETON,
		 PROP_TAG_CALLBACK, font_event, f,
		 PROP_TAG_ROOT, p,
		 PROP_TAG_MUTEX, &font_mutex,
		 NULL);
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



const char *fontclasses[3] = { "mainfont", "condfont", "subfont"};

/**
 *
 */
static void
reset_font(int id)
{
  hts_mutex_lock(&font_mutex);
  clear_font_prop(id);
  htsmsg_delete_field(store, fontclasses[id]);
  switch(id) {
  case 0:
    prop_set_void(font_prop_main);
    break;
  case 1:
    prop_set_void(font_prop_cond);
    break;
  case 2:
    prop_set_void(font_prop_subs);
    font_subs[0] = 0;
    break;
  }
  htsmsg_store_save(store, "fontstash");
  hts_mutex_unlock(&font_mutex);
}


/**
 *
 */
static void
reset_main(void *opaque, prop_event_t event, ...)
{
  reset_font(0);
}
static void
reset_cond(void *opaque, prop_event_t event, ...)
{
  reset_font(1);
}
static void
reset_subs(void *opaque, prop_event_t event, ...)
{
  reset_font(2);
}

/**
 *
 */
static void
fontstash_init(void)
{
  prop_concat_t *pc;

  prop_t *fonts = prop_create(prop_get_global(), "fonts");

  font_prop_main = prop_create(fonts, "main");
  font_prop_cond = prop_create(fonts, "condensed");
  font_prop_subs = prop_create(fonts, "subs");
  fontstash_installed_root = prop_create(fonts, "installed");

  fontstash_browse_nodes = prop_create_root(NULL);

  pc = prop_concat_create(fontstash_browse_nodes);

  prop_t *top = prop_create_root(NULL);
  settings_create_action(top, _p("Reset main font to default"), NULL,
			 reset_main, NULL, SETTINGS_RAW_NODES, NULL);
  settings_create_action(top, _p("Reset narrow font to default"), NULL,
			 reset_cond, NULL, SETTINGS_RAW_NODES, NULL);
  settings_create_action(top, _p("Reset subtitle font to default"), NULL,
			 reset_subs, NULL, SETTINGS_RAW_NODES, NULL);

  prop_t *x = prop_create_root(NULL);
  struct prop_nf *pn = prop_nf_create(x, fontstash_installed_root,
				      NULL, PROP_NF_AUTODESTROY);
  prop_nf_sort(pn, "node.metadata.title", 0, 0, NULL, 1);
  prop_nf_release(pn);

  prop_concat_add_source(pc, x, NULL);

  prop_concat_add_source(pc, top, makesep(_p("Defaults")));

  if((store = htsmsg_store_load("fontstash")) == NULL)
    store = htsmsg_create_map();

  hts_mutex_init(&font_mutex);


  prop_t *p = prop_create_root(NULL);

  prop_concat_add_source(gconf.settings_look_and_feel,
			 prop_create(p, "nodes"), NULL);

  settings_add_url(p, _p("Fonts"), NULL, NULL, NULL, "fontstash:", 0);

  char path[512];

  snprintf(path, sizeof(path), "file://%s/installedfonts",
	   gconf.persistent_path);

  fa_dir_t *fd = fa_scandir(path, NULL, 0);

  if(fd == NULL)
    return;

  const char *mainfont = htsmsg_get_str(store, "mainfont");
  const char *condfont = htsmsg_get_str(store, "condfont");
  const char *subfont  = htsmsg_get_str(store, "subfont");

  fa_dir_entry_t *fde;
  RB_FOREACH(fde, &fd->fd_entries, fde_link) {
    font_t *f = font_find(rstr_get(fde->fde_filename));
    f->f_installed_path = rstr_dup(fde->fde_url);
    prop_set_int(f->f_prop_installed, 1);

    if(mainfont && !strcmp(f->f_title, mainfont)) {
      prop_set_rstring(font_prop_main, f->f_installed_path);
      prop_set_int(f->f_prop_mainfont, 1);
    }

    if(condfont && !strcmp(f->f_title, condfont)) {
      prop_set_rstring(font_prop_cond, f->f_installed_path);
      prop_set_int(f->f_prop_condfont, 1);
    }

    if(subfont && !strcmp(f->f_title, subfont)) {
      prop_set_rstring(font_prop_subs, f->f_installed_path);
      snprintf(font_subs, sizeof(font_subs), "%s",
	       rstr_get(f->f_installed_path));
      prop_set_int(f->f_prop_subfont, 1);
    }
    font_make_installed(f);
  }
  fa_dir_free(fd);
}

INITME(INIT_GROUP_GRAPHICS, fontstash_init, NULL, 0);


/**
 *
 */
static int
fontstash_canhandle(const char *url)
{
  return !strncmp(url, "fontstash:", strlen("fontstash:"));
}



/**
 *
 */
static int
fontstash_open_url(prop_t *page, const char *url, int sync)
{
  prop_t *m = prop_create(page, "model");
  prop_t *md = prop_create(m, "metadata");
  prop_set(m, "type", PROP_SET_STRING, "directory");

  prop_link(_p("Installed fonts"), prop_create(md, "title"));
  prop_link(fontstash_browse_nodes, prop_create(m, "nodes"));
  return 0;
}


/**
 *
 */
static backend_t be_fontstash = {
  .be_canhandle = fontstash_canhandle,
  .be_open      = fontstash_open_url,
};

BE_REGISTER(fontstash);
