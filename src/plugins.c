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

#if ENABLE_SPIDERMONKEY
#include "js/js.h"
#endif


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
  if(plugin_scandir(SHOWTIME_PLUGINS_URL, errbuf, sizeof(errbuf))) {
    TRACE(TRACE_ERROR, "plugins", "Unable to scan default plugindir %s -- %s",
	  SHOWTIME_PLUGINS_URL, errbuf);
  }

}
