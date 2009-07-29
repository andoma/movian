/*
 *  Showtime GTK frontend
 *  Copyright (C) 2009 Andreas Öman
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
#include "navigator.h"
#include "gu.h"
#include "showtime.h"

/**
 *
 */
typedef struct playdeck {
  GtkWidget *root;
  GtkWidget *tbar;
  GtkWidget *volume;

  GtkToolItem *prev, *next, *playpause;

  action_type_t pp_action;

  prop_sub_t *volsub;

  GtkObject *pos_adjust;
  GtkWidget *pos_slider;
  prop_sub_t *pos_sub;
  int pos_grabbed;

  char *cur_artist;
  char *cur_album;

  GtkWidget *trackextra;

} playdeck_t;



/**
 *
 */
static void
prev_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  event_post(event_create_action(ACTION_PREV_TRACK));
}


/**
 *
 */
static void
next_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  event_post(event_create_action(ACTION_NEXT_TRACK));
}


/**
 *
 */
static void
playpause_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  playdeck_t *pd = user_data;
  event_post(event_create_action(pd->pp_action));
}


/**
 *
 */
static void
bar_sensitivity(playdeck_t *pd, gboolean v)
{
  gtk_widget_set_sensitive(GTK_WIDGET(pd->prev),       v);
  gtk_widget_set_sensitive(GTK_WIDGET(pd->next),       v);
  gtk_widget_set_sensitive(GTK_WIDGET(pd->playpause),  v);
  gtk_widget_set_sensitive(GTK_WIDGET(pd->pos_slider), v);
}


/**
 *
 */
static void
update_playstatus(void *opaque, prop_event_t event, ...)
{
  playdeck_t *pd = opaque;
  const char *status;

  va_list ap;
  va_start(ap, event);

  switch(event) {
  default:
  case PROP_SET_VOID:
    bar_sensitivity(pd, FALSE);
    break;

  case PROP_SET_STRING:
    bar_sensitivity(pd, TRUE);

    status = va_arg(ap, char *);
    gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(pd->playpause), 
				 !strcmp(status, "play") ? 
				 GTK_STOCK_MEDIA_PAUSE:
				 GTK_STOCK_MEDIA_PLAY);

    pd->pp_action = !strcmp(status, "play") ? ACTION_PAUSE : ACTION_PLAY;
    break;
  }
}

/**
 *
 */
static void
update_mastervol(void *opaque, float value)
{
  playdeck_t *pd = opaque;

  value = (value / 75.0) + 1;
  gtk_scale_button_set_value(GTK_SCALE_BUTTON(pd->volume), value);
}


/**
 *
 */
static void
read_mastervol(GtkScaleButton *button, gdouble value, gpointer user_data)
{
  playdeck_t *pd = user_data;
  extern prop_t *prop_mastervol; /* A bit ugly.  We could use
				    prop_get_by_name(), but this is
				    easier */
  
  value = (value - 1) * 75;
  prop_set_float_ex(prop_mastervol, pd->volsub, value);
}


/**
 *
 */
static void
update_duration(void *opaque, float value)
{
  playdeck_t *pd = opaque;
  gtk_adjustment_set_value(GTK_ADJUSTMENT(pd->pos_adjust), 0);
  gtk_adjustment_set_upper(GTK_ADJUSTMENT(pd->pos_adjust), value);
}


/**
 *
 */
static void
update_curtime(void *opaque, float value)
{
  playdeck_t *pd = opaque;

  if(!pd->pos_grabbed)
    gtk_adjustment_set_value(GTK_ADJUSTMENT(pd->pos_adjust), value);
}


/**
 *
 */
static void
slider_grabbed(GtkWidget *widget, gpointer user_data)
{
  playdeck_t *pd = user_data;
  pd->pos_grabbed = 1;
}

/**
 *
 */
static gboolean 
slider_updated(GtkRange *range, GtkScrollType scroll,
	       gdouble value, gpointer user_data)
{
  playdeck_t *pd = user_data;
  prop_t *p;

  pd->pos_grabbed = 0;

  p = prop_get_by_name(PNVEC("global", "media", "current", "currenttime"),
		       1, NULL);
  if(p != NULL) {
    prop_set_float_ex(p, pd->pos_sub, value);
    prop_ref_dec(p);
  }
  return FALSE;
}

