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
#include <CoreServices/CoreServices.h>

#include "main.h"
#include "backend/backend.h"
#include "backend/search.h"
#include "fileaccess.h"
#include "fa_probe.h"
#include "fa_search.h"
#include "service.h"
#include "settings.h"
#include "metadata/metadata.h"
#include "htsmsg/htsmsg_store.h"


static int spotlight_enabled;

typedef struct fa_search_s {
  char                 *fas_query;
  prop_t               *fas_nodes;
  prop_sub_t           *fas_sub;
  prop_courier_t       *fas_pc;
  int                   fas_run;
} fa_search_t;

static void
fa_search_destroy(fa_search_t *fas)
{
  free(fas->fas_query);
  
  prop_unsubscribe(fas->fas_sub);
  
  if (fas->fas_pc)
    prop_courier_destroy(fas->fas_pc);
  
  if (fas->fas_nodes)
    prop_ref_dec(fas->fas_nodes);
  
  free(fas);
}

static CFArrayRef
spotlight_scope_directories(void) {
  CFMutableArrayRef directories;
  service_t *s;
  
  directories = CFArrayCreateMutable(kCFAllocatorDefault,
                                     0,
                                     &kCFTypeArrayCallBacks);
  
  hts_mutex_lock(&service_mutex);
  
  LIST_FOREACH(s, &services, s_link) {
    if(strncmp(s->s_url, "file://", strlen("file://")) != 0)
      continue;
    
    CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault,
                                                 s->s_url + strlen("file://"),
                                                 kCFStringEncodingUTF8);
    CFArrayAppendValue(directories, path);
    CFRelease(path);
  }
  
  hts_mutex_unlock(&service_mutex);
  
  return directories;
}

static void
spotlight_search_nodesub(void *opaque, prop_event_t event, ...)
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

static void *
spotlight_searcher(void *aux)
{
  fa_search_t *fas = aux;
  CFArrayRef directories;
  MDQueryRef query;
  CFMutableStringRef query_string;
  CFIndex query_count;
  CFIndex query_index;
  prop_t *entries[2] = {NULL, NULL};
  prop_t *nodes[2] = {NULL, NULL};
  int i, t;
  char iconpath[PATH_MAX];

  snprintf(iconpath, sizeof(iconpath), "%s/res/fileaccess/fs_icon.png",
	   app_dataroot());

  fas->fas_pc = prop_courier_create_passive();
  fas->fas_sub = 
  prop_subscribe(PROP_SUB_TRACK_DESTROY,
                 PROP_TAG_CALLBACK, spotlight_search_nodesub, fas,
                 PROP_TAG_ROOT, fas->fas_nodes,
                 PROP_TAG_COURIER, fas->fas_pc,
                 NULL);
  
  query_string = CFStringCreateMutable(kCFAllocatorDefault, 0);
  CFStringAppendCString(query_string, "kMDItemFSName = '*",
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, fas->fas_query,
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, "*'cd || ",
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, "kMDItemAlbum = '*",
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, fas->fas_query,
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, "*'cd || ",
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, "kMDItemAuthors = '*",
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, fas->fas_query,
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, "*'cd || ",
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, "kMDItemTitle = '*",
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, fas->fas_query,
                        kCFStringEncodingUTF8);
  CFStringAppendCString(query_string, "*'cd",
                        kCFStringEncodingUTF8);
  query = MDQueryCreate(kCFAllocatorDefault,
                        query_string,
                        NULL,
                        NULL);
  CFRelease(query_string);
  directories = spotlight_scope_directories();
  MDQuerySetSearchScope(query, directories, 0);
  CFRelease(directories);
  MDQueryExecute(query, kMDQuerySynchronous);
  MDQueryStop(query);
  query_count = MDQueryGetResultCount(query);
  query_index = 0;
  
  while (1) {
    prop_t *p, *metadata;
    const char *type;
    int ctype;
    MDItemRef item;
    CFStringRef pathRef;
    int len;
    char *path;

    prop_courier_poll(fas->fas_pc);
    
    if(!fas->fas_run)
      break;
    
    if(query_index >= query_count)
      break;
    
    item = (MDItemRef)MDQueryGetResultAtIndex(query, query_index);
    query_index++;
    
    metadata = prop_create_root("metadata");
    
    pathRef = (CFStringRef)MDItemCopyAttribute(item, kMDItemPath);
    
    len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(pathRef),
                                            kCFStringEncodingUTF8) + 1;
    path = malloc(len);
    CFStringGetCString(pathRef, path, len, kCFStringEncodingUTF8);
    CFRelease(pathRef);
    metadata_t *md = fa_probe_metadata(path, NULL, 0, NULL, NULL);
    
    if(md == NULL)
      continue;

    t = 0;
    ctype = md->md_contenttype;
    switch(md->md_contenttype) {
    case CONTENT_AUDIO:
      break;
    case CONTENT_VIDEO:
    case CONTENT_DVD:
      t = 1;
      break;
    default:
      continue;
    }
    metadata_destroy(md);

    if(nodes[t] == NULL)
      if(search_class_create(fas->fas_nodes, &nodes[t], &entries[t],
			     t ? "Local video files" : "Local audio files",
                             iconpath)) {
	free(path);
	break;
      }

    prop_add_int(entries[t], 1);

    if((type = content2type(ctype)) == NULL)
      continue; /* Unlikely.. */

    p = prop_create_root(NULL);

    if(prop_set_parent(metadata, p))
      prop_destroy(metadata);

    prop_set_string(prop_create(p, "url"), path);
    free(path);
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
  
  CFRelease(query);
  
  TRACE(TRACE_DEBUG, "FA", "Searcher: %s: Done", fas->fas_query);
  fa_search_destroy(fas);
  
  return NULL;
}

static void
spotlight_search(prop_t *model, const char *query, prop_t *loading)
{
  if(!spotlight_enabled)
    return;
  
  fa_search_t *fas = calloc(1, sizeof(*fas));
  char *s;
  
  fas->fas_query = s = strdup(query);
  fas->fas_run = 1;
  fas->fas_nodes = prop_ref_inc(prop_create(model, "nodes"));
  
  hts_thread_create_detached("spotlight search", spotlight_searcher, fas,
			     THREAD_PRIO_MODEL);
}

static int
spotlight_init(void)
{
  prop_t *s = search_get_settings();

  setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Search using spotlight")),
                 SETTING_VALUE(1),
                 SETTING_WRITE_BOOL(&spotlight_enabled),
                 SETTING_STORE("spotlight", "enable"),
                 NULL);

  return 0;
}

backend_t be_spotlight = {
  .be_init = spotlight_init,
  .be_search = spotlight_search
};

BE_REGISTER(spotlight);
