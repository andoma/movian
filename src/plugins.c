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

#include "showtime.h"
#include "fileaccess/fileaccess.h"
#include "plugins.h"
#include "htsmsg/htsmsg_json.h"
#include "backend/backend.h"

#if ENABLE_SPIDERMONKEY
#include "js/js.h"
#endif

static hts_mutex_t repo_mutex;


/**
 *
 */
static void
plugin_load_js(const char *id, const char *url)
{
#if ENABLE_SPIDERMONKEY
  char errbuf[256];
  if(js_plugin_load(id, url, errbuf, sizeof(errbuf))) 
    TRACE(TRACE_ERROR, "plugins", "Unable to load %s [%s] -- %s",
	  url, id, errbuf);

#else
  TRACE(TRACE_ERROR, "plugins", 
	"Unable to load %s -- Javscript not enabled", url);
#endif
}


/**
 *
 */
static void
plugin_load(const char *url)
{
  char ctrlfile[URL_MAX];
  char *json;
  struct fa_stat fs;
  char errbuf[256];
  htsmsg_t *ctrl;

  snprintf(ctrlfile, sizeof(ctrlfile), "%s/plugin.json", url);

  json = fa_quickload(ctrlfile, &fs, NULL, errbuf, sizeof(errbuf));

  if(json == NULL) {
    TRACE(TRACE_ERROR, "plugins", "Unable to load plugin control file %s -- %s",
	  ctrlfile, errbuf);
    return;
  }
  
  ctrl = htsmsg_json_deserialize(json);

  if(ctrl != NULL) {

    const char *type = htsmsg_get_str(ctrl, "type");
    const char *file = htsmsg_get_str(ctrl, "file");
    const char *id   = htsmsg_get_str(ctrl, "id");

    if(type == NULL)
      TRACE(TRACE_ERROR, "plugins", 
	    "Missing \"type\" element in control file %s",
	    ctrlfile);
    
    if(file == NULL)
      TRACE(TRACE_ERROR, "plugins", 
	    "Missing \"file\" element in control file %s",
	    ctrlfile);

    if(id == NULL)
      TRACE(TRACE_ERROR, "plugins", 
	    "Missing \"id\" element in control file %s",
	    ctrlfile);

    if(type && file && id) {
      char fullpath[URL_MAX];

      snprintf(fullpath, sizeof(fullpath), "%s/%s", url, file);

      if(!strcmp(type, "javascript")) {

	plugin_load_js(id, fullpath);

      } else {
	TRACE(TRACE_ERROR, "plugins", "Unknown type \"%s\" in control file %s",
	      type, ctrlfile);
      }

    }

    htsmsg_destroy(ctrl);

  } else {

    TRACE(TRACE_ERROR, "plugins", "Unable parse JSON control file %s",
	  ctrlfile);
  }
  free(json);
}


static int
plugin_scandir(const char *url, char *errbuf, size_t errsize)
{
  fa_dir_entry_t *fde;
  fa_dir_t *fd = fa_scandir(url, errbuf, errsize);

  if(fd == NULL)
    return -1;

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link)
    plugin_load(fde->fde_url);
  
  fa_dir_free(fd);
  return 0;
}


/**
 *
 */
void
plugins_init(void)
{
  char errbuf[256];

  hts_mutex_init(&repo_mutex);

  if(plugin_scandir(SHOWTIME_PLUGINS_URL, errbuf, sizeof(errbuf))) {
    TRACE(TRACE_ERROR, "plugins", "Unable to scan default plugindir %s -- %s",
	  SHOWTIME_PLUGINS_URL, errbuf);
  }
}




/**
 *
 */
static int
plugin_canhandle(const char *url)
{
  return !strncmp(url, "plugin:", strlen("plugin:"));
}


#define PLUGIN_REPO_URL "http://localhost:8000/files/plugins.json"

/**
 *
 */
static htsmsg_t *
repo_get(char *errbuf, size_t errlen)
{
  char *result;
  static htsmsg_t *repository;

  hts_mutex_lock(&repo_mutex);
  
  if(repository != NULL) {
    hts_mutex_unlock(&repo_mutex);
    return repository;
  }

  if(http_request(PLUGIN_REPO_URL, NULL, &result, NULL, errbuf, errlen,
		  NULL, NULL, 0, NULL, NULL, NULL)) {
    hts_mutex_unlock(&repo_mutex);
    return NULL;
  }
  
  repository = htsmsg_json_deserialize(result);
  free(result);

  hts_mutex_unlock(&repo_mutex);

  if(repository == NULL)
    snprintf(errbuf, errlen, "Invalid JSON");

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

  if(id == NULL || title == NULL)
    return NULL;

  snprintf(url, sizeof(url), "plugin:repo:%s", id);

  prop_t *p = prop_create_root(NULL);
  prop_t *metadata = prop_create(p, "metadata");

  prop_set_string(prop_create(p, "type"), "plugin");
  prop_set_string(prop_create(p, "url"), url);

  prop_set_string(prop_create(metadata, "title"), title);
  prop_set_string(prop_create(metadata, "icon"),
		  htsmsg_get_str(pm, "icon"));
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
static int
plugin_open_url(prop_t *page, const char *url)
{
  const char *s;
  if(!strcmp(url, "plugin:repo")) {
    plugin_open_repo(page);
    return 0;
  }

  if((s = mystrbegins(url, "plugin:repo:")) != NULL) {

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
