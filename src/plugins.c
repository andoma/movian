/*
 *  Plugin mananger
 *  Copyright (C) 2010 Andreas Öman
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include "showtime.h"
#include "fileaccess/fileaccess.h"
#include "plugins.h"
#include "htsmsg/htsmsg_json.h"
#include "backend/backend.h"
#include "misc/string.h"

#if ENABLE_SPIDERMONKEY
#include "js/js.h"
#endif

#define PLUGIN_REPO_URL "http://localhost:8000/files/plugins-v1.json"

extern char *showtime_persistent_path;
static htsmsg_t *loaded_plugins;
static hts_mutex_t plugin_mutex;

/**
 *
 */
static htsmsg_t *
plugin_get_conf_by_id(const char *srch)
{
  return htsmsg_get_map(loaded_plugins, srch);
}


/**
 *
 */
static void
plugin_delete_by_id(const char *srch)
{
  htsmsg_delete_field(loaded_plugins, srch);
}



/**
 *
 */
static int
plugin_load_js(const char *id, const char *url,
	       char *errbuf, size_t errlen)
{
#if ENABLE_SPIDERMONKEY
  return js_plugin_load(id, url, errbuf, errlen);
#else
  snprintf(errbuf, errlen, "Unable to load %s -- Javscript not enabled", url);
  return -1;
#endif
}


/**
 *
 */
static int
plugin_load(const char *url, char *errbuf, size_t errlen, int force)
{
  char ctrlfile[URL_MAX];
  char *json;
  struct fa_stat fs;
  htsmsg_t *ctrl;

  snprintf(ctrlfile, sizeof(ctrlfile), "%s/plugin.json", url);

  if((json = fa_quickload(ctrlfile, &fs, NULL, errbuf, errlen)) == NULL)
    return -1;
  
  ctrl = htsmsg_json_deserialize(json);
  free(json);
  if(ctrl != NULL) {

    const char *type = htsmsg_get_str(ctrl, "type");
    const char *file = htsmsg_get_str(ctrl, "file");
    const char *id   = htsmsg_get_str(ctrl, "id");

    if(type == NULL) {
      snprintf(errbuf, errlen, "Missing \"type\" element in control file %s",
	    ctrlfile);
      htsmsg_destroy(ctrl);
      return -1;
    }

    if(file == NULL) {
      snprintf(errbuf, errlen, "Missing \"file\" element in control file %s",
	       ctrlfile);
      htsmsg_destroy(ctrl);
      return -1;
    }

    if(id == NULL) {
      snprintf(errbuf, errlen, "Missing \"id\" element in control file %s",
	    ctrlfile);
      htsmsg_destroy(ctrl);
      return -1;

    } else {

      if(!force && plugin_get_conf_by_id(id) != NULL) {
	snprintf(errbuf, errlen, "Plugin \"%s\" already loaded",
		 ctrlfile);
	htsmsg_destroy(ctrl);
	return -1;
      }

    }

    char fullpath[URL_MAX];
    int r;

    snprintf(fullpath, sizeof(fullpath), "%s/%s", url, file);
    
    if(!strcmp(type, "javascript")) {
      r = plugin_load_js(id, fullpath, errbuf, errlen);
    } else {
      snprintf(errbuf, errlen, "Unknown type \"%s\" in control file %s",
	       type, ctrlfile);
      r = -1;
    }

    if(!r) {
      htsmsg_delete_field(loaded_plugins, id);
      htsmsg_add_msg(loaded_plugins, id, ctrl);
    } else {
      htsmsg_destroy(ctrl);
    } 
    return r;

  } else {

    snprintf(errbuf, errlen, "Unable parse JSON control file %s",
	     ctrlfile);
    return -1;
  }
}


/**
 *
 */
static void *
plugin_init_thread(void *aux)
{
  char path[200];
  char errbuf[200];
  fa_dir_entry_t *fde;

  snprintf(path, sizeof(path), "file://%s/plugins", showtime_persistent_path);

  fa_dir_t *fd = fa_scandir(path, NULL, 0);

  if(fd != NULL) {
    TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
      snprintf(path, sizeof(path), "zip://%s", fde->fde_url);
      if(plugin_load(path, errbuf, sizeof(errbuf), 0)) {
	TRACE(TRACE_ERROR, "plugins", "Unable to load %s -- %s", path, errbuf);
      }
    }
    fa_dir_free(fd);
  }
  hts_mutex_unlock(&plugin_mutex);
  return NULL;
}


