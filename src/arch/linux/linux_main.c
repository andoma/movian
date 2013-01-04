/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2012 Andreas Öman
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


#include <X11/Xlib.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "showtime.h"
#include "arch/arch.h"
#include "arch/posix/posix.h"

#include "linux.h"
#include "prop/prop.h"


hts_mutex_t gdk_mutex;
prop_courier_t *glibcourier;

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




/**
 *
 */
static void
wakeup_glib_mainloop(void *aux)
{
  g_main_context_wakeup(g_main_context_default());
}


/**
 *
 */
static gboolean
courier_prepare(GSource *s, gint *timeout)
{
  *timeout = -1;
  return FALSE;
}


/**
 *
 */
static gboolean
courier_check(GSource *s)
{
  return prop_courier_check(glibcourier);
}


/**
 *
 */
static gboolean
courier_dispatch(GSource *s, GSourceFunc callback, gpointer aux)
{
  prop_courier_poll(glibcourier);
  return TRUE;
}


/**
 *
 */
static GSourceFuncs source_funcs = {
  courier_prepare, 
  courier_check,
  courier_dispatch,
};


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

  gconf.concurrency = get_system_concurrency();

  trap_init();

  showtime_init();

  glibcourier = prop_courier_create_notify(wakeup_glib_mainloop, NULL);
  g_source_attach(g_source_new(&source_funcs, sizeof(GSource)),
		  g_main_context_default());

  linux_init_monitors();

#if ENABLE_GU
  if(gconf.ui && !strcmp(gconf.ui, "gu")) {
    extern void gu_start(void);
    gu_start();
  } else
#endif
 {
  extern void glw_x11_start(void);
  glw_x11_start();
 }

  showtime_fini();

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
