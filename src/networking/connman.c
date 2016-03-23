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
#include <gio/gio.h>

#include "main.h"
#include "prop/prop.h"
#include "prop/prop_gvariant.h"
#include "prop/prop_glib_courier.h"
#include "prop/prop_concat.h"
#include "backend/backend.h"
#include "notifications.h"
#include "settings.h"
#include "htsmsg/htsmsg_store.h"

#include "connman.h"

static prop_t *netconf_model;
static prop_t *service_nodes;
static prop_t *net_state;
static prop_courier_t *connman_courier;
static prop_t *connman_settings;
static int have_wifi;

#define STRINGIFY(A)  #A

/**
 *
 */
static const char *connman_agent_xml = STRINGIFY(
<node name="/net/connman/Agent">
  <interface name="net.connman.Agent">
    <method name="ReportError">
       <annotation name="org.freedesktop.DBus.GLib.Async" value="true"/>
       <arg type="o" direction="in"/>
       <arg type="s" direction="in"/>
    </method>

    <method name="RequestInput">
      <annotation name="org.freedesktop.DBus.GLib.Async" value="true"/>
      <arg type="o" direction="in"/>
      <arg type="a{sv}" direction="in"/>
      <arg type="a{sv}" direction="out"/>
    </method>

    <method name="Cancel">
      <annotation name="org.freedesktop.DBus.GLib.Async" value=""/>
    </method>

    <method name="Release">
      <annotation name="org.freedesktop.DBus.GLib.Async" value=""/>
   </method>
  </interface>
</node>
					  );



TAILQ_HEAD(connman_service_queue, connman_service);

/**
 *
 */
typedef struct connman_service {
  int cs_refcount;
  TAILQ_ENTRY(connman_service) cs_link;
  prop_t *cs_prop;
  char *cs_path;
  prop_sub_t *cs_sub;
  GDBusProxy *cs_proxy;
  char *cs_name;

  // Input request

  prop_t *cs_input_req_prop;
  prop_sub_t *cs_input_req_sub;
  GDBusMethodInvocation *cs_input_req_inv;
  int cs_input_req_want_identity;

} connman_service_t;

static struct connman_service_queue connman_services;


/**
 *
 */
static connman_service_t *
connman_service_find(const char *path)
{
  connman_service_t *cs;
  TAILQ_FOREACH(cs, &connman_services, cs_link)
    if(!strcmp(cs->cs_path, path))
      return cs;
  return NULL;
}


/**
 *
 */
static void
connman_stop_input_request(connman_service_t *cs)
{
  prop_unsubscribe(cs->cs_input_req_sub);
  cs->cs_input_req_sub = NULL;

  prop_destroy(cs->cs_input_req_prop);
  cs->cs_input_req_prop = NULL;

  if(cs->cs_input_req_inv != NULL) {
    g_object_unref(G_OBJECT(cs->cs_input_req_inv));
    cs->cs_input_req_inv = NULL;
  }
}

/**
 *
 */
static void
connman_connect_cb(GObject *source_object,
		   GAsyncResult *res,
		   gpointer user_data)
{
  connman_service_t *cs = user_data;
  GError *err = NULL;
  GVariant *v =  g_dbus_proxy_call_finish(cs->cs_proxy, res, &err);

  if(v == NULL) {
    connman_stop_input_request(cs);
    TRACE(TRACE_ERROR, "CONNMAN", "Unable to connect to %s -- %s",
	  cs->cs_path, err->message);
    g_error_free(err);
    return;
  }
  g_variant_unref(v);
}


/**
 *
 */
static void
connman_service_connect(connman_service_t *cs)
{
  GError *err = NULL;
  GVariant *v = g_dbus_proxy_call_sync(cs->cs_proxy, "Disconnect", NULL,
                                       G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
  if(v == NULL) {
    g_error_free(err);
  } else {
    g_variant_unref(v);
  }

  TRACE(TRACE_DEBUG, "CONNMAN", "User request connect to %s", cs->cs_path);

  g_dbus_proxy_call(cs->cs_proxy, "Connect", NULL,
		    G_DBUS_CALL_FLAGS_NONE, 600 * 1000, NULL,
		    connman_connect_cb, cs);
}


/**
 *
 */
static void
connman_service_event(void *opaque, event_t *e)
{
  connman_service_t *cs = opaque;
  if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
    connman_service_connect(cs);
  }
}