/**
 *
 */
void
plugins_init(const char *loadme)
{
  hts_mutex_init(&plugin_mutex);

  loaded_plugins = htsmsg_create_map();

  hts_mutex_lock(&plugin_mutex);

  if(loadme != NULL) {
    char errbuf[200];
    if(plugin_load(loadme, errbuf, sizeof(errbuf), 0)) {
      TRACE(TRACE_ERROR, "plugins", "Unable to load %s -- %s", loadme, errbuf);
    }
  }

  /*
   * Scanning and loaded installed plugins can be a bit slow, and we
   * don't like slow, so let's do that in a different thread
   */
  hts_thread_create_detached("plugininit", plugin_init_thread, NULL,
			     THREAD_PRIO_LOW);
}


/**
 *
 */
static int
plugin_canhandle(const char *url)
{
  return !strncmp(url, "plugin:", strlen("plugin:"));
}


/**
 *
 */
static const char *
plugin_installed_version(const char *srch)
{
  htsmsg_t *pm = plugin_get_conf_by_id(srch);

  if(pm == NULL)
    return NULL;
  return htsmsg_get_str(pm, "version") ?: "Unknown";
}


/**
 *
 */
static htsmsg_t *
repo_get(char *errbuf, size_t errlen)
{
  char *result;
  static htsmsg_t *repository;
  htsmsg_t *json;

  hts_mutex_lock(&plugin_mutex);
  
  if(repository != NULL) {
    hts_mutex_unlock(&plugin_mutex);
    return repository;
  }

  if(http_request(PLUGIN_REPO_URL, NULL, &result, NULL, errbuf, errlen,
		  NULL, NULL, 0, NULL, NULL, NULL)) {
  bad:
    hts_mutex_unlock(&plugin_mutex);
    return NULL;
  }
  
  json = htsmsg_json_deserialize(result);
  free(result);

  if(json == NULL) {
    snprintf(errbuf, errlen, "Malformed JSON in repository");
    goto bad;
  }

  const int ver = htsmsg_get_u32_or_default(json, "version", 0);

  if(ver != 1) {
    snprintf(errbuf, errlen, "Unsupported repository version %d", ver);
    goto bad;
  }
  
  repository = htsmsg_get_list(json, "plugins");
  if(repository == NULL) {
    snprintf(errbuf, errlen, "Missing plugin list in repository");
    goto bad;
  }

  hts_mutex_unlock(&plugin_mutex);
  return repository;
}


/**
 *
 */
static prop_t *
plugin_prop_from_htsmsg(htsmsg_t *pm)
{
  char url[256];
  const char *id = htsmsg_get_str(pm, "id");
  const char *title = htsmsg_get_str(pm, "title");
  const char *icon = htsmsg_get_str(pm, "icon");

  if(id == NULL || title == NULL)
    return NULL;

  snprintf(url, sizeof(url), "plugin:repo:%s", id);

  prop_t *p = prop_create_root(NULL);
  prop_t *metadata = prop_create(p, "metadata");

  prop_set_string(prop_create(p, "type"), "plugin");
  prop_set_string(prop_create(p, "url"), url);

  prop_set_string(prop_create(metadata, "title"), title);

  if(icon != NULL) {
    char *iconurl = url_resolve_relative_from_base(PLUGIN_REPO_URL, icon);
    prop_set_string(prop_create(metadata, "icon"), iconurl);
    free(iconurl);
  }

  return p;
}


/**
 *
 */
static void
plugin_open_repo(prop_t *page)
{
  char errbuf[200];
  htsmsg_t  *repo;
  
  if((repo = repo_get(errbuf, sizeof(errbuf))) == NULL) {
    nav_open_errorf(page, "Unable to request plugin repository: %s", 
		    errbuf);
    return;
  }

  prop_t *model = prop_create(page, "model");
  prop_set_string(prop_create(model, "type"), "directory");
  prop_set_string(prop_create(model, "contents"), "items");

  prop_t *metadata = prop_create(model, "metadata");
  
  prop_set_string(prop_create(metadata, "title"),
		  "Available plugins");

  prop_t *nodes = prop_create(model, "nodes");

  htsmsg_field_t *f;
  HTSMSG_FOREACH(f, repo) {
    htsmsg_t *pm =  htsmsg_get_map_by_field(f);
    if(pm == NULL)
      continue;

    prop_t *p = plugin_prop_from_htsmsg(pm);
    if(p == NULL)
      continue;

    if(prop_set_parent(p, nodes))
      prop_destroy(p);
  }
}


