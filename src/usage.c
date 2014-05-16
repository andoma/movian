#include "showtime.h"
#include "htsmsg/htsmsg.h"
#include "htsmsg/htsmsg_json.h"
#include "htsmsg/htsmsg_store.h"
#include "fileaccess/fileaccess.h"
#include "arch/arch.h"
#include "misc/str.h"
#include "usage.h"
#include "plugins.h"

static hts_mutex_t usage_mutex;
static htsmsg_t *usage_counters;
static htsmsg_t *plugin_counters;
static int usage_time_base;


/**
 *
 */
void
usage_init(void)
{
  hts_mutex_init(&usage_mutex);
  usage_time_base = showtime_get_ts() / 1000000LL;

  usage_counters  = htsmsg_create_map();
  plugin_counters = htsmsg_create_map();
}


/**
 *
 */
static htsmsg_t *
make_usage_report(void)
{
  extern const char *htsversion_full;

  htsmsg_t *out = htsmsg_create_map();

  htsmsg_add_str(out, "deviceid", gconf.device_id);
  htsmsg_add_str(out, "version", htsversion_full);
  htsmsg_add_str(out, "arch", showtime_get_system_type());
  htsmsg_add_u32(out, "verint", showtime_get_version_int());
  htsmsg_add_u32(out, "generated", time(NULL));

  time_t now = showtime_get_ts() / 1000000LL;

  int runtime = now - usage_time_base;
  htsmsg_s32_inc(usage_counters, "runtime", runtime);
  usage_time_base = now;

  htsmsg_add_msg(out, "counters", usage_counters);
  usage_counters = htsmsg_create_map();

  htsmsg_add_msg(out, "plugincounters", plugin_counters);
  plugin_counters = htsmsg_create_map();

  return out;
}


/**
 *
 */
void
usage_fini(void)
{
  if(gconf.device_id[0] == 0)
    return;

  hts_mutex_lock(&usage_mutex);

  int runtime = showtime_get_ts() / 1000000LL - usage_time_base;
  htsmsg_s32_inc(usage_counters, "runtime", runtime);

  htsmsg_t *r = make_usage_report();

  hts_mutex_unlock(&usage_mutex);

  htsmsg_store_save(r, "usage");
}


/**
 *
 */
static void *
send_report(void *aux)
{
  htsmsg_t *m = aux;
  htsbuf_queue_t hq;

  htsbuf_queue_init(&hq, 0);

  htsmsg_add_msg(m, "plugins", plugins_get_installed_list());

  htsmsg_json_serialize(m, &hq, 0);

  http_req("https://showtimemediacenter.com/showtime/status/v1/usage",
           HTTP_POSTDATA(&hq, "application/json"),
           NULL);

  htsbuf_queue_flush(&hq);

  htsmsg_release(m);
  return NULL;
}



/**
 *
 */
void
usage_report_send(int stored)
{
  if(gconf.device_id[0] == 0)
    return;

  htsmsg_t *m;
  if(stored) {
    m = htsmsg_store_load("usage");
    if(m == NULL)
      return;

    htsmsg_store_remove("usage");

    // Legacy cleanup, remove some day
    htsmsg_store_remove("usagecounters");
    htsmsg_store_remove("plugincounters");

  } else {

    hts_mutex_lock(&usage_mutex);
    m = make_usage_report();
    hts_mutex_unlock(&usage_mutex);
  }

  hts_thread_create_detached("report", send_report, m, THREAD_PRIO_BGTASK);
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
