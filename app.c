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

#include "showtime.h"
#include "app.h"
#include "layout/layout.h"
#include "menu.h"
#include "apps/launcher/launcher.h"
#include "apps/playlist/playlist.h"

static pthread_mutex_t app_index_mutex; 
static int app_index = 0;

LIST_HEAD(, app) apps;

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
 * Spawn a new application instance
 */
void
app_spawn(app_t *a, struct config_head *settings, int index)
{
  appi_t *ai = calloc(1, sizeof(appi_t));

  ai->ai_app = a;
  ai->ai_instance_index = index;

  input_init(&ai->ai_ic);
  mp_init(&ai->ai_mp, a->app_name, ai);
  
  glw_focus_stack_init(&ai->ai_gfs);
  
  ai->ai_settings = settings;

  a->app_spawn(ai);
}


/**
 * Create a new application instance
 */
appi_t *
appi_create(const char *name)
{
  appi_t *ai = calloc(1, sizeof(appi_t));

  input_init(&ai->ai_ic);
  mp_init(&ai->ai_mp, name, ai);
  
  glw_focus_stack_init(&ai->ai_gfs);
  return ai;
}

/**
 * Destroy an application
 */
void
appi_destroy(appi_t *ai)
{
  char buf[256];

  if(ai->ai_settings != NULL) {
    config_free0(ai->ai_settings);
    free(ai->ai_settings);
  }

  if(ai->ai_instance_index != 0 && settingsdir != NULL) {
    snprintf(buf, sizeof(buf), "%s/applications/%d", settingsdir, 
	     ai->ai_instance_index);
    unlink(buf);
  }

  mp_deinit(&ai->ai_mp);
  free((void *)ai->ai_name);
  input_flush_queue(&ai->ai_ic);
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
FILE *
appi_setings_create(appi_t *ai)
{
  char buf[256];
  struct stat st;
  FILE *fp;
  if(settingsdir == NULL)
    return NULL;

  if(ai->ai_instance_index == 0) {
    pthread_mutex_lock(&app_index_mutex);
    ai->ai_instance_index = ++app_index;
    pthread_mutex_unlock(&app_index_mutex);
  }

  snprintf(buf, sizeof(buf), "%s/applications", settingsdir);

  if(stat(buf, &st) == 0 || mkdir(buf, 0700) == 0) {
    snprintf(buf, sizeof(buf), "%s/applications/%d", settingsdir, 
	     ai->ai_instance_index);

    fp = fopen(buf, "w+");
    if(fp == NULL)
      return NULL;

    fprintf(fp, "application = %s\n", ai->ai_app->app_name);
    fprintf(fp, "instanceid = %d\n", ai->ai_instance_index);
    return fp;

  }
  return NULL;
}


/**
 * Open application settings file, truncates it if already exist.
 */
static void
autolaunch_applications(void)
{
  char buf[256];
  char fullpath[256];
  struct dirent **namelist, *d;
  int i, n;
  const char *appname;
  app_t *app;
  int index;

  struct config_head *cl;

  if(settingsdir == NULL)
    return;

  snprintf(buf, sizeof(buf), "%s/applications", settingsdir);

  n = scandir(buf, &namelist, NULL, NULL);
  if(n < 0)
    return;

  for(i = 0; i < n; i++) {
    d = namelist[i];
    if(d->d_name[0] == '.')
      continue;
    
    snprintf(fullpath, sizeof(fullpath), "%s/%s", buf, d->d_name);

    cl = malloc(sizeof(struct config_head));

    TAILQ_INIT(cl);
    if(config_read_file0(fullpath, cl) == -1)
      continue;
    
    appname = config_get_str_sub(cl, "application", NULL);
    
    app = app_find_by_name(appname);
    if(app == NULL) {
      fprintf(stderr, "Unable to spawn app %s, application does not exist\n",
	      appname);
      config_free0(cl);
      free(cl);
      continue;
    }

    index = atoi(config_get_str_sub(cl, "instanceid", "0"));
    if(index == 0)
      continue;

    if(index > app_index)
      app_index = index;

    app_spawn(app, cl, index);
  }
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

  playlist_init();

  LOADAPP(clock);
  LOADAPP(navigator);

  autolaunch_applications();
}