/**
 *
 */
static htsmsg_t *
get_item_by_id(htsmsg_t *repo, const char *id)
{
  htsmsg_field_t *f;
  HTSMSG_FOREACH(f, repo) {
    htsmsg_t *pm =  htsmsg_get_map_by_field(f);
    if(pm == NULL)
      continue;
    if(!strcmp(id, htsmsg_get_str(pm, "id") ?: ""))
      return pm;
  }
  return NULL;
}


/**
 *
 */
static void
add_item_node(prop_t *parent, const char *type, const char *id,
	      const char *title, const char *data, prop_t **datap)
{
  prop_t *d, *p = prop_create_root(NULL);

  prop_set_string(prop_create(p, "id"), id);
  prop_set_string(prop_create(p, "type"), type);
  prop_set_string(prop_create(p, "title"), title);

  d = prop_create(p, "data");
  if(datap != NULL)
    *datap = prop_ref_inc(d);

  prop_set_string(d, data);

  if(prop_set_parent(p, parent))
    prop_destroy(p);
}


/**
 *
 */
static prop_t *
add_action_node(prop_t *parent, const char *type, const char *id,
		const char *title, const char *data, int enabled)
{
  prop_t *p = prop_create_root(NULL);

  prop_set_string(prop_create(p, "id"), id);
  prop_set_string(prop_create(p, "type"), type);
  prop_set_string(prop_create(p, "title"), title);
  prop_set_string(prop_create(p, "data"), data);

  prop_t *en = prop_ref_inc(prop_create(p, "enabled"));
  prop_set_int(en, enabled);

  if(prop_set_parent(p, parent))
    prop_destroy(p);

  return en;
}


/**
 *
 */
typedef struct {
  char *pid_id;

  enum {
    PLUGIN_AVAILABLE,
    PLUGIN_INSTALLED_CURRENT,
    PLUGIN_INSTALLED_UPGRADABLE,
  } pid_status;

  htsmsg_t *pid_msg;
  prop_sub_t *pid_eventsub;
  prop_t *pid_statustxt;
  prop_t *pid_canInstall;
  prop_t *pid_canUninstall;
  prop_t *pid_canUpgrade;
  prop_t *pid_installedversion;
  int pid_running;
  prop_courier_t *pid_pc;
} plugin_item_data_t;


/**
 *
e */
static void
plugin_install(plugin_item_data_t *pid)
{
  const char *dlurl0;
  char errbuf[200];
  char path[200];

  dlurl0 = htsmsg_get_str(pid->pid_msg, "downloadURL");

  if(dlurl0 == NULL) {
    prop_set_string(pid->pid_statustxt, "Can't generate download URL");
    return;
  }

  char *url = url_resolve_relative_from_base(PLUGIN_REPO_URL, dlurl0);

  TRACE(TRACE_INFO, "plugins", "Downloading plugin %s from %s",
	pid->pid_id, url);

  prop_set_int(pid->pid_canInstall, 0);
  prop_set_int(pid->pid_canUpgrade, 0);
  prop_set_string(pid->pid_statustxt, "Downloading");

  fa_stat_t fs;

  char *buf = fa_quickload(url, &fs, NULL, errbuf, sizeof(errbuf));

  if(buf == NULL) {
    prop_set_stringf(pid->pid_statustxt, errbuf);
    return;
  }

  if(fs.fs_size < 4 ||
     buf[0] != 0x50 || buf[1] != 0x4b || buf[2] != 0x03 || buf[3] != 0x04) {
    prop_set_stringf(pid->pid_statustxt, "Invalid plugin archive");
    return;
  }
  
  TRACE(TRACE_INFO, "plugins", "Plugin %s valid ZIP archive %d bytes",
	pid->pid_id, (int)fs.fs_size);
  prop_set_string(pid->pid_statustxt, "Installing");

  snprintf(path, sizeof(path), "%s/plugins", showtime_persistent_path);
  mkdir(path, 0770);

  snprintf(path, sizeof(path), "%s/plugins/%s.zip", showtime_persistent_path,
	   pid->pid_id);

  int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0660);
  if(fd == -1) {
    prop_set_string(pid->pid_statustxt, "File open error");
    TRACE(TRACE_ERROR, "plugins", "Unable to write to %s -- %s",
	  path, strerror(errno));
    free(buf);
    return;
  }

  size_t r = write(fd, buf, fs.fs_size);
  free(buf);
  if(close(fd) || r != fs.fs_size) {
    prop_set_string(pid->pid_statustxt, "Disk write error");
    TRACE(TRACE_ERROR, "plugins", "Unable to write to %s -- %s",
	  path, strerror(errno));
    return;
  }

  snprintf(path, sizeof(path),
	   "zip://file://%s/plugins/%s.zip", showtime_persistent_path,
	   pid->pid_id);

  hts_mutex_lock(&plugin_mutex);

  if(plugin_load(path, errbuf, sizeof(errbuf), 1)) {
    prop_set_string(pid->pid_statustxt, errbuf);
    TRACE(TRACE_ERROR, "plugins", "Unable to load %s -- %s", path, errbuf);
  } else {
    prop_set_string(pid->pid_statustxt, "Installed");
    prop_set_int(pid->pid_canUninstall, 1);
    prop_set_string(pid->pid_installedversion,
		    plugin_installed_version(pid->pid_id));
  }
  
  hts_mutex_unlock(&plugin_mutex);
}


