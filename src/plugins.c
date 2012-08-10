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
#include "settings.h"
#include "htsmsg/htsmsg_json.h"
#include "backend/backend.h"
#include "misc/string.h"
#include "prop/prop_nodefilter.h"
#include "prop/prop_concat.h"
#include "notifications.h"

#if ENABLE_SPIDERMONKEY
#include "js/js.h"
#endif


const char *plugin_repo_url = "http://showtime.lonelycoder.com/plugins/plugins-v1.json";
extern char *showtime_persistent_path;
static hts_mutex_t plugin_mutex;
static char *devplugin;

static prop_t *plugin_root_installed;
static prop_t *plugin_root_repo;

static prop_t *plugin_root_model;
static prop_courier_t *plugin_courier;



LIST_HEAD(plugin_list, plugin);

static struct plugin_list plugins;


typedef struct plugin {
  LIST_ENTRY(plugin) pl_link;
  char *pl_id;
  char *pl_package;
  char *pl_title;

  prop_t *pl_inst_prop;

  char *pl_inst_ver;
  char *pl_repo_ver;
  char *pl_showtime_min_version;

  int pl_loaded;
  int pl_installed;
  int pl_can_upgrade;

  prop_t *pl_status;
  prop_t *pl_statustxt;
  prop_t *pl_minver;

  prop_t *pl_canInstall;
  prop_t *pl_canUpgrade;
  prop_t *pl_cantUpgrade;
  prop_t *pl_canUninstall;
  prop_t *pl_prop_installed;


  int pl_new_version_avail;

} plugin_t;

static int plugin_install(plugin_t *pl, const char *package);
static void plugin_remove(plugin_t *pl);
static void plugin_autoupgrade(void);

static int autoupgrade;

/**
 *
 */
static void
set_autoupgrade(void *opaque, int value) 
{
  hts_mutex_lock(&plugin_mutex);
  autoupgrade = value;

  plugin_autoupgrade();

  hts_mutex_unlock(&plugin_mutex);
}

/**
 *
 */
static plugin_t *
plugin_find(const char *id)
{
  plugin_t *pl;
  LIST_FOREACH(pl, &plugins, pl_link)
    if(!strcmp(pl->pl_id, id))
      return pl;
  
  pl = calloc(1, sizeof(plugin_t));
  pl->pl_id = strdup(id);

  pl->pl_status = prop_create_root(NULL);
  pl->pl_statustxt = prop_create(pl->pl_status, "statustxt");
  pl->pl_minver = prop_create(pl->pl_status, "minver");
  pl->pl_canInstall   = prop_create(pl->pl_status, "canInstall");
  pl->pl_canUpgrade   = prop_create(pl->pl_status, "canUpgrade");
  pl->pl_cantUpgrade   = prop_create(pl->pl_status, "cantUpgrade");
  pl->pl_canUninstall = prop_create(pl->pl_status, "canUninstall");
  pl->pl_prop_installed = prop_create(pl->pl_status, "installed");

  LIST_INSERT_HEAD(&plugins, pl, pl_link);
  return pl;
}


/**
 *
 */
static void
update_global_state(void)
{
  plugin_t *pl;
  int num_upgradable = 0;
  LIST_FOREACH(pl, &plugins, pl_link)
    if(pl->pl_new_version_avail)
      num_upgradable++;

  prop_set(prop_get_global(), "plugins", "status", "upgradeable", NULL,
	   PROP_SET_INT, num_upgradable);
}


/**
 *
 */