/**
 *
 */
static void
connman_svc_signal(GDBusProxy *proxy,
		   gchar      *sender_name,
		   gchar      *signal_name,
		   GVariant   *parameters,
		   gpointer    user_data)
{
  connman_service_t *cs = user_data;
  TRACE(TRACE_DEBUG, "CONNMAN", "Service-signal %s %s from %s",
	cs->cs_path, signal_name, sender_name);
  TRACE(TRACE_DEBUG, "CONNMAN", "%s", g_variant_print(parameters, TRUE));

  if(!strcmp(signal_name, "PropertyChanged")) {
    prop_set_from_tuple(parameters, prop_create(cs->cs_prop, "metadata"));
  }
}


/**
 *
 */
static void
connman_service_release(connman_service_t *cs)
{
  cs->cs_refcount--;
  if(cs->cs_refcount > 0)
    return;

  g_object_unref(cs->cs_proxy);
  free(cs->cs_name);
  free(cs->cs_path);
  free(cs);
}



/**
 *
 */
static void
connman_service_destroy(connman_service_t *cs)
{
  connman_stop_input_request(cs);
  prop_unsubscribe(cs->cs_sub);
  TAILQ_REMOVE(&connman_services, cs, cs_link);
  prop_destroy(cs->cs_prop);
  cs->cs_prop = NULL;
  connman_service_release(cs);
}



/**
 *
 */
static void
input_req_event(void *opaque, event_t *e)
{
  connman_service_t *cs = opaque;
  if(cs->cs_input_req_inv == NULL)
    return;

  if(event_is_action(e, ACTION_OK)) {

    prop_print_tree(cs->cs_input_req_prop, 1);

    rstr_t *username = prop_get_string(cs->cs_input_req_prop, "username", NULL);
    rstr_t *password = prop_get_string(cs->cs_input_req_prop, "password", NULL);

    GVariant *result;
    GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

    g_variant_builder_add(builder, "{sv}", "Passphrase",
			  g_variant_new_string(rstr_get(password)));

    if(cs->cs_input_req_want_identity)
      g_variant_builder_add(builder, "{sv}", "Identity",
			    g_variant_new_string(rstr_get(username)));

    result = g_variant_new("(a{sv})", builder);

    TRACE(TRACE_DEBUG, "CONNMAN", "Auth response: %s",
	  g_variant_print(result, TRUE));

    g_dbus_method_invocation_return_value(cs->cs_input_req_inv, result);

    g_variant_builder_unref(builder);
    rstr_release(username);
    rstr_release(password);
    connman_stop_input_request(cs);
  }

  if(event_is_action(e, ACTION_CANCEL)) {
    g_dbus_method_invocation_return_dbus_error(cs->cs_input_req_inv,
					       "net.connman.Agent.Error.Canceled",
					       "Canceled by user");
    connman_stop_input_request(cs);
  }

}


/**
 *  Popup an request to the user on behalf of connman
 */