/**
 *
 */
static void
plugin_remove(plugin_item_data_t *pid)
{
  char path[200];

  snprintf(path, sizeof(path), "%s/plugins/%s.zip", showtime_persistent_path,
	   pid->pid_id);
  unlink(path);

#if ENABLE_SPIDERMONKEY
  js_plugin_unload(pid->pid_id);
#endif

  hts_mutex_lock(&plugin_mutex);
  plugin_delete_by_id(pid->pid_id);
  hts_mutex_unlock(&plugin_mutex);

  htsmsg_store_remove("plugins/%s", pid->pid_id);

  prop_set_int(pid->pid_canUninstall, 0);
  prop_set_int(pid->pid_canUpgrade, 0);
  prop_set_int(pid->pid_canInstall, 1);
  prop_set_string(pid->pid_installedversion, "Not installed");
  prop_set_string(pid->pid_statustxt, "Not installed");
}


/**
 *
 */
static void
plugin_item_action(plugin_item_data_t *pid, const char *str)
{
  if(!strcmp(str, "install") || !strcmp(str, "upgrade"))
    plugin_install(pid);
  if(!strcmp(str, "remove"))
    plugin_remove(pid);
}


/**
 *
 */
static void
plugin_item_event(plugin_item_data_t *pid, event_t *e)
{
  if(event_is_type(e, EVENT_ACTION_VECTOR)) {
    event_action_vector_t *eav = (event_action_vector_t *)e;
    int i;
    for(i = 0; i < eav->num; i++)
      plugin_item_action(pid, action_code2str(eav->actions[i]));
    
  } else if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
    plugin_item_action(pid, e->e_payload);
  }
}


/**
 *
 */
