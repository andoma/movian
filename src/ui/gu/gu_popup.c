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

#include "showtime.h"
#include "event.h"
#include "gu.h"

typedef struct popup {
  LIST_ENTRY(popup) link;
  prop_t *p;
  GtkWidget *win;

  GtkWidget *username;
  GtkWidget *password;

} popup_t;


/**
 *
 */
static void
popup_destroy(popup_t *pop)
{
  gtk_widget_destroy(pop->win);
  prop_ref_dec(pop->p);
  LIST_REMOVE(pop, link);
  free(pop);
}


/**
 *
 */
static void
popup_send_result(popup_t *pop, action_type_t res)
{
  prop_t *p;

  p = prop_get_by_name(PNVEC("self", "eventSink"), 1,
		       PROP_TAG_NAMED_ROOT, pop->p, "self",
		       NULL);
  if(p != NULL) {
    event_t *e = event_create_action(res);
    prop_send_ext_event(p, e);
    event_release(e);
    prop_ref_dec(p);
  }
}


/**
 *
 */
static void
auth_ok(GtkButton *unused__, gpointer user_data)
{
  popup_t *pop = user_data;
  prop_t *p;

  p = prop_get_by_name(PNVEC("self", "username"), 1,
		       PROP_TAG_NAMED_ROOT, pop->p, "self",
		       NULL);
  if(p != NULL) {
    prop_set_string(p, gtk_entry_get_text(GTK_ENTRY(pop->username)));
    prop_ref_dec(p);
  }

  p = prop_get_by_name(PNVEC("self", "password"), 1,
		       PROP_TAG_NAMED_ROOT, pop->p, "self",
		       NULL);
  if(p != NULL) {
    prop_set_string(p, gtk_entry_get_text(GTK_ENTRY(pop->password)));
    prop_ref_dec(p);
  }
  popup_send_result(pop, ACTION_OK);
}


/**
 *
 */
static void
auth_cancel(GtkButton *unused__, gpointer user_data)
{
  popup_t *pop = user_data;
  popup_send_result(pop, ACTION_CANCEL);
}




/**
 *
 */
static void
popup_create_auth(gtk_ui_t *gu, prop_t *p)
{
  GtkWidget *vbox, *hbox;
  GtkWidget *win;
  GtkWidget *l, *e, *w;
  prop_sub_t *s;

  popup_t *pop = calloc(1, sizeof(popup_t));

  pop->p = prop_ref_inc(p);
  LIST_INSERT_HEAD(&gu->popups, pop, link);

  /* The window */

  pop->win = win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  
  gtk_window_set_title(GTK_WINDOW(win), "Authentication request");
  gtk_window_set_default_size(GTK_WINDOW(win), 400, 180);
  gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
  gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER_ON_PARENT);
  //  gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(gu->gu_window));


  /* Vbox */

  vbox = gtk_vbox_new(FALSE, 1);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
  gtk_container_add(GTK_CONTAINER(win), vbox);

  /* ID label */
  l = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(vbox), l, FALSE, TRUE, 0);

  s = prop_subscribe(0,
		     PROP_TAG_NAME("self", "id"),
		     PROP_TAG_CALLBACK_STRING, gu_subscription_set_label, l,
		     PROP_TAG_COURIER, glibcourier, 
		     PROP_TAG_NAMED_ROOT, p, "self",
		     NULL);
  gu_unsubscribe_on_destroy(GTK_OBJECT(l), s);

  /* Reason label */
  l = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(vbox), l, FALSE, TRUE, 0);

  s = prop_subscribe(0,
		     PROP_TAG_NAME("self", "reason"),
		     PROP_TAG_CALLBACK_STRING, gu_subscription_set_label, l,
		     PROP_TAG_COURIER, glibcourier,
		     PROP_TAG_NAMED_ROOT, p, "self",
		     NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(l), s);

  /* Username */

  hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

  l = gtk_label_new("Username:");
  gtk_label_set_width_chars(GTK_LABEL(l), 10);
  gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_RIGHT);
  gtk_box_pack_start(GTK_BOX(hbox), l, FALSE, TRUE, 0);
  
  pop->username = e = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(hbox), e, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(e), "activate", G_CALLBACK(auth_ok), pop);

  /* Password */

  hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

  l = gtk_label_new("Password:");
  gtk_label_set_width_chars(GTK_LABEL(l), 10);
  gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_RIGHT);
  gtk_box_pack_start(GTK_BOX(hbox), l, FALSE, TRUE, 0);
  
  pop->password = e = gtk_entry_new();
  gtk_entry_set_visibility(GTK_ENTRY(e), FALSE);
  gtk_box_pack_start(GTK_BOX(hbox), e, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(e), "activate", G_CALLBACK(auth_ok), pop);


  /* Separator */

  w = gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(vbox), w, FALSE, TRUE, 0);

  /* Action buttons */

  hbox = gtk_hbutton_box_new();
  gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

  w = gtk_button_new_from_stock(GTK_STOCK_OK);
  gtk_container_add(GTK_CONTAINER(hbox), w);
  g_signal_connect(G_OBJECT(w), "clicked", G_CALLBACK(auth_ok), pop);

  w = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
  gtk_container_add(GTK_CONTAINER(hbox), w);
  g_signal_connect(G_OBJECT(w), "clicked", G_CALLBACK(auth_cancel), pop);

  gtk_widget_show_all(win);
}



/**
 *
 */
static void
popups_update(void *opaque, prop_event_t event, ...)
{
  gtk_ui_t *gu = opaque;
  prop_t *p;
  rstr_t *str;
  popup_t *pop;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);

    if((str = prop_get_string(p, "type", NULL)) != NULL) {
      if(!strcmp(rstr_get(str), "auth"))
	popup_create_auth(gu, p);
      rstr_release(str);
    }
    break;

  case PROP_DEL_CHILD:
    p = va_arg(ap, prop_t *);

    LIST_FOREACH(pop, &gu->popups, link)
      if(pop->p == p)
	break;

    if(pop != NULL)
      popup_destroy(pop);
    break;

  default:
    break;
  }
}


/**
 *
 */
void
gu_popup_init(gtk_ui_t *gu)
{
  gu->gu_popup_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("global", "popups"),
		   PROP_TAG_CALLBACK, popups_update, gu,
		   PROP_TAG_COURIER, glibcourier,
		   NULL);
}


/**
 *
 */
void
gu_popup_fini(gtk_ui_t *gu)
{
  prop_unsubscribe(gu->gu_popup_sub);

  popup_t *pop;
  while((pop = LIST_FIRST(&gu->popups)) != NULL)
    popup_destroy(pop);

}