static GVariant *
agent_request_input(connman_service_t *cs, GVariant *req,
		    GDBusMethodInvocation *inv)
{
  TRACE(TRACE_INFO, "CONNMAN", "Requesting credentials for %s", cs->cs_path);

  TRACE(TRACE_DEBUG, "CONNMAN", "RequestInput: %s",
	g_variant_print(req, TRUE));

  prop_t *p = prop_create_root(NULL);

  prop_set(p, "type",   PROP_SET_STRING, "auth");
  prop_set(p, "id",     PROP_SET_STRING, cs->cs_path);
  prop_set(p, "source", PROP_SET_STRING, "Network");


  GVariant *prev = g_variant_lookup_value(req, "PreviousPassphrase", NULL);

  if(prev) {
    prop_set(p, "reason", PROP_SET_STRING, "Password incorrect");
  } else {
    prop_set(p, "reason", PROP_SET_STRING, "Password needed");
  }

  GVariant *identity = g_variant_lookup_value(req, "Identity", NULL);

  cs->cs_input_req_want_identity = identity != NULL;
  prop_set(p, "disableUsername", PROP_SET_INT, !cs->cs_input_req_want_identity);

  prop_set(p, "disableDomain", PROP_SET_INT, 1);

  prop_t *r = prop_create(p, "eventSink");

  cs->cs_input_req_sub =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK_EVENT, input_req_event, cs,
		   PROP_TAG_NAMED_ROOT, r, "popup",
		   PROP_TAG_COURIER, connman_courier,
		   NULL);

  cs->cs_input_req_prop = p;

  /* Will show the popup */
  if(prop_set_parent(p, prop_create(prop_get_global(), "popups"))) {
    /* popuproot is a zombie, this is an error */
    abort();
  }

  cs->cs_input_req_inv = inv;
  g_object_ref(G_OBJECT(inv));

  return NULL;
}


/**
 *
 */
static connman_service_t *
update_service(GVariant *v, const char *path, connman_service_t *after)
{
  connman_service_t *cs = connman_service_find(path);

  if(cs == NULL) {

    GError *err = NULL;
    GDBusProxy *proxy =
      g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
				    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START ,
				    NULL,
				    "net.connman",
				    path,
				    "net.connman.Service",
				    NULL,
				    &err);
    if(proxy == NULL) {
      TRACE(TRACE_ERROR, "CONNMAN", "Unable to connect to service %s -- %s",
	    path, err->message);
      g_error_free(err);
      return NULL;
    }

    cs = calloc(1, sizeof(connman_service_t));
    cs->cs_refcount = 1;
    cs->cs_proxy = proxy;

    if(after == NULL) {
      TAILQ_INSERT_HEAD(&connman_services, cs, cs_link);
    } else {
      TAILQ_INSERT_AFTER(&connman_services, after, cs, cs_link);
    }

    connman_service_t *next = TAILQ_NEXT(cs, cs_link);

    cs->cs_prop = prop_create_root(path);
    cs->cs_path = strdup(path);

    cs->cs_sub =
      prop_subscribe(0,
		     PROP_TAG_CALLBACK_EVENT, connman_service_event, cs,
		     PROP_TAG_ROOT, cs->cs_prop,
		     PROP_TAG_COURIER, connman_courier,
		     NULL);

    g_signal_connect(G_OBJECT(cs->cs_proxy), "g-signal",
		     G_CALLBACK(connman_svc_signal), cs);

    // Insert at correct position

    if(prop_set_parent_ex(cs->cs_prop, service_nodes,
			  next ? next->cs_prop : NULL, NULL))
      abort();

    prop_t *m = prop_create(cs->cs_prop, "metadata");
    prop_link(prop_create(m, "name"), prop_create(m, "title"));
    prop_set(cs->cs_prop, "type", PROP_SET_STRING, "network");

  } else {

    // Possibly move

    TAILQ_REMOVE(&connman_services, cs, cs_link);

    if(after == NULL) {
      TAILQ_INSERT_HEAD(&connman_services, cs, cs_link);
    } else {
      TAILQ_INSERT_AFTER(&connman_services, after, cs, cs_link);
    }

    connman_service_t *next = TAILQ_NEXT(cs, cs_link);

    prop_move(cs->cs_prop, next ? next->cs_prop : NULL);
  }

  // Update metadata

  prop_set_from_vardict(v, prop_create(cs->cs_prop, "metadata"));

  GVariant *name = g_variant_lookup_value(v, "Name", NULL);
  const gchar *val = name ? g_variant_get_string(name, NULL) : NULL;
  if(val)
    mystrset(&cs->cs_name, val);
  return cs;
}


/**
 *
 */