static void
update_state(plugin_t *pl)
{
  int canInstall = 0;
  int canUninstall = 0;
  int canUpgrade = 0;
  int cantUpgrade = 0;
  rstr_t *status = NULL;

  int version_dep_ok = 
    pl->pl_showtime_min_version == NULL ||
    showtime_parse_version_int(pl->pl_showtime_min_version) <=
    showtime_get_version_int();

  prop_set_void(pl->pl_minver);
  pl->pl_new_version_avail = 0;

  if(pl->pl_installed == 0) {

    if(!version_dep_ok) {
      status = _("Not installable");
      prop_set_string(pl->pl_minver, pl->pl_showtime_min_version);

    } else {

      status = _("Not installed");
      canInstall = 1;
    }

  } else if(!strcmp(pl->pl_inst_ver ?: "", pl->pl_repo_ver ?: "")) {
    status = _("Up to date");
    canUninstall = 1;
  } else {
    canUninstall = 1;

    if(pl->pl_repo_ver != NULL) {
      pl->pl_new_version_avail = 1;

      if(!version_dep_ok) {
	status = _("Not upgradable");
	prop_set_string(pl->pl_minver, pl->pl_showtime_min_version);
	cantUpgrade = 1;
      } else {
	status = _("Upgradable");
	canUpgrade = 1;
      }
    }
  }

  pl->pl_can_upgrade = canUpgrade;
  prop_set_int(pl->pl_canInstall,   canInstall);
  prop_set_int(pl->pl_canUninstall, canUninstall);
  prop_set_int(pl->pl_canUpgrade,   canUpgrade);
  prop_set_int(pl->pl_cantUpgrade,  cantUpgrade);
  prop_set_int(pl->pl_prop_installed, pl->pl_installed);
  prop_set_rstring(pl->pl_statustxt, status);
  rstr_release(status);
}


/**
 *
 */
static void
plugin_event(void *opaque, prop_event_t event, ...)
{
  plugin_t *pl = opaque;
  va_list ap;
  prop_sub_t *s;
  event_t *e;
  prop_t *p;

  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    s = va_arg(ap, prop_sub_t *);
    prop_unsubscribe(s);
    break;

  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    p = va_arg(ap, prop_t *);

    if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
      if(!strcmp(e->e_payload, "install")) {
	rstr_t *package = prop_get_string(p, "package", NULL);
	plugin_install(pl, rstr_get(package));
	rstr_release(package);
      }
      else if(!strcmp(e->e_payload, "upgrade"))
	plugin_install(pl, NULL);
      else if(!strcmp(e->e_payload, "uninstall"))
	plugin_remove(pl);
    }
    break;
  }
}


/**
 *
 */
static void
plugin_fill_prop0(struct htsmsg *pm, struct prop *p,
		  const char *basepath, int as_installed,
		  plugin_t *pl)
{
  const char *title = htsmsg_get_str(pm, "title") ?: pl->pl_id;
  const char *icon  = htsmsg_get_str(pm, "icon");


  prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_SINGLETON,
		 PROP_TAG_CALLBACK, plugin_event, pl,
		 PROP_TAG_ROOT, p,
		 PROP_TAG_MUTEX, &plugin_mutex,
		 NULL);

  prop_t *metadata = prop_create(p, "metadata");

  prop_link(pl->pl_status, prop_create(p, "status"));

  prop_set_string(prop_create(metadata, "title"), title);

  prop_set_string_ex(prop_create(metadata, "description"),
		     NULL,
		     htsmsg_get_str(pm, "description"),
		     PROP_STR_RICH);

  prop_set_string(prop_create(metadata, "synopsis"),
		  htsmsg_get_str(pm, "synopsis"));

  prop_set_string(prop_create(metadata, "author"),
		  htsmsg_get_str(pm, "author"));

  prop_set_string(prop_create(metadata, "version"),
		  htsmsg_get_str(pm, "version"));

  if(as_installed) {

    prop_set_string(prop_create(metadata, "installed_version"),
		    htsmsg_get_str(pm, "version"));

    prop_link(prop_create(prop_create(prop_create(plugin_root_repo, pl->pl_id),
				      "metadata"), "version"),
	      prop_create(metadata, "available_version"));


  } else {

    prop_set_string(prop_create(metadata, "available_version"),
		    htsmsg_get_str(pm, "version"));

    prop_t *inst = prop_create(plugin_root_installed, pl->pl_id);
    prop_t *instm = prop_create(inst, "metadata");

    prop_link(prop_create(instm, "version"),
	      prop_create(metadata, "installed_version"));
  }

  if(icon != NULL) {
    if(basepath != NULL) {
      char url[512];
      snprintf(url, sizeof(url), "%s/%s", basepath, icon);
      prop_set_string(prop_create(metadata, "icon"), url);
    } else {
      char *iconurl = url_resolve_relative_from_base(plugin_repo_url, icon);
      prop_set_string(prop_create(metadata, "icon"), iconurl);
      free(iconurl);
    }
  }
}