static gchar *
slider_value_callback(GtkScale *scale, gdouble value)
{
  int v = value;
  return g_strdup_printf("%d:%02d", v / 60, v % 60);
 }


/**
 *
 */
static void
pd_set_albumart(void *opaque, const char *str)
{
  GdkPixbuf *pb;

  if(str == NULL) {
    gtk_widget_hide(GTK_WIDGET(opaque));
    return;
  }

  pb = gu_pixbuf_get_sync(str, -1, 84);
  g_object_set(G_OBJECT(opaque), "pixbuf", pb, NULL);
  if(pb != NULL)
    g_object_unref(G_OBJECT(pb));
  gtk_widget_show(GTK_WIDGET(opaque));
}


/**
 *
 */
static void
set_current_title(void *opaque, const char *str)
{
  char *m;

  if(str == NULL) {
    gtk_widget_hide(GTK_WIDGET(opaque));
    return;
  }

  m = g_markup_printf_escaped("<span size=\"x-large\">%s</span>", str);
  gtk_label_set_markup(GTK_LABEL(opaque), m);
  g_free(m);
  gtk_widget_show(GTK_WIDGET(opaque));
}


/**
 *
 */
static void
pd_update_trackextra(playdeck_t *pd)
{
  char *m;

  if(pd->cur_artist == NULL &&
     pd->cur_album == NULL) {
    gtk_widget_hide(GTK_WIDGET(pd->trackextra));
    return;
  }

  m = g_markup_printf_escaped("<span>%s%s%s</span>", 
			      pd->cur_artist ?: "",
			      pd->cur_artist && pd->cur_album ? " - " : "",
			      pd->cur_album ?: "");

  gtk_label_set_markup(GTK_LABEL(pd->trackextra), m);
  g_free(m);
  gtk_widget_show(GTK_WIDGET(pd->trackextra));
}



/**
 *
 */
static void
set_current_artist(void *opaque, const char *str)
{
  playdeck_t *pd = opaque;
  free(pd->cur_artist);
  pd->cur_artist = str ? strdup(str) : NULL;
  pd_update_trackextra(pd);
}


/**
 *
 */
static void
set_current_album(void *opaque, const char *str)
{
  playdeck_t *pd = opaque;
  free(pd->cur_album);
  pd->cur_album = str ? strdup(str) : NULL;
  pd_update_trackextra(pd);
}


/**
 *
 */
