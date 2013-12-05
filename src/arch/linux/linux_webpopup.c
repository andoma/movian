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

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include "showtime.h"
#include "ui/webpopup.h"
#include "misc/str.h"
#include "linux.h"

extern hts_mutex_t gdk_mutex;

LIST_HEAD(webpopup_list, webpopup);

static struct webpopup_list pending_open, pending_close;

/**
 *
 */
typedef struct webpopup {
  webpopup_result_t wp_wr; // Must be first
  LIST_ENTRY(webpopup) wp_link;
  char *wp_url;
  char *wp_title;
  char *wp_trap;
  hts_cond_t wp_cond;

  GtkWidget *wp_win;
  //  GtkWidget *wp_scrolled_win;
  GtkWidget *wp_webview;

} webpopup_t;


/**
 *
 */
static void
finalize(webpopup_t *wp, int code)
{
  if(wp->wp_wr.wr_resultcode != -1)
    return;
  wp->wp_wr.wr_resultcode = code;
  webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(wp->wp_webview));
  LIST_INSERT_HEAD(&pending_close, wp, wp_link);
}


/**
 *
 */
static gboolean
navigation_policy_decision_requested(WebKitWebView             *web_view,
				     WebKitWebFrame            *frame,
				     WebKitNetworkRequest      *request,
				     WebKitWebNavigationAction *action,
				     WebKitWebPolicyDecision   *decision,
				     gpointer                   user_data)
{
  webpopup_t *wp = user_data;

  const char *uri = webkit_network_request_get_uri(request);
  if(mystrbegins(uri, wp->wp_trap)) {
    TRACE(TRACE_DEBUG, "Webkit", "Opening %s -- Final URI reached", uri);
    mystrset(&wp->wp_wr.wr_trapped.url, uri);
    finalize(wp, WEBPOPUP_TRAPPED_URL);
    webkit_web_policy_decision_ignore(decision);

  } else {
    TRACE(TRACE_DEBUG, "Webkit", "Opening %s", uri);
    webkit_web_policy_decision_use(decision);
  }
  return TRUE;
}



/**
 *
 */
static gboolean
load_error(WebKitWebView  *web_view,
	   WebKitWebFrame *web_frame,
	   gchar          *uri,
	   GError         *web_error,
	   gpointer        user_data) 
{
  webpopup_t *wp = user_data;

  if(wp->wp_wr.wr_resultcode != -1)
    return TRUE;

  TRACE(TRACE_ERROR, "WebKit", "Failed to load %s -- %s",
	uri, web_error->message);
  
  finalize(wp, WEBPOPUP_LOAD_ERROR);
  return TRUE;
}



/**
 *
 */
static gboolean 
closed_window(GtkWidget *widget, GdkEvent *event, gpointer data)
{
  webpopup_t *wp = data;
  finalize(wp, WEBPOPUP_CLOSED_BY_USER);
  return TRUE;
}

/**
 *
 */
void
linux_webpopup_check(void)
{
  webpopup_t *wp;
  while((wp = LIST_FIRST(&pending_open)) != NULL) {
    LIST_REMOVE(wp, wp_link);
    wp->wp_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(wp->wp_win), wp->wp_title);
    wp->wp_webview = webkit_web_view_new();


#if 0
    wp->wp_scrolled_win = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(wp->wp_scrolled_win), wp->wp_webview);
    gtk_container_add(GTK_CONTAINER(wp->wp_win), wp->wp_scrolled_win);
#else
    WebKitWebSettings *s =
      webkit_web_view_get_settings(WEBKIT_WEB_VIEW(wp->wp_webview));
    g_object_set(G_OBJECT(s), "auto-resize-window", 1, NULL);
    gtk_container_add(GTK_CONTAINER(wp->wp_win), wp->wp_webview);
#endif
    g_signal_connect(G_OBJECT(wp->wp_webview),
		     "navigation-policy-decision-requested",
		     G_CALLBACK(navigation_policy_decision_requested), wp);

    g_signal_connect(G_OBJECT(wp->wp_webview),
		     "load-error",
		     G_CALLBACK(load_error), wp);

    g_signal_connect(G_OBJECT(wp->wp_win), "delete_event",
		     G_CALLBACK(closed_window), wp);

    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(wp->wp_webview), wp->wp_url);

    //gtk_window_set_default_size(GTK_WINDOW(wp->wp_win), 1200, 800);

    gtk_widget_show_all(wp->wp_win);
  }

  while((wp = LIST_FIRST(&pending_close)) != NULL) {
    LIST_REMOVE(wp, wp_link);
    hts_cond_signal(&wp->wp_cond);
    gtk_widget_destroy(wp->wp_win);
  }
}


/**
 *
 */
webpopup_result_t *
webpopup_create(const char *url, const char *title, const char *trap)
{
  webpopup_t *wp = calloc(1, sizeof(webpopup_t));
  webpopup_result_t *wr = &wp->wp_wr;
  hts_cond_init(&wp->wp_cond, &gdk_mutex);
  wp->wp_wr.wr_resultcode = -1;
  wp->wp_url   = strdup(url);
  wp->wp_title = strdup(title);
  wp->wp_trap  = strdup(trap);

  hts_mutex_lock(&gdk_mutex);
  LIST_INSERT_HEAD(&pending_open, wp, wp_link);
  g_main_context_wakeup(g_main_context_default());

  while(wp->wp_wr.wr_resultcode == -1)
    hts_cond_wait(&wp->wp_cond, &gdk_mutex);

  gdk_threads_leave();

  webpopup_finalize_result(wr);

  free(wp->wp_url);
  free(wp->wp_title);
  free(wp->wp_trap);

  return wr;
}


/**
 *
 */
void
linux_webpopup_init(void)
{
}