/**
 *
 */
void
plugin_props_from_file(prop_t *prop, const char *zipfile)
{
  char path[200];
  char errbuf[200];
  char *buf;

  snprintf(path, sizeof(path), "zip://%s/plugin.json", zipfile);
  buf = fa_load(path, NULL, NULL, errbuf, sizeof(errbuf), NULL, 0, NULL, NULL);
  if(buf == NULL) {
    TRACE(TRACE_ERROR, "plugins", "Unable to open %s -- %s", path, errbuf);
    return;
  }
  htsmsg_t *pm = htsmsg_json_deserialize(buf);
  free(buf);

  if(pm == NULL)
    return;

  const char *id = htsmsg_get_str(pm, "id");

  if(id != NULL) {
    hts_mutex_lock(&plugin_mutex);
    plugin_t *pl = plugin_find(id);


    snprintf(path, sizeof(path), "zip://%s", zipfile);
    plugin_fill_prop0(pm, prop, path, 0, pl);
    prop_set(prop, "package", NULL, PROP_SET_STRING, zipfile);
    update_state(pl);
    hts_mutex_unlock(&plugin_mutex);
  }
  htsmsg_destroy(pm);

}


/**
 *
 */
static void
plugin_prop_setup(htsmsg_t *pm, plugin_t *pl, const char *basepath,
		  int as_installed)
{
  prop_t *p;

  hts_mutex_assert(&plugin_mutex);

  if(as_installed) {
    if(pl->pl_inst_prop == NULL)
      pl->pl_inst_prop = prop_create(plugin_root_installed, pl->pl_id);

    p = pl->pl_inst_prop;

  } else {

    p = prop_create(plugin_root_repo, pl->pl_id);
  }

  prop_set(p, "type", NULL, PROP_SET_STRING, "plugin");

  plugin_fill_prop0(pm, p, basepath, as_installed, pl);
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
plugin_load(const char *url, char *errbuf, size_t errlen, int force,
	    int as_installed)
{
  char ctrlfile[URL_MAX];
  char *json;
  htsmsg_t *ctrl;

  snprintf(ctrlfile, sizeof(ctrlfile), "%s/plugin.json", url);

  if((json = fa_load(ctrlfile, NULL, NULL, errbuf, errlen, NULL, 0,
		     NULL, NULL)) == NULL)
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
    }

    plugin_t *pl = plugin_find(id);

    if(!force && pl->pl_loaded) {
      snprintf(errbuf, errlen, "Plugin \"%s\" already loaded",
	       id);
      htsmsg_destroy(ctrl);
      return -1;
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

      if(as_installed) {
	plugin_prop_setup(ctrl, pl, url, 1);
	mystrset(&pl->pl_inst_ver, htsmsg_get_str(ctrl, "version"));
	pl->pl_installed = 1;
      }

      mystrset(&pl->pl_inst_ver, htsmsg_get_str(ctrl, "version"));
      mystrset(&pl->pl_title, htsmsg_get_str(ctrl, "title") ?: id);

      pl->pl_loaded = 1;
    }
    htsmsg_destroy(ctrl);
    update_state(pl);
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
static void
plugin_load_installed(void)
{
  char path[200];
  char errbuf[200];
  fa_dir_entry_t *fde;

  snprintf(path, sizeof(path), "file://%s/installedplugins",
	   showtime_persistent_path);

  fa_dir_t *fd = fa_scandir(path, NULL, 0);

  if(fd != NULL) {
    TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
      snprintf(path, sizeof(path), "zip://%s", rstr_get(fde->fde_url));
      if(plugin_load(path, errbuf, sizeof(errbuf), 0, 1)) {
	TRACE(TRACE_ERROR, "plugins", "Unable to load %s\n%s", path, errbuf);
      }
    }
    fa_dir_free(fd);
  }
}



/**
 *
 */