void
gu_playdeck_add(gtk_ui_t *gu, GtkWidget *parent)
{
  GtkToolItem *ti;
  GtkWidget *w;
  GtkWidget *l;
  GtkWidget *vbox;

  playdeck_t *pd = calloc(1, sizeof(playdeck_t));


  w = gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(parent), w, FALSE, TRUE, 0);
  

  pd->root = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(parent), pd->root, FALSE, TRUE, 0);

  /* Playdeck album art */
  w = gtk_image_new();
  gtk_misc_set_alignment(GTK_MISC(w), 0, 1);
  gtk_box_pack_start(GTK_BOX(pd->root), w, FALSE, TRUE, 0);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", 
			       "metadata", "album_art"),
		 PROP_TAG_CALLBACK_STRING, pd_set_albumart, w,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  /* Middle vbox */
  vbox = gtk_vbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(pd->root), vbox, TRUE, TRUE, 0);

  
  /* Title of current track */
  l = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(l), 0, 0);
  gtk_box_pack_start(GTK_BOX(vbox), l, TRUE, TRUE, 0);
  g_object_set(G_OBJECT(l), 
	       "ellipsize", PANGO_ELLIPSIZE_END, NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", 
			       "metadata", "title"),
		 PROP_TAG_CALLBACK_STRING, set_current_title, l,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  /* Title of current track */
  pd->trackextra = gtk_label_new("");
  g_object_set(G_OBJECT(pd->trackextra), 
	       "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_misc_set_alignment(GTK_MISC(pd->trackextra), 0, 0);
  gtk_box_pack_start(GTK_BOX(vbox), pd->trackextra, TRUE, TRUE, 0);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", 
			       "metadata", "album"),
		 PROP_TAG_CALLBACK_STRING, set_current_album, pd,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", 
			       "metadata", "artist"),
		 PROP_TAG_CALLBACK_STRING, set_current_artist, pd,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);



  /* The toolbar */
  pd->tbar = gtk_toolbar_new();
  gtk_toolbar_set_style(GTK_TOOLBAR(pd->tbar), GTK_TOOLBAR_ICONS);
  gtk_box_pack_start(GTK_BOX(vbox), pd->tbar, FALSE, TRUE, 0);

  /* Prev button */
  pd->prev = ti = gtk_tool_button_new_from_stock(GTK_STOCK_MEDIA_PREVIOUS);
  gtk_toolbar_insert(GTK_TOOLBAR(pd->tbar), ti, -1);
  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(prev_clicked), pd);

  /* Play / Pause */
  pd->playpause = ti = gtk_tool_button_new_from_stock(GTK_STOCK_MEDIA_PLAY);
  gtk_toolbar_insert(GTK_TOOLBAR(pd->tbar), ti, -1);
  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(playpause_clicked), pd);

  /* Next button */
  pd->next = ti = gtk_tool_button_new_from_stock(GTK_STOCK_MEDIA_NEXT);
  gtk_toolbar_insert(GTK_TOOLBAR(pd->tbar), ti, -1);
  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(next_clicked), pd);

  /* Separator */
  ti = gtk_separator_tool_item_new();
  gtk_toolbar_insert(GTK_TOOLBAR(pd->tbar), ti, -1);

  /* Subscribe to playstatus */
  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "playstatus"),
		 PROP_TAG_CALLBACK, update_playstatus, pd,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  /**
   * Media position
   */
  pd->pos_adjust = gtk_adjustment_new(0, 0, 0, 0, 0, 0);

  pd->pos_slider = gtk_hscale_new(GTK_ADJUSTMENT(pd->pos_adjust));
  gtk_scale_set_value_pos (GTK_SCALE(pd->pos_slider), GTK_POS_LEFT);

  g_signal_connect(G_OBJECT(pd->pos_slider), "grab-focus", 
		   G_CALLBACK(slider_grabbed), pd);

  g_signal_connect(G_OBJECT(pd->pos_slider), "change-value", 
		   G_CALLBACK(slider_updated), pd);

  g_signal_connect(G_OBJECT(pd->pos_slider), "format-value", 
		   G_CALLBACK(slider_value_callback), pd);

  ti = gtk_tool_item_new();
  gtk_tool_item_set_expand(ti, TRUE);
  gtk_container_add(GTK_CONTAINER(ti), pd->pos_slider);
  gtk_toolbar_insert(GTK_TOOLBAR(pd->tbar), ti, -1);
  
  /* Subscribe to current track position */
  pd->pos_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("global", "media", "current", "currenttime"),
		   PROP_TAG_CALLBACK_FLOAT, update_curtime, pd,
		   PROP_TAG_COURIER, gu->gu_pc,
		   NULL);

  /* Subscribe to current track duration */
  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", 
			       "metadata", "duration"),
		 PROP_TAG_CALLBACK_FLOAT, update_duration, pd,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);



  /* Separator */
  ti = gtk_separator_tool_item_new();
  gtk_toolbar_insert(GTK_TOOLBAR(pd->tbar), ti, -1);

  /**
   * Volume control
   */
  ti = gtk_tool_item_new();
  pd->volume = gtk_volume_button_new();
  gtk_container_add(GTK_CONTAINER(ti), pd->volume);
  gtk_toolbar_insert(GTK_TOOLBAR(pd->tbar), ti, -1);

  g_signal_connect(G_OBJECT(pd->volume), "value-changed", 
		   G_CALLBACK(read_mastervol), pd);

  pd->volsub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("global", "audio", "mastervolume"),
		   PROP_TAG_CALLBACK_FLOAT, update_mastervol, pd,
		   PROP_TAG_COURIER, gu->gu_pc,
		   NULL);

}