static void
services_changed(GVariant *params)
{
  connman_service_t *prev = NULL, *cs;

  GVariant *del = g_variant_get_child_value(params, 1);

  for(int i = 0, n =  g_variant_n_children(del); i < n; i++) {
    GVariant *v = g_variant_get_child_value(del, i);
    if(g_variant_type_equal(g_variant_get_type(v),
			    G_VARIANT_TYPE_OBJECT_PATH)) {
      const char *name = g_variant_get_string(v, NULL);
      TRACE(TRACE_DEBUG, "CONNMAN", "Deleted network %s", name);

      if((cs = connman_service_find(name)) != NULL)
	connman_service_destroy(cs);

    }
  }

  GVariant *add = g_variant_get_child_value(params, 0);
  for(int i = 0, n =  g_variant_n_children(add); i < n; i++) {
    GVariant *v = g_variant_get_child_value(add, i);
    const char *id =
      g_variant_get_string(g_variant_get_child_value(v, 0), NULL);

    cs = update_service(g_variant_get_child_value(v, 1), id, prev);
    if(cs != NULL)
      prev = cs;
  }
}


/**
 *
 */
static void
connman_mgr_signal(GDBusProxy *proxy,
		   gchar      *sender_name,
		   gchar      *signal_name,
		   GVariant   *parameters,
		   gpointer    user_data)
{
  TRACE(TRACE_DEBUG, "CONNMAN", "Manager-signal %s from %s",
	signal_name, sender_name);
  TRACE(TRACE_DEBUG, "CONNMAN", "%s", g_variant_print(parameters, TRUE));

  if(!strcmp(signal_name, "ServicesChanged")) {
    services_changed(parameters);
  }
}


/**
 *
 */