static htsmsg_t *
repo_get(const char *repo, char *errbuf, size_t errlen)
{
  char *result;
  htsmsg_t *json;

  result = fa_load(repo, NULL, NULL, errbuf, errlen, NULL, 0, NULL, NULL);
  if(result == NULL)
    return NULL;
  
  json = htsmsg_json_deserialize(result);
  free(result);

  if(json == NULL) {
    snprintf(errbuf, errlen, "Malformed JSON in repository");
    return NULL;
  }

  const int ver = htsmsg_get_u32_or_default(json, "version", 0);

  if(ver != 1) {
    snprintf(errbuf, errlen, "Unsupported repository version %d", ver);
    htsmsg_destroy(json);
    return NULL;
  }

  const char *msg = htsmsg_get_str(json, "message");
  if(msg != NULL) {
    snprintf(errbuf, errlen, "%s", msg);
    htsmsg_destroy(json);
    return NULL;
  }
  
  htsmsg_field_t *f = htsmsg_field_find(json, "plugins");
  if(f == NULL) {
    snprintf(errbuf, errlen, "Missing plugin list in repository");
    htsmsg_destroy(json);
    return NULL;
  }
  htsmsg_t *r = htsmsg_detach_submsg(f);
  htsmsg_destroy(json);

  return r;
}


/**
 *
 */
static void
plugins_load(void)
{
  char errbuf[512];
  const char *repo = plugin_repo_url;

  plugin_load_installed();
  
  htsmsg_t *r = repo_get(repo, errbuf, sizeof(errbuf));
  
  if(r != NULL) {
    htsmsg_field_t *f;

    HTSMSG_FOREACH(f, r) {
      htsmsg_t *pm;
      if((pm = htsmsg_get_map_by_field(f)) == NULL)
	continue;
      const char *id = htsmsg_get_str(pm, "id");
      if(id == NULL)
	continue;
      plugin_t *pl = plugin_find(id);
      plugin_prop_setup(pm, pl, NULL, 0);
      mystrset(&pl->pl_repo_ver, htsmsg_get_str(pm, "version"));
      mystrset(&pl->pl_showtime_min_version,
	       htsmsg_get_str(pm, "showtimeVersion"));
      update_state(pl);

      const char *dlurl = htsmsg_get_str(pm, "downloadURL");
      if(dlurl != NULL) {
	char *package = url_resolve_relative_from_base(repo, dlurl);
	free(pl->pl_package);
	pl->pl_package = package;
      }
    }

    htsmsg_destroy(r);

  } else {
    TRACE(TRACE_ERROR, "plugins", "Unable to load repo %s -- %s",
	  repo, errbuf);
  }
  update_global_state();
  plugin_autoupgrade();
}


/**
 *
 */
static void
plugin_autoupgrade(void)
{
  plugin_t *pl;

  if(!autoupgrade)
    return;

  LIST_FOREACH(pl, &plugins, pl_link) {
    if(!pl->pl_can_upgrade)
      continue;
    if(plugin_install(pl, NULL))
      continue;
    notify_add(NULL, NOTIFY_INFO, NULL, 5, 
	       _("Upgraded plugin %s to version %s"), pl->pl_title,
	       pl->pl_inst_ver);
  }
  update_global_state();
}

/**
 *
 */
static void
plugins_setup_root_props(void)
{
  prop_concat_t *pc;
  struct prop_nf *pnf;
  prop_t *d, *p;
  htsmsg_t *store;

  if((store = htsmsg_store_load("pluginconf")) == NULL)
    store = htsmsg_create_map();

  plugin_root_installed = prop_create_root(NULL);
  plugin_root_repo = prop_create_root(NULL);
  plugin_root_model = prop_create_root(NULL);

  prop_set_string(prop_create(plugin_root_model, "type"), "directory");

  prop_link(_p("Plugins"),
	    prop_create(prop_create(plugin_root_model, "metadata"), "title"));

  pc = prop_concat_create(prop_create(plugin_root_model, "nodes"), 0);

  // Top items

  prop_t *sta = prop_create_root(NULL);
  
  p = prop_create(sta, NULL);
  prop_set_string(prop_create(p, "type"), "directory");
  prop_link(_p("Browse available plugins"),
	    prop_create(prop_create(p, "metadata"), "title"));
  prop_set_string(prop_create(p, "url"), "plugin:repo");

  settings_create_bool(sta, "autoupgrade", _p("Automatically upgrade plugins"),
		       1, store, set_autoupgrade, NULL,
		       SETTINGS_RAW_NODES | SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"pluginconf");


  prop_concat_add_source(pc, sta, NULL);

  // Installed plugins

  prop_t *inst = prop_create_root(NULL);
  pnf = prop_nf_create(inst, plugin_root_installed, NULL, PROP_NF_AUTODESTROY);
  prop_nf_sort(pnf, "node.metadata.title", 0, 0, NULL, 1);


  d = prop_create_root(NULL);
  prop_link(_p("Installed plugins"),
	    prop_create(prop_create(d, "metadata"), "title"));
  prop_set_string(prop_create(d, "type"), "divider");
  prop_concat_add_source(pc, inst, d);
}


