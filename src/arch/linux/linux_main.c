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
#include <X11/Xlib.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "main.h"
#include "arch/arch.h"
#include "arch/posix/posix.h"
#include "linux.h"
#include "prop/prop.h"
#include "navigator.h"
#include "service.h"
#include "prop/prop_glib_courier.h"

hts_mutex_t gdk_mutex;
prop_courier_t *glibcourier;

static void add_xdg_paths(void);

/**
 *
 */
static void
gdk_obtain(void)
{
  hts_mutex_lock(&gdk_mutex);
}


/**
 *
 */
static void
gdk_release(void)
{
  hts_mutex_unlock(&gdk_mutex);
}

static int running;
extern const linux_ui_t ui_glw, ui_gu;
static const linux_ui_t *ui_wanted = &ui_glw, *ui_current;


/**
 *
 */
int
arch_stop_req(void)
{
  running = 0;
  g_main_context_wakeup(g_main_context_default());
  return 0;
}


/**
 *
 */
static void
switch_ui(void)
{
  if(ui_current == &ui_glw)
    ui_wanted = &ui_gu;
  else
    ui_wanted = &ui_glw;
}


/**
 *
 */
static void
mainloop(void)
{
  void *aux = NULL;

  running = 1;

  while(running) {

    if(ui_current != ui_wanted) {

      prop_t *nav;

      if(ui_current != NULL) {
	nav = ui_current->stop(aux);
      } else {
	nav = NULL;
      }

      ui_current = ui_wanted;

      aux= ui_current->start(nav);
    }

#if ENABLE_WEBPOPUP
    linux_webpopup_check();
#endif
    gtk_main_iteration();
  }

  if(ui_current != NULL) {
    prop_t *nav = ui_current->stop(aux);
    if(nav != NULL)
      prop_destroy(nav);
  }
}


/**
 *
 */
static void
linux_global_eventsink(void *opaque, event_t *e)
{
  if(event_is_action(e, ACTION_SWITCH_UI))
    switch_ui();
}


/**
 * Linux main
 */
int
main(int argc, char **argv)
{
  gconf.binary = argv[0];

  posix_init();

  XInitThreads();
  hts_mutex_init(&gdk_mutex);

  g_thread_init(NULL);
  gdk_threads_set_lock_functions(gdk_obtain, gdk_release);

  gdk_threads_init();
  gdk_threads_enter();
  gtk_init(&argc, &argv);

  parse_opts(argc, argv);

  linux_init();

  main_init();

  if(gconf.ui && !strcmp(gconf.ui, "gu"))
    ui_wanted = &ui_gu;

  glibcourier = glib_courier_create(g_main_context_default());

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "eventSink"),
		 PROP_TAG_CALLBACK_EVENT, linux_global_eventsink, NULL,
		 PROP_TAG_COURIER, glibcourier, 
		 NULL);

  add_xdg_paths();

  mainloop();

  main_fini();

  arch_exit();
}


/**
 *
 */
void
arch_exit(void)
{
  exit(gconf.exit_code);
}


/**
 *
 */
static void
add_xdg_path(const char *class, const char *type)
{
  char id[64];
  char cmd[256];
  char path[512];
  snprintf(cmd, sizeof(cmd), "xdg-user-dir %s", class);
  FILE *fp = popen(cmd, "r");
  if(fp == NULL)
    return;

  if(fscanf(fp, "%511[^\n]", path) == 1) {
    char *title = strrchr(path, '/');
    title = title ? title + 1 : path;

    snprintf(id, sizeof(id), "xdg-user-dir-%s", class);

    service_create_managed(id, title, path, type, NULL, 0, 1,
			   SVC_ORIGIN_SYSTEM);
  }
  fclose(fp);
}


/**
 *
 */
static void
add_xdg_paths(void)
{
  add_xdg_path("MUSIC", "music");
  add_xdg_path("PICTURES", "photos");
  add_xdg_path("VIDEOS", "video");
}
