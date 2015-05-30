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
#include "main.h"
#include "htsmsg/htsmsg.h"
#include "htsmsg/htsmsg_json.h"
#include "htsmsg/htsmsg_store.h"
#include "fileaccess/http_client.h"
#include "arch/arch.h"
#include "misc/str.h"
#include "usage.h"
#include "plugins.h"
#include "task.h"
#include "misc/callout.h"
#include "upgrade.h"

static hts_mutex_t usage_mutex;
static htsmsg_t *usage_counters;
static htsmsg_t *plugin_counters;
static callout_t usage_callout;

static void try_send(void *aux);

/**
 *
 */
static void
usage_early_init(void)
{
  hts_mutex_init(&usage_mutex);
  usage_counters  = htsmsg_create_map();
  plugin_counters = htsmsg_create_map();
}

INITME(INIT_GROUP_NET, usage_early_init, NULL);




static int start_sent;
static int send_fails;
static int64_t session_start_time;

static int
sendmsg(htsmsg_t *msg)
{
  //  htsmsg_print("msg", msg);
  htsmsg_t *list = htsmsg_create_list();
  htsmsg_add_msg(list, NULL, msg);

  htsbuf_queue_t hq;
  htsbuf_queue_init(&hq, 0);

  htsbuf_append(&hq, "requests=", 9);

  char *json = htsmsg_json_serialize_to_str(list, 0);
  htsbuf_append_and_escape_url(&hq, json);
  free(json);

  int err = http_req("http://analytics1.movian.tv/i/bulk",
                     HTTP_POSTDATA(&hq, "application/x-www-form-urlencoded"),
                     HTTP_FLAGS(FA_NO_DEBUG),
                     NULL);

  htsmsg_release(list);
  return err;
}


/**
 *
 */
static void
add_events(htsmsg_t *m)
{
  hts_mutex_lock(&usage_mutex);

  htsmsg_field_t *f, *f2;
  htsmsg_t *events = NULL;

  HTSMSG_FOREACH(f, usage_counters) {
    if(f->hmf_type != HMF_S64)
      continue;
    if(events == NULL)
      events = htsmsg_create_list();
    htsmsg_t *s = htsmsg_create_map();
    htsmsg_add_str(s, "key", f->hmf_name);
    htsmsg_add_s64(s, "count", f->hmf_s64);
    htsmsg_add_msg(events, NULL, s);
  }

  htsmsg_release(usage_counters);
  usage_counters  = htsmsg_create_map();

  HTSMSG_FOREACH(f, plugin_counters) {
    htsmsg_t *pm = f->hmf_childs;
    if(pm == NULL)
      continue;
    HTSMSG_FOREACH(f2, pm) {
      if(f2->hmf_type != HMF_S64)
        continue;
      if(events == NULL)
        events = htsmsg_create_list();
      htsmsg_t *s = htsmsg_create_map();
      htsmsg_add_str(s, "key", f2->hmf_name);
      htsmsg_add_s64(s, "count", f2->hmf_s64);
      htsmsg_t *seg = htsmsg_create_map();
      htsmsg_add_str(seg, "plugin", f->hmf_name);
      htsmsg_add_msg(s, "segmentation", seg);
      htsmsg_add_msg(events, NULL, s);
    }
  }

  htsmsg_release(plugin_counters);
  plugin_counters = htsmsg_create_map();

  hts_mutex_unlock(&usage_mutex);

  if(events != NULL)
    htsmsg_add_msg(m, "events", events);

}


static void
usage_periodic(struct callout *c, void *aux)
{
  task_run(try_send, NULL);
}


/**
 *
 */
static void
try_send(void *aux)
{
  htsmsg_t *m = htsmsg_create_map();

  htsmsg_add_str(m, "device_id", gconf.device_id);
  htsmsg_add_str(m, "app_key", "3c19a63561f24198533e8372ef4865d54fd185a0");

  if(start_sent == 0) {
    htsmsg_add_u32(m, "begin_session", 1);

    htsmsg_t *metrics = htsmsg_create_map();
    extern const char *htsversion_full;

    htsmsg_add_str(metrics, "_os", arch_get_system_type());

    if(gconf.os_info[0])
      htsmsg_add_str(metrics, "_os_version" , gconf.os_info);

    if(gconf.device_type[0])
      htsmsg_add_str(metrics, "_device" , gconf.device_type);

    char *track = upgrade_get_track();
    if(track != NULL)
      htsmsg_add_str(metrics, "_carrier", track);
    else
      htsmsg_add_str(metrics, "_carrier", "none");

    htsmsg_add_str(metrics, "_app_version", htsversion_full);
    htsmsg_add_str(metrics, "_locale", gconf.lang);

    htsmsg_add_msg(m, "metrics", metrics);
    session_start_time = arch_get_ts();
  } else {
    int duration = (arch_get_ts() - session_start_time) / 1000000;
    htsmsg_add_u32(m, "session_duration", duration);
  }

  add_events(m);

  int err = sendmsg(m);
  if(err == 0) {
    start_sent = 1;
    callout_arm(&usage_callout, usage_periodic, NULL, 60 * 5);
    send_fails = 0;
  } else {
    send_fails++;
    if(send_fails == 10)
      return; // Give up
    callout_arm(&usage_callout, usage_periodic, NULL, 10 + send_fails * 2);
  }
}


/**
 *
 */
static void
usage_init(void)
{
}


/**
 *
 */
void
usage_start(void)
{
  if(gconf.disable_analytics || gconf.device_id[0] == 0)
    return;
  task_run(try_send, NULL);
}


/**
 *
 */
void
usage_inc_counter(const char *id, int value)
{
  hts_mutex_lock(&usage_mutex);
  htsmsg_s32_inc(usage_counters, id, value);
  hts_mutex_unlock(&usage_mutex);
}


/**
 *
 */
void
usage_inc_plugin_counter(const char *plugin, const char *id, int value)
{
  hts_mutex_lock(&usage_mutex);
  htsmsg_t *m = htsmsg_get_map(plugin_counters, plugin);
  if(m == NULL) {
    m = htsmsg_create_map();
    htsmsg_add_msg(plugin_counters, plugin, m);
    m = htsmsg_get_map(plugin_counters, plugin);
    assert(m != NULL);
  }

  htsmsg_s32_inc(m, id, value);
  hts_mutex_unlock(&usage_mutex);
}


/**
 *
 */
static void
usage_fini(void)
{
  callout_disarm(&usage_callout);
  if(!start_sent)
    return;

  htsmsg_t *m = htsmsg_create_map();

  htsmsg_add_str(m, "device_id", gconf.device_id);
  htsmsg_add_str(m, "app_key", "3c19a63561f24198533e8372ef4865d54fd185a0");
  htsmsg_add_u32(m, "end_session", 1);
  add_events(m);
  sendmsg(m);
}


INITME(INIT_GROUP_API, usage_init, usage_fini);