/**
 *
 */
static void *
plugin_thread(void *aux)
{
  hts_mutex_lock(&plugin_mutex);
  plugins_load();
  hts_mutex_unlock(&plugin_mutex);
  return NULL;
}

/**
 *
 */
void
plugins_init(const char *loadme, const char *repo)
{
  if(repo)
     plugin_repo_url = repo;
  hts_mutex_init(&plugin_mutex);
  plugin_courier = prop_courier_create_waitable();

  plugins_setup_root_props();


  hts_mutex_lock(&plugin_mutex);

  if(loadme != NULL) {
    char errbuf[200];
    devplugin = strdup(loadme);
    if(plugin_load(loadme, errbuf, sizeof(errbuf), 1, 0)) {
      TRACE(TRACE_ERROR, "plugins",
	    "Unable to load development plugin: %s\n%s", loadme, errbuf);
    } else {
      TRACE(TRACE_INFO, "plugins", "Loaded dev plugin %s", devplugin);
    }
    plugins_load();
  } else {
    hts_thread_create_detached("pluginsinit", plugin_thread, NULL,
			       THREAD_PRIO_LOW);
  }

  hts_mutex_unlock(&plugin_mutex);
}


/**
 *
 */
void
plugins_reload_dev_plugin(void)
{
  char errbuf[200];
  if(devplugin == NULL)
    return;

  hts_mutex_lock(&plugin_mutex);

  if(plugin_load(devplugin, errbuf, sizeof(errbuf), 1, 0))
    TRACE(TRACE_ERROR, "plugins", 
	  "Unable to reload development plugin: %s\n%s", devplugin, errbuf);
  else
    TRACE(TRACE_INFO, "plugins", "Reloaded dev plugin %s", devplugin);

  hts_mutex_unlock(&plugin_mutex);
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
static void
plugin_remove(plugin_t *pl)
{
  char path[200];

  TRACE(TRACE_DEBUG, "plugin", "Uninstalling %s", pl->pl_id);

  snprintf(path, sizeof(path), "%s/installedplugins/%s.zip",
	   showtime_persistent_path, pl->pl_id);
  unlink(path);

#if ENABLE_SPIDERMONKEY
  js_plugin_unload(pl->pl_id);
#endif

  htsmsg_store_remove("plugins/%s", pl->pl_id);

  prop_destroy(pl->pl_inst_prop);
  pl->pl_inst_prop = NULL;
  pl->pl_installed = 0;
  pl->pl_loaded = 0;

  update_state(pl);
}


/**
 *
 */
static int
plugin_install(plugin_t *pl, const char *package)
{
  char errbuf[200];
  char path[200];
  rstr_t *s;

  if(package == NULL)
    package = pl->pl_package;

  if(package == NULL) {
    prop_set_string(pl->pl_statustxt, "No package file specified");
    return -1;
  }

  TRACE(TRACE_INFO, "plugins", "Downloading plugin %s from %s",
	pl->pl_id, package);

  s = _("Downloading");
  prop_set_rstring(pl->pl_statustxt, s);
  rstr_release(s);

  size_t size;

  char *buf = fa_load(package, &size, NULL,
		      errbuf, sizeof(errbuf), NULL, 0, NULL, NULL);

  if(buf == NULL) {
    prop_set_string(pl->pl_statustxt, errbuf);
    return -1;
  }

  if(size < 4 ||
     buf[0] != 0x50 || buf[1] != 0x4b || buf[2] != 0x03 || buf[3] != 0x04) {
    s = _("Corrupt plugin bundle");
    prop_set_rstring(pl->pl_statustxt, s);
    rstr_release(s);
    return -1;
  }
  
  TRACE(TRACE_INFO, "plugins", "Plugin %s valid ZIP archive %d bytes",
	pl->pl_id, (int)size);
  s = _("Installing");
  prop_set_rstring(pl->pl_statustxt, s);
  rstr_release(s);

  snprintf(path, sizeof(path), "%s/installedplugins", showtime_persistent_path);
  mkdir(path, 0770);

  snprintf(path, sizeof(path), "%s/installedplugins/%s.zip",
	   showtime_persistent_path, pl->pl_id);

  unlink(path);

#if ENABLE_SPIDERMONKEY
  js_plugin_unload(pl->pl_id);
#endif

  int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0660);
  if(fd == -1) {
    s = _("File open error");
    prop_set_rstring(pl->pl_statustxt, s);
    rstr_release(s);

    TRACE(TRACE_ERROR, "plugins", "Unable to write to %s -- %s",
	  path, strerror(errno));
    free(buf);
    return -1;
  }

  size_t r = write(fd, buf, size);
  free(buf);
  if(close(fd) || r != size) {
    s = _("Disk write error");
    prop_set_rstring(pl->pl_statustxt, s);
    rstr_release(s);
    TRACE(TRACE_ERROR, "plugins", "Unable to write to %s -- %s",
	  path, strerror(errno));
    return -1;
  }

  snprintf(path, sizeof(path),
	   "zip://file://%s/installedplugins/%s.zip", showtime_persistent_path,
	   pl->pl_id);

  if(plugin_load(path, errbuf, sizeof(errbuf), 1, 1)) {
    prop_set_string(pl->pl_statustxt, errbuf);
    TRACE(TRACE_ERROR, "plugins", "Unable to load %s -- %s", path, errbuf);
    return -1;
  }
  return 0;
}