static void
plugin_item_sub(void *opaque, prop_event_t event, ...)
{
  plugin_item_data_t *pid = opaque;
  va_list ap;

  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    pid->pid_running = 0;
    break;

  case PROP_EXT_EVENT:
    plugin_item_event(pid, va_arg(ap, event_t *));
    break;

  default:
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void *
plugin_item_thread(void *arg)
{
  plugin_item_data_t *pid = arg;

  while(pid->pid_running)
    prop_courier_wait_and_dispatch(pid->pid_pc);

  prop_unsubscribe(pid->pid_eventsub);

  prop_courier_destroy(pid->pid_pc);

  prop_ref_dec(pid->pid_statustxt);
  prop_ref_dec(pid->pid_canInstall);
  prop_ref_dec(pid->pid_canUninstall);
  prop_ref_dec(pid->pid_canUpgrade);
  prop_ref_dec(pid->pid_installedversion);
  free(pid->pid_id);
  htsmsg_destroy(pid->pid_msg);
  free(pid);
  return NULL;
}


/**
 *
 */
static void
plugin_open_repo_item(prop_t *page, const char *id)
{
  char errbuf[200];
  htsmsg_t  *repo, *pm;
  
  if((repo = repo_get(errbuf, sizeof(errbuf))) == NULL) {
    nav_open_errorf(page, "Unable to request plugin repository: %s", errbuf);
    return;
  }

  if((pm = get_item_by_id(repo, id)) == NULL) {
    nav_open_errorf(page, "Plugin ID %s does not exist", id);
    return;
  }

  prop_set_int(prop_create(page, "directClose"), 1);

  plugin_item_data_t *pid = calloc(1, sizeof(plugin_item_data_t));

  pid->pid_id = strdup(id);
  pid->pid_msg = htsmsg_copy(pm);

  prop_t *model = prop_create(page, "model");
  prop_set_string(prop_create(model, "type"), "item");

  prop_t *metadata = prop_create(model, "metadata");
  
  prop_set_string(prop_create(metadata, "title"),
		  htsmsg_get_str(pm, "title"));

  const char *icon = htsmsg_get_str(pm, "icon");

  if(icon != NULL) {
    char *iconurl = url_resolve_relative_from_base(PLUGIN_REPO_URL, icon);
    prop_set_string(prop_create(metadata, "icon"), iconurl);
    free(iconurl);
  }
  
  prop_t *nodes = prop_create(model, "nodes");

  const char *s;

  if((s = htsmsg_get_str(pm, "description")) != NULL) {
    add_item_node(nodes, "string", "description", NULL, s, NULL); 
    add_item_node(nodes, "divider", NULL, NULL, NULL, NULL);
  }

  if((s = htsmsg_get_str(pm, "author")) != NULL)
    add_item_node(nodes, "titledstring", "author", "Author", s, NULL);


  add_item_node(nodes, "titledstring", "status", "Status", NULL,
		&pid->pid_statustxt);


  const char *repo_ver = htsmsg_get_str(pm, "version") ?: "Unknown";
  const char *inst_ver = plugin_installed_version(id);

  add_item_node(nodes, "titledstring", "version", "Available version",
		repo_ver, NULL);

  add_item_node(nodes, "titledstring", "version",
		"Installed version", inst_ver ?: "Not installed",
		&pid->pid_installedversion);


  if(inst_ver == NULL) {
    pid->pid_status = PLUGIN_AVAILABLE;
    prop_set_string(pid->pid_statustxt, "Not installed");
  } else if(!strcmp(inst_ver, repo_ver)) {
    pid->pid_status = PLUGIN_INSTALLED_CURRENT;
    prop_set_string(pid->pid_statustxt, "Up to date");
  } else {
    pid->pid_status = PLUGIN_INSTALLED_UPGRADABLE;
    prop_set_string(pid->pid_statustxt, "Upgradable");
  }

  nodes = prop_create(model, "actions");

  pid->pid_canInstall = 
    add_action_node(nodes, "pageevent", "install", "Install plugin", "install",
		    pid->pid_status == PLUGIN_AVAILABLE);

  pid->pid_canUpgrade = 
    add_action_node(nodes, "pageevent", "upgrade", "Upgrade plugin", "upgrade",
		    pid->pid_status == PLUGIN_INSTALLED_UPGRADABLE);

  pid->pid_canUninstall = 
    add_action_node(nodes, "pageevent", "remove", "Uninstall plugin", "remove",
		    pid->pid_status != PLUGIN_AVAILABLE);

  pid->pid_pc = prop_courier_create_waitable();
  pid->pid_running = 1;

  pid->pid_eventsub = prop_subscribe(PROP_SUB_TRACK_DESTROY,
				     PROP_TAG_NAME("page", "eventSink"),
				     PROP_TAG_CALLBACK, plugin_item_sub, pid,
				     PROP_TAG_NAMED_ROOT, page, "page",
				     PROP_TAG_COURIER, pid->pid_pc,
				     NULL);

  hts_thread_create_detached("pluginitem", plugin_item_thread, pid,
			     THREAD_PRIO_LOW);
}


/**
 *
 */
static int
plugin_open_url(prop_t *page, const char *url)
{
  const char *s;
  if(!strcmp(url, "plugin:repo")) {
    plugin_open_repo(page);
    return 0;
  }

  if((s = mystrbegins(url, "plugin:repo:")) != NULL) {
    plugin_open_repo_item(page, s);
    return 0;
  }

  nav_open_errorf(page, "Invalud URI");
  return 0;
}


/**
 *
 */
static backend_t be_plugin = {
  .be_canhandle = plugin_canhandle,
  .be_open = plugin_open_url,
};

BE_REGISTER(plugin);
