/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include <glib.h>

#include "prop.h"
#include "prop_glib_courier.h"

typedef struct glib_courier {
  GSource s;
  prop_courier_t *pc;
} glib_courier_t;


/**
 *
 */
static void
glib_courier_wakeup(void *aux)
{
  g_main_context_wakeup(aux);
}


/**
 *
 */
static gboolean
glib_courier_prepare(GSource *s, gint *timeout)
{
  *timeout = -1;
  return FALSE;
}


/**
 *
 */
static gboolean
glib_courier_check(GSource *s)
{
  glib_courier_t *gc = (glib_courier_t *)s;
  return prop_courier_check(gc->pc);
}


/**
 *
 */
static gboolean
glib_courier_dispatch(GSource *s, GSourceFunc callback, gpointer aux)
{
  glib_courier_t *gc = (glib_courier_t *)s;
  prop_courier_poll(gc->pc);
  return TRUE;
}


/**
 *
 */
static GSourceFuncs source_funcs = {
  glib_courier_prepare, 
  glib_courier_check,
  glib_courier_dispatch,
};


/**
 *
 */
prop_courier_t *
glib_courier_create(GMainContext *ctx)
{
  prop_courier_t *pc = prop_courier_create_notify(glib_courier_wakeup, ctx);
  GSource *s = g_source_new(&source_funcs, sizeof(glib_courier_t));
  glib_courier_t *gc = (glib_courier_t *)s;
  gc->pc = pc;
  g_source_attach(s, ctx);
  return pc;
}
