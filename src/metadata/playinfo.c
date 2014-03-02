/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <stdio.h>
#include <unistd.h>

#include <sqlite3.h>

#include "prop/prop.h"
#include "metadata.h"
#include "db/db_support.h"
#include "db/kvstore.h"
#include "showtime.h"
#include "playinfo.h"

#define METADATA_VERSION_STR "1"

static HTS_MUTEX_DECL(mip_mutex);

static void update_by_url(const char *url, int dolock);

/**
 *
 */
void
playinfo_register_play(const char *url, int inc)
{
  int cur = kv_url_opt_get_int(url, KVSTORE_DOMAIN_SYS, "playcount", 0);

  kv_url_opt_set(url, KVSTORE_DOMAIN_SYS, "playcount",
                 KVSTORE_SET_INT, cur + inc);

  kv_url_opt_set(url, KVSTORE_DOMAIN_SYS, "lastplayed",
                 KVSTORE_SET_INT, (int)(time(NULL)));

  update_by_url(url, 1);
}



/**
 *
 */
void
playinfo_set_restartpos(const char *url, int64_t pos_ms, int unimportant)
{
  int f = unimportant ? KVSTORE_UNIMPORTANT : 0;
  kv_url_opt_set(url, KVSTORE_DOMAIN_SYS, "restartposition",
                 f | (pos_ms <= 0 ? KVSTORE_SET_VOID : KVSTORE_SET_INT64),
		 pos_ms);
  update_by_url(url, 1);
}


/**
 *
 */
int64_t
playinfo_get_restartpos(const char *url)
{
  return kv_url_opt_get_int64(url, KVSTORE_DOMAIN_SYS, "restartposition", 0);
}

/**
 *
 */
typedef struct metadb_item_prop {
  LIST_ENTRY(metadb_item_prop) mip_link;
  prop_t *mip_playcount;
  prop_t *mip_lastplayed;
  prop_t *mip_restartpos;

  char *mip_url;
  prop_sub_t *mip_destroy_sub;
  prop_sub_t *mip_playcount_sub;

  int mip_refcount;

} metadb_item_prop_t;

#define MIP_HASHWIDTH 311

static LIST_HEAD(, metadb_item_prop) mip_hash[MIP_HASHWIDTH];



typedef struct metadb_item_info {
  int mii_playcount;
  int mii_lastplayed;
  int mii_restartpos;
} metadb_item_info_t;

/**
 *
 */
static void
mip_get(const char *url, metadb_item_info_t *mii)
{
  mii->mii_playcount  =
    kv_url_opt_get_int(url, KVSTORE_DOMAIN_SYS, "playcount", 0);

  mii->mii_lastplayed =
    kv_url_opt_get_int(url, KVSTORE_DOMAIN_SYS, "lastplayed", 0);

  mii->mii_restartpos =
    kv_url_opt_get_int64(url, KVSTORE_DOMAIN_SYS, "restartposition", 0);
}


/**
 *
 */
static void
mip_set(metadb_item_prop_t *mip, const metadb_item_info_t *mii)
{
  prop_set_int_ex(mip->mip_playcount, mip->mip_playcount_sub,
		  mii->mii_playcount);
  prop_set_int(mip->mip_lastplayed, mii->mii_lastplayed);
  prop_set_float(mip->mip_restartpos, mii->mii_restartpos / 1000.0);
}


/**
 *
 */
static void
update_by_url(const char *url, int dolock)
{
  metadb_item_info_t mii;

  mip_get(url, &mii);

  metadb_item_prop_t *mip;
  const unsigned int hash = mystrhash(url) % MIP_HASHWIDTH;

  if(dolock)
    hts_mutex_lock(&mip_mutex);
  LIST_FOREACH(mip, &mip_hash[hash], mip_link)
    if(!strcmp(mip->mip_url, url))
      mip_set(mip, &mii);
  if(dolock)
    hts_mutex_unlock(&mip_mutex);
}


/**
 *
 */
static void
mip_release(metadb_item_prop_t *mip)
{
  mip->mip_refcount--;
  if(mip->mip_refcount > 0)
    return;

  LIST_REMOVE(mip, mip_link);

  prop_unsubscribe(mip->mip_destroy_sub);
  prop_unsubscribe(mip->mip_playcount_sub);
  prop_ref_dec(mip->mip_playcount);
  prop_ref_dec(mip->mip_lastplayed);
  prop_ref_dec(mip->mip_restartpos);
  free(mip->mip_url);
  free(mip);

}

/**
 *
 */
static void
metadb_item_prop_destroyed(void *opaque, prop_event_t event, ...)
{
  metadb_item_prop_t *mip = opaque;
  if(event == PROP_DESTROYED)
    mip_release(mip);
}

/**
 *
 */
static void
metadb_set_playcount(void *opaque, prop_event_t event, ...)
{
  metadb_item_prop_t *mip = opaque;
  va_list ap;

  if(event == PROP_DESTROYED) {
    mip_release(mip);
    return;
  }
  if(event != PROP_SET_INT) 
    return;

  va_start(ap, event);
  int v = va_arg(ap, int);
  va_end(ap);

  kv_url_opt_set(mip->mip_url, KVSTORE_DOMAIN_SYS, "playcount",
                 KVSTORE_SET_INT, v);
  update_by_url(mip->mip_url, 0);
}



/**
 *
 */
void
playinfo_bind_url_to_prop(const char *url, prop_t *parent)
{
  metadb_item_info_t mii;
  mip_get(url, &mii);

  metadb_item_prop_t *mip = malloc(sizeof(metadb_item_prop_t));

  hts_mutex_lock(&mip_mutex);
  mip->mip_refcount = 2;  // One per subscription created below

  mip->mip_destroy_sub =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, metadb_item_prop_destroyed, mip,
		   PROP_TAG_ROOT, parent,
		   PROP_TAG_MUTEX, &mip_mutex,
		   NULL);

  assert(mip->mip_destroy_sub != NULL);


  mip->mip_playcount  = prop_create_r(parent, "playcount");
  mip->mip_lastplayed = prop_create_r(parent, "lastplayed");
  mip->mip_restartpos = prop_create_r(parent, "restartpos");
  
  mip->mip_playcount_sub =
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, metadb_set_playcount, mip,
		   PROP_TAG_ROOT, mip->mip_playcount,
		   PROP_TAG_MUTEX, &mip_mutex,
		   NULL);
  
  assert(mip->mip_playcount_sub != NULL);

  mip->mip_url = strdup(url);
  unsigned int hash = mystrhash(url) % MIP_HASHWIDTH;
  LIST_INSERT_HEAD(&mip_hash[hash], mip, mip_link);
  mip_set(mip, &mii);
  hts_mutex_unlock(&mip_mutex);
}


/**
 *
 */
void
playinfo_mark_urls_as(const char **urls, int num_urls, int seen)
{
  for(int j = 0; j < num_urls; j++) {
    kv_url_opt_set(urls[j], KVSTORE_DOMAIN_SYS, "playcount",
                   KVSTORE_SET_INT, seen);
    update_by_url(urls[j], 1);
  }
}