static void
connman_getservices(GDBusProxy *connman)
{
  GError *err = NULL;


  GVariant *v = g_dbus_proxy_call_sync(connman, "GetServices", NULL,
				       G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

  if(v == NULL) {
    TRACE(TRACE_ERROR, "CONNMAN", "Unable to GetServices -- %s",
	  err->message);
    g_error_free(err);
    return;
  }


  GVariant *list = g_variant_get_child_value(v, 0);
  connman_service_t *prev = NULL, *cs;
  if(list != NULL) {
    prop_destroy_childs(service_nodes);

    int num_services = g_variant_n_children(list);

    for(int i = 0; i < num_services; i++) {
      GVariant *svc = g_variant_get_child_value(list, i);
      const char *path =
	g_variant_get_string(g_variant_get_child_value(svc, 0), NULL);

      cs = update_service(g_variant_get_child_value(svc, 1), path, prev);
      if(cs != NULL)
	prev = cs;
    }
  }
  g_variant_unref(v);
}


/**
 *
 */
static void
connman_gettechnologies(GDBusProxy *connman)
{
  GError *err = NULL;


  GVariant *v = g_dbus_proxy_call_sync(connman, "GetTechnologies", NULL,
				       G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

  if(v == NULL) {
    TRACE(TRACE_ERROR, "CONNMAN", "Unable to GetTechnologies -- %s",
	  err->message);
    g_error_free(err);
    return;
  }

  TRACE(TRACE_DEBUG, "CONNMAN", "Technologies: %s",
	g_variant_print(v, TRUE));

  GVariant *list = g_variant_get_child_value(v, 0);
  if(list != NULL) {
    int num_tech = g_variant_n_children(list);

    for(int i = 0; i < num_tech; i++) {
      GVariant *tech = g_variant_get_child_value(list, i);

      const char *path =
	g_variant_get_string(g_variant_get_child_value(tech, 0), NULL);

      if(!strcmp(path, "/net/connman/technology/wifi"))
	have_wifi = 1;
    }
  }

  g_variant_unref(v);
}



/**
 *
 */
static void
connman_getpropreties(GDBusProxy *connman)
{
  GError *err = NULL;


  GVariant *v = g_dbus_proxy_call_sync(connman, "GetProperties", NULL,
				       G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

  if(v == NULL) {
    TRACE(TRACE_ERROR, "CONNMAN", "Unable to GetProperties -- %s",
	  err->message);
    g_error_free(err);
    return;
  }

  GVariant *dict = g_variant_get_child_value(v, 0);
  if(dict != NULL) {
    GVariant *state = g_variant_lookup_value(dict, "State", NULL);
    if(state != NULL) {
      const gchar *val = g_variant_get_string(state, NULL);
      prop_set_string(net_state, val);
    }
  }
  g_variant_unref(v);
}


/**
 *
 */
static connman_service_t *
connman_service_from_params(GVariant *param)
{
  const char *path =
    g_variant_get_string(g_variant_get_child_value(param, 0), NULL);

  connman_service_t *cs = path ? connman_service_find(path) : NULL;
  return cs;
}

/**
 *
 */
static void
handle_method_call(GDBusConnection *connection, const gchar *sender,
		   const gchar *object_path, const gchar *interface_name,
		   const gchar *method_name, GVariant *param,
		   GDBusMethodInvocation *inv, gpointer user_data)
{
  TRACE(TRACE_DEBUG, "CONNMAN", "agent method call: %s", method_name);

  if(!strcmp(method_name, "ReportError")) {
    connman_service_t *cs = connman_service_from_params(param);

    const char *msg =
      g_variant_get_string(g_variant_get_child_value(param, 1), NULL);

    notify_add(NULL, NOTIFY_ERROR, NULL, 3,
	       rstr_alloc("%s\n%s"), cs ? cs->cs_name : "Unknown network", msg);
    g_dbus_method_invocation_return_value(inv, NULL);

  } else if(!strcmp(method_name, "RequestInput")) {
    connman_service_t *cs = connman_service_from_params(param);

    if(cs == NULL) {
      g_dbus_method_invocation_return_error_literal(inv,
						    G_DBUS_ERROR,
						    G_DBUS_ERROR_INVALID_ARGS,
						    "Unknown service");
      return;
    }

    agent_request_input(cs, g_variant_get_child_value(param, 1), inv);

  } else {
    g_dbus_method_invocation_return_error(inv,
					  G_DBUS_ERROR,
					  G_DBUS_ERROR_INVALID_ARGS,
					  "Unknown method %s",
					  method_name);
    return;
  }
}


/**
 *
 */
static const GDBusInterfaceVTable connman_agent_vtable = {
  handle_method_call,
  NULL,
  NULL,
};

/**
 *
 */
static void
on_bus_acquired(GDBusConnection *connection, const gchar *name,
		gpointer user_data)
{
  GDBusProxy *connman = user_data;
  GError *err = NULL;

  GDBusNodeInfo *node_info =
    g_dbus_node_info_new_for_xml(connman_agent_xml, NULL);

  g_dbus_connection_register_object(connection,
				    "/showtime/netagent",
				    node_info->interfaces[0],
				    &connman_agent_vtable,
				    NULL, NULL, NULL);

  GVariant *params;

  params = g_variant_new("(o)", "/showtime/netagent");

  GVariant *v =
    g_dbus_proxy_call_sync(connman, "RegisterAgent", params,
			   G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
  if(v == NULL) {
    TRACE(TRACE_ERROR, "CONNMAN", "Unable to register agent -- %s",
	  err->message);
    g_error_free(err);
    return;
  }
  g_variant_unref(v);
}


/**
 *
 */
static void
on_name_acquired(GDBusConnection *connection, const gchar *name,
		 gpointer user_data)
{
}


/**
 *
 */
static void
on_name_lost(GDBusConnection *connection, const gchar *name,
	     gpointer user_data)
{
}


/**
 * Lots of hardcoded crap here
 */
static void
set_wifi_enable(void *opaque, int enable)
{
  const char *path = "/net/connman/technology/wifi";
  GError *err = NULL;
  GDBusProxy *proxy =
    g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
				  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START ,
				  NULL,
				  "net.connman",
				  path,
				  "net.connman.Technology",
				  NULL,
				  &err);

  if(proxy == NULL) {
    TRACE(TRACE_ERROR, "CONNMAN", "Unable to connect to technology %s -- %s",
	  path, err->message);
    g_error_free(err);
    return;
  }

  GVariant *params;
  params = g_variant_new("(sv)", "Powered",
			 g_variant_new_boolean(enable));

  GVariant *v =
    g_dbus_proxy_call_sync(proxy, "SetProperty", params,
			   G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

  if(v == NULL) {
    if(err->code != 36)
      TRACE(TRACE_ERROR, "CONNMAN", "Unable to set power %s -- %s",
            path, err->message);
    g_error_free(err);
  } else {
    g_variant_unref(v);
  }
  g_object_unref(G_OBJECT(proxy));
}


/**
 *
 */
static void *
connman_thread(void *aux)
{
  GError *err;
  GDBusProxy *mgr;
  GMainContext *ctx = g_main_context_new();
  GMainLoop *mainloop = g_main_loop_new(ctx, FALSE);
  connman_courier = glib_courier_create(ctx);

  g_main_context_push_thread_default(ctx);

 again:

  err = NULL;

  mgr = g_dbus_proxy_new_for_bus_sync(G_BUS_TYPE_SYSTEM,
				      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START ,
				      NULL,
				      "net.connman",
				      "/",
				      "net.connman.Manager",
				      NULL,
				      &err);
  if(mgr == NULL) {
    TRACE(TRACE_ERROR, "CONNMAN", "Unable to connect to connman -- %s",
	  err->message);
    g_error_free(err);
    sleep(5);
    goto again;
  }

  g_signal_connect(G_OBJECT(mgr), "g-signal",
		   G_CALLBACK(connman_mgr_signal), NULL);


  g_bus_own_name(G_BUS_TYPE_SYSTEM,
		 "com.showtimemediacenter.showtime.network.agent",
		 G_BUS_NAME_OWNER_FLAGS_NONE,
		 on_bus_acquired,
		 on_name_acquired,
		 on_name_lost,
		 mgr,
		 NULL);

  connman_getpropreties(mgr);
  connman_getservices(mgr);
  connman_gettechnologies(mgr);

  if(have_wifi) {
    setting_create(SETTING_BOOL, connman_settings, SETTINGS_INITIAL_UPDATE,
		   SETTING_TITLE(_p("Enable Wi-Fi")),
		   SETTING_CALLBACK(set_wifi_enable, NULL),
		   SETTING_STORE("connman", "enable_wifi"),
		   NULL);
  }

  g_main_loop_run(mainloop);
  return NULL;
}


#define MYURL "settings:networkconnections"

/**
 *
 */
void
connman_init(void)
{
  TAILQ_INIT(&connman_services);

  netconf_model = prop_create_root(NULL);
  prop_concat_t *pc = prop_concat_create(prop_create(netconf_model, "nodes"));

  net_state     = prop_create(netconf_model, "status");
  prop_set(netconf_model, "type", PROP_SET_STRING, "directory");

  prop_t *m = prop_create(netconf_model, "metadata");
  prop_set(m, "title", PROP_SET_RSTRING, _("Network connections"));


  // service_nodes contains list of items we receive from connman
  service_nodes = prop_create_root(NULL);
  prop_concat_add_source(pc, service_nodes, NULL);

  // settings
  connman_settings = prop_create_root(NULL);

  prop_t *delim = prop_create_root(NULL);
  prop_set_string(prop_create(delim, "type"), "separator");
  prop_concat_add_source(pc, prop_create(connman_settings, "nodes"), delim);

  settings_add_url(gconf.settings_network,
		   _p("Network connections"), NULL, NULL, NULL, MYURL,
		   SETTINGS_FIRST);

  hts_thread_create_detached("connman", connman_thread, NULL,
			     THREAD_PRIO_BGTASK);
}



/**
 *
 */
static int
be_netconf_canhandle(const char *url)
{
  if(!strcmp(url, MYURL))
    return 20;
  return 0;
}


/**
 *
 */
static int
be_netconf_open(prop_t *page, const char *url0, int sync)
{
  prop_link(netconf_model, prop_create(page, "model"));
  return 0;
}


/**
 *
 */
static backend_t be_netconf = {
  .be_canhandle = be_netconf_canhandle,
  .be_open = be_netconf_open,
};

BE_REGISTER(netconf);
