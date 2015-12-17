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
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <sys/types.h>
#include <regex.h>
#include <ctype.h>

#include "main.h"
#include "backend/backend.h"
#include "backend/search.h"
#include "fileaccess.h"
#include "fa_probe.h"
#include "fa_search.h"
#include "service.h"
#include "settings.h"
#include "htsmsg/htsmsg_store.h"

/* FIXME: utf-8 support? */

static int locatedb_enabled;

typedef struct fa_search_s {
  char                 *fas_query;
  prop_t               *fas_nodes;
  prop_sub_t           *fas_sub;
  FILE                 *fas_fp;
  prop_courier_t       *fas_pc;
  int                   fas_run;
} fa_search_t;


static void
fa_search_destroy (fa_search_t *fas)
{
  free(fas->fas_query);

  prop_unsubscribe(fas->fas_sub);

  if (fas->fas_pc)
    prop_courier_destroy(fas->fas_pc);

  if (fas->fas_nodes)
    prop_ref_dec(fas->fas_nodes);

  /* FIXME: We should wrap our own popen2() to get the pid
   *        of the child process so we can kill it.
   *        popen() simply waits for the process to finish, which
   *        could be quite some time for Mr locate. */
  if (fas->fas_fp)
    pclose(fas->fas_fp);

  free(fas);
}


static void
fa_search_nodesub(void *opaque, prop_event_t event, ...)
{
  fa_search_t *fas = opaque;
  va_list ap;

  va_start(ap, event);

  switch(event)
    {
    case PROP_DESTROYED:
      fas->fas_run = 0;
      break;


    default:
      break;
    }

  va_end(ap);
}


/**
 * Replaces regex tokens with '.' in the string.
 * FIXME: Do this properly by escaping instead.
 */
static char *
deregex (char *str)
{
  char *t = str;

  t = str;
  while (*t) {
    switch (*t)
      {
      case '|':
      case '\\':
      case '(':
      case ')':
      case '^':
      case '$':
      case '+':
      case '?':
      case '*':
      case '[':
      case ']':
      case '{':
      case '}':
	*t = '.';
      break;
      }
    t++;
  }
  
  return str;
}


/**
 * Compiles a regexp matching all paths of existing file:// services.
 * E.g.: "^(path1|path2|path3...)".
 */
static int
fa_create_paths_regex (regex_t *preg) {
  service_t *s;
  int len = 0;
  char *str, *t;
  int errcode;

  hts_mutex_lock(&service_mutex);

  /* First calculate the space needed for the regex. */
  LIST_FOREACH(s, &services, s_link)
    if (!strncmp(s->s_url, "file://", strlen("file://")))
      len += strlen(s->s_url + strlen("file://")) + strlen("/|");

  if (len == 0) {
    /* No file:// services found. We should either flunk out
     * and do nothing here, or act as if this was a feature
     * and provide un-filtered 'locate' output. I really dont know
     * whats best. */
    str = strdup(".*");

  } else {
    str = t = malloc(len + strlen("^()") + 1);

    t += sprintf(str, "^(");

    /* Then construct the regex. */
    LIST_FOREACH(s, &services, s_link)
      if (!strncmp(s->s_url, "file://", strlen("file://")))
	t += sprintf(t, "%s%s|",
		     deregex(mystrdupa(s->s_url + strlen("file://"))),
		     (s->s_url[strlen(s->s_url)-1] == '/' ? "" : "/"));

    *(t-1) = ')';
  }

  hts_mutex_unlock(&service_mutex);

  if ((errcode = regcomp(preg, str, REG_EXTENDED|REG_ICASE|REG_NOSUB))) {
    char buf[64];
    regerror(errcode, preg, buf, sizeof(buf));
    TRACE(TRACE_ERROR, "FA", "Search regex compilation of \"%s\" failed: %s",
	  str, buf);
    free(str);
    return -1;
  }

  free(str);
  return 0;
}


/**
 * Consume 'locate' (updatedb) output results and feed into search results.
 */