/**
 *
 */
static void
plugin_open_start(prop_t *page)
{
  prop_link(plugin_root_model, prop_create(page, "model"));
}


/**
 *
 */
static void
plugin_open_repo(prop_t *page)
{
  struct prop_nf *pnf;
  prop_t *model = prop_create(page, "model");
  prop_set_string(prop_create(model, "type"), "directory");

  prop_link(_p("Available plugins"),
	    prop_create(prop_create(model, "metadata"), "title"));

  pnf = prop_nf_create(prop_create(model, "nodes"),
		       plugin_root_repo, NULL, PROP_NF_AUTODESTROY);
  prop_nf_sort(pnf, "node.metadata.title", 0, 0, NULL, 1);
  prop_nf_release(pnf);
}


/**
 *
 */
static int
plugin_open_url(prop_t *page, const char *url)
{
  if(!strcmp(url, "plugin:start")) {
    plugin_open_start(page);
    return 0;
  }

  if(!strcmp(url, "plugin:repo")) {
    plugin_open_repo(page);
    return 0;
  }

  nav_open_errorf(page, _("Invalud URI"));
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

/**
 *
 */
void
plugin_open_file(prop_t *page, const char *url)
{
  char path[200];
  char errbuf[200];
  char *buf;

  snprintf(path, sizeof(path), "zip://%s/plugin.json", url);
  buf = fa_load(path, NULL, NULL, errbuf, sizeof(errbuf), NULL, 0, NULL, NULL);
  if(buf == NULL) {
    nav_open_errorf(page, _("Unable to load plugin.json: %s"), errbuf);
    return;
  }

  htsmsg_t *pm = htsmsg_json_deserialize(buf);
  free(buf);

  if(pm == NULL) {
    nav_open_errorf(page, _("Unable to load plugin.json: Malformed JSON"));
    return;
  }

  const char *id = htsmsg_get_str(pm, "id");

  if(id != NULL) {
    hts_mutex_lock(&plugin_mutex);
    plugin_t *pl = plugin_find(id);
    plugin_install(pl, url);
    hts_mutex_unlock(&plugin_mutex);
  } else {
    nav_open_errorf(page, _("Field \"id\" not found in plugin.json"));
  }
  htsmsg_destroy(pm);
}



