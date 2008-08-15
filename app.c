/*
 *  Application handing
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

#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <assert.h>

#include <libhts/htssettings.h>

#include "showtime.h"
#include "app.h"
#include "event.h"
#include "layout/layout.h"
#include "apps/launcher/launcher.h"
#include "apps/settings/settings.h"
#include "apps/playlist/playlist.h"

static hts_mutex_t appi_list_mutex;

static hts_mutex_t app_index_mutex;
static int app_index = 0;

LIST_HEAD(, app)  apps;
LIST_HEAD(, appi) appis;

/**
 *
 */
static int
appi_speedbutton_switcher(glw_event_t *ge, void *opaque)
{
  event_keydesc_t *ek = (void *)ge;
  appi_t *ai;
  int r = 0;

  if(ge->ge_type != EVENT_KEYDESC)
    return 0;

  hts_mutex_lock(&appi_list_mutex);

  LIST_FOREACH(ai, &appis, ai_link) {
    if(!strcmp(ek->desc, ai->ai_speedbutton)) {
      layout_world_appi_show(ai);
      r = 1;
      break;
    }
  }
  hts_mutex_unlock(&appi_list_mutex);
  return r;
}



/**
 *
 */
static app_t *
app_find_by_name(const char *appname)
{
  app_t *a;
  LIST_FOREACH(a, &apps, app_link)
    if(!strcmp(appname, a->app_name))
      return a;
  
  return NULL;
}


/**
 *
 */
static void
appi_map_generic_config(appi_t *ai, htsmsg_t *settings)
{
  const char *v;

  if((v = htsmsg_get_str(settings, "speedbutton")) != NULL)
    snprintf(ai->ai_speedbutton, sizeof(ai->ai_speedbutton), "%s", v);
}

/**
 * 
 */
static appi_t *
appi_create0(const char *name)
{
  appi_t *ai = calloc(1, sizeof(appi_t));
  
  hts_mutex_lock(&appi_list_mutex);
  LIST_INSERT_HEAD(&appis, ai, ai_link);
  hts_mutex_unlock(&appi_list_mutex);
  
  glw_event_initqueue(&ai->ai_geq);
  ai->ai_mp = mp_create(name, ai); /* Includes a refcount, this is
				      decreated in appi_destroy() */

  ai->ai_prop_root = glw_prop_create(NULL, "app", GLW_GP_DIRECTORY);
  ai->ai_prop_title = glw_prop_create(ai->ai_prop_root, "title", GLW_GP_STRING);

  return ai;
}

/**
 * Spawn a new application instance
 */
void
app_spawn(app_t *a, htsmsg_t *settings, int index)
{
  appi_t *ai = appi_create0(a->app_name);

  ai->ai_app = a;
  ai->ai_instance_index = index;
  ai->ai_settings = settings;

  if(settings != NULL)
    appi_map_generic_config(ai, settings);

  a->app_spawn(ai);
}


/**
 * Create a new application instance
 */
appi_t *
appi_create(const char *name)
{
  appi_t *ai = appi_create0(name);
  ai->ai_name = name;
  return ai;
}

/**
 * Destroy an application
 */
void
appi_destroy(appi_t *ai)
{
  if(ai->ai_settings != NULL)
    htsmsg_destroy(ai->ai_settings);

  hts_settings_remove("applications/%d", ai->ai_instance_index);

  mp_unref(ai->ai_mp);

  free((void *)ai->ai_name);
  glw_event_flushqueue(&ai->ai_geq);
  glw_prop_destroy(ai->ai_prop_root);
  free(ai);
}


/**
 * Init a specific app
 */
static void
app_init(app_t *a)
{
  launcher_app_add(a);
}

/**
 * Open application settings file, truncates it if already exist.
 */
htsmsg_t *
appi_settings_create(appi_t *ai)
{
  htsmsg_t *m;

  if(ai->ai_instance_index == 0) {
    hts_mutex_lock(&app_index_mutex);
    ai->ai_instance_index = ++app_index;
    hts_mutex_unlock(&app_index_mutex);
  }

  m = htsmsg_create();

  htsmsg_add_str(m, "application", ai->ai_app->app_name);
  htsmsg_add_u32(m, "instanceid", ai->ai_instance_index);
  if(ai->ai_speedbutton[0])
    htsmsg_add_str(m, "speedbutton", ai->ai_speedbutton);
  return m;
}

/**
 * Finalize storage of settings
 */
void
appi_settings_save(appi_t *ai, htsmsg_t *m)
{
  hts_settings_save(m, "applications/%d", ai->ai_instance_index);
  htsmsg_destroy(m);
}


/**
 * Open application settings file, truncates it if already exist.
 */
static void
autolaunch_applications(void)
{
  htsmsg_t *m, *a;
  htsmsg_field_t *f;
  const char *appname;
  app_t *app;
  uint32_t instance;

  if((m = hts_settings_load("applications")) == NULL)
    return;

  HTSMSG_FOREACH(f, m) {
    if((a = htsmsg_get_msg_by_field(f)) == NULL)
      continue;
    
    if((appname = htsmsg_get_str(a, "application")) == NULL)
      continue;

    app = app_find_by_name(appname);
    if(app == NULL) {
      fprintf(stderr, "Unable to spawn app %s, application does not exist\n",
	      appname);
      continue;
    }
    
    if(htsmsg_get_u32(a, "instanceid", &instance))
      continue;

    if(instance > app_index)
      app_index = instance;

    a = htsmsg_detach_submsg(f);
    app_spawn(app, a, instance);
  }
  htsmsg_destroy(m);
}



/**
 * Load all applications
 */

#define LOADAPP(a)				 \
 {						 \
   extern app_t app_ ## a;			 \
   LIST_INSERT_HEAD(&apps, &app_ ## a, app_link);\
   app_init(&app_ ## a);			 \
 }

void
apps_load(void)
{
  launcher_init();
  settings_init();

  playlist_init();

  //  LOADAPP(clock);
  LOADAPP(navigator);
  //  LOADAPP(tv);

  autolaunch_applications();

  event_handler_register("speedbuttons", appi_speedbutton_switcher,
			 EVENTPRI_SPEEDBUTTONS, NULL);
}



/**
 * General settings for applications which do not provide one of
 * their own
 */
void
app_settings(appi_t *ai, glw_t *parent, 
	     const char *name, const char *miniature)
{
  abort();
#if 0
  struct layout_form_entry_list lfelist;
  glw_t *m;
  inputevent_t ie;
  char buf[200];

  snprintf(buf, sizeof(buf), "Settings for %s", name);

  TAILQ_INIT(&lfelist);

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, parent,
		 GLW_ATTRIB_FILENAME, "app_general_settings",
		 NULL);

  if(name)
    layout_update_str(m, "settings_title", buf);

  if(miniature)
    layout_update_model(m, "settings_model", miniature);


  LFE_ADD_KEYDESC(&lfelist, "speedbutton", ai->ai_speedbutton, 
		  sizeof(ai->ai_speedbutton));
  LFE_ADD_BTN(&lfelist, "ok", 0);
  
  layout_form_query(&lfelist, m, &ai->ai_gfs, &ie);
  glw_detach(m);
#endif
}