static void
fa_locate_searcher (fa_search_t *fas)
{
  char buf[PATH_MAX];
  char iconpath[PATH_MAX];
  regex_t preg;

  prop_t *entries[2] = {NULL, NULL};
  prop_t *nodes[2] = {NULL, NULL};
  int t, i;

  if (fa_create_paths_regex(&preg) == -1)
    return fa_search_destroy(fas);

  snprintf(iconpath, sizeof(iconpath), "%s/res/fileaccess/fs_icon.png",
	   app_dataroot());

  /* Consume 'locate' results. */
  while (1) {
    char url[PATH_MAX+strlen("file://")];
    prop_t *p, *metadata;
    const char *type;
    struct fa_stat fs;
    int ctype;

    prop_courier_poll(fas->fas_pc);

    if (!fas->fas_run)
      break;

    if (!fgets(buf, sizeof(buf), fas->fas_fp))
      break;

    if (!*buf || *buf == '\n')
      continue;

    buf[strlen(buf)-1] = '\0';

    /* Ignore dot-files/dirs. */
    if (strstr(buf, "/."))
      continue;

    if (regexec(&preg, buf, 0, NULL, 0)) {
      TRACE(TRACE_DEBUG, "FA", "Searcher: %s: \"%s\" not matching regex: SKIP",
	    fas->fas_query, buf);
      continue;
    }

    /* Probe media type.
     *
     * FIXME: We might want to hide matching files under a matching directory,
     *        or the other way around.
     *        E..g:
     *           Metallica/
     *                     01-Metallica-Song1.mp3
     *                     02-Metallica-Song1.mp3
     *
     *        Should either hide Metallica/ or 01-Metallica-Song1..2..
     *        But that would require the 'locate' output to be sorted, is it?
     *        Its also problematic where a sub set of the tracks matches
     *        the directory name. Then what should we show?.
     *
     *        There is also the problem with:
     *        Scrubs S01E01/
     *                 Scrubs_s01e01.avi
     *                 Sample/
     *                    Scrubs_s01e01_sample.avi
     *        Which will show as four separate entries, less than optimal.
     *
     *        For now we provide all matches, directories and files,
     *        matching on the entire path (not just the basename).
     */

    snprintf(url, sizeof(url), "file://%s", buf);

    if (fa_stat(url, &fs, NULL, 0))
      continue;

    metadata = prop_create_root("metadata");

    if(fs.fs_type == CONTENT_DIR) {
      ctype = CONTENT_DIR;
      prop_set_string(prop_create(metadata, "title"), basename(buf));
    } else {
      metadata_t *md = fa_probe_metadata(url, NULL, 0, NULL, NULL);
      if(md != NULL) {
	ctype = md->md_contenttype;
	metadata_destroy(md);
      } else {
	ctype = CONTENT_UNKNOWN;
      }
    }


    if (ctype == CONTENT_UNKNOWN)
      continue;

    switch(ctype) {
    case CONTENT_AUDIO:
      t = 0;
      break;

    case CONTENT_VIDEO:
    case CONTENT_DVD:
      t = 1;
      break;

    default:
      continue;
    }


    if(nodes[t] == NULL)
      if(search_class_create(fas->fas_nodes, &nodes[t], &entries[t],
			     t ? "Local video files" : "Local audio files",
			     iconpath))
	break;

    prop_add_int(entries[t], 1);

    if ((type = content2type(ctype)) == NULL)
      continue; /* Unlikely.. */


    p = prop_create_root(NULL);

    if (prop_set_parent(metadata, p))
      prop_destroy(metadata);

    prop_set_string(prop_create(p, "url"), url);
    prop_set_string(prop_create(p, "type"), type);

    if(prop_set_parent(p, nodes[t])) {
      prop_destroy(p);
      break;
    }
  }
  
  for(i = 0; i < 2; i++) {
    if(nodes[i])
      prop_ref_dec(nodes[i]);
    if(entries[i])
      prop_ref_dec(entries[i]);
  }

  TRACE(TRACE_DEBUG, "FA", "Searcher: %s: Done", fas->fas_query);
  fa_search_destroy(fas);

  regfree(&preg);
}


static void *
fa_searcher (void *aux)
{
  fa_search_t *fas = aux;
  char cmd[PATH_MAX];

  /* FIXME: We should have some sort of priority here. E.g.:
   *         1) User defined search command.
   *         2) Some omnipresent indexer (beagle?)
   *         3) locate/updatedb
   *         4) standard find.
   */
  snprintf(cmd, sizeof(cmd),
	   "locate -i -L -q -b '%s'", fas->fas_query);

  TRACE(TRACE_DEBUG, "FA", "Searcher: %s: executing \"%s\"",
	fas->fas_query, cmd);

  if ((fas->fas_fp = popen(cmd, "re")) == NULL) {
    TRACE(TRACE_ERROR, "FA", "Searcher: %s: Unable to execute \"%s\": %s",
	  fas->fas_query, cmd, strerror(errno));
    fa_search_destroy(fas);
    return NULL;
  }

  fas->fas_pc = prop_courier_create_passive();
  fas->fas_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, fa_search_nodesub, fas,
		   PROP_TAG_ROOT, fas->fas_nodes,
		   PROP_TAG_COURIER, fas->fas_pc,
		   NULL);

  fa_locate_searcher(fas);

  return NULL;
}


static void
locatedb_search(prop_t *model, const char *query, prop_t *loading)
{
  if (!locatedb_enabled)
    return;

  fa_search_t *fas = calloc(1, sizeof(*fas));
  char *s;

  /* Convery query to lower-case to provide case-insensitive search. */
  fas->fas_query = s = strdup(query);
  do {
    *s = tolower((int)*s);
  } while (*++s);

  fas->fas_run = 1;
  fas->fas_nodes = prop_ref_inc(prop_create(model, "nodes"));

  hts_thread_create_detached("fa search", fa_searcher, fas,
			     THREAD_PRIO_MODEL);
}


/**
 *
 */
static int
locatedb_init(void)
{
  prop_t *s = search_get_settings();

  setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Search using Unix locatedb")),
                 SETTING_VALUE(1),
                 SETTING_WRITE_BOOL(&locatedb_enabled),
                 SETTING_STORE("locatedb", "enable"),
                 NULL);

  return 0;
}


/**
 *
 */
backend_t be_locatedb = {
  .be_init = locatedb_init,
  .be_search = locatedb_search
};

BE_REGISTER(locatedb);
