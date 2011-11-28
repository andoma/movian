/*
 *  Backend using Spotify
 *  Copyright (C) 2009, 2010 Andreas Öman
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/file.h>

#include "showtime.h"
#include "prop/prop_nodefilter.h"
#include "navigator.h"
#include "backend/backend.h"
#include "backend/search.h"
#include "backend/backend_prop.h"
#include "playqueue.h"
#include "media.h"
#include "notifications.h"
#include "ext/spotify/api.h"
#include "keyring.h"
#include "misc/ptrvec.h"
#include "service.h"
#include "misc/pixmap.h"
#include "settings.h"
#include "htsmsg/htsbuf.h"

#include "api/lastfm.h"

#define SPOTIFY_ICON_URL "bundle://resources/spotify/spotify_icon.png"

#ifdef CONFIG_LIBSPOTIFY_LOAD_RUNTIME
#include <dlfcn.h>
#endif
#include "ext/spotify/apifunctions.h"
#include "spotify_app_key.h"

struct spotify_page;

/**
 *
 */
static int silent_start;
static int spotify_is_enabled;
static prop_courier_t *spotify_courier;
static media_pipe_t *spotify_mp;
static hts_mutex_t spotify_mutex;
static hts_cond_t spotify_cond_main;
static int spotify_high_bitrate;
static int spotify_offline_bitrate_96;

static int play_position;
static int seek_pos;


static int is_thread_running;
static int is_logged_in;
static int login_rejected_by_user;
static int pending_login;
static const char *pending_relogin;

static sp_session *spotify_session;
TAILQ_HEAD(spotify_msg_queue, spotify_msg);
static struct spotify_msg_queue spotify_msgs;


/**
 * Binds a property to metadata in spotify
 * This will make sure the properties stays updated with the
 * spotify version of the track.
 * The structure self-destructs when the property tree is deleted (by
 * a subscription that checks PROP_DESTROYED)
 */

LIST_HEAD(metadata_list, metadata);

typedef struct metadata {
  LIST_ENTRY(metadata) m_link;
  sp_track *m_source;

  prop_sub_t *m_sub;

  char m_static_set;
  prop_t *m_available;
  prop_t *m_title;
  prop_t *m_trackindex;
  prop_t *m_duration;
  prop_t *m_popularity;
  prop_t *m_album;
  prop_t *m_album_art;
  prop_t *m_album_year;
  prop_t *m_artist;
  prop_t *m_additional_artists;
  prop_t *m_artist_images;
  prop_t *m_starred;
  prop_t *m_status;

} metadata_t;


/**
 * Users
 */
typedef struct spotify_user {
  LIST_ENTRY(spotify_user) su_link;
  sp_user *su_user;
  prop_t *su_prop;

  prop_t *su_prop_name;
  prop_t *su_prop_picture;

  int su_mark;
  prop_t *su_prop_friend;
  prop_t *su_prop_title;
  prop_t *su_prop_url;

} spotify_user_t;

static LIST_HEAD(, spotify_user) spotify_users;

#if SPOTIFY_WITH_SOCIAL
static void spotify_userinfo_updated(sp_session *session);
#endif

static prop_t *friend_nodes;


/**
 * Playlist support
 */
static LIST_HEAD(, playlistcontainer) rethink_playlistcontainers;
typedef struct playlistcontainer {

  LIST_ENTRY(playlistcontainer) plc_rethink_link;
  int plc_rethink;

  prop_t *plc_root_tree;
  prop_t *plc_root_flat;
  prop_t *plc_pending;
  ptrvec_t plc_playlists;

  char *plc_name;
  sp_playlistcontainer *plc_pc;
  prop_sub_t *plc_destroy_sub;

} playlistcontainer_t;

static playlistcontainer_t *current_user_rootlist;
static void plc_for_user(sp_session *sess, struct spotify_page *sp,
			 const char *username);

static LIST_HEAD(, playlist) playlists;

typedef struct playlist {
  
  LIST_ENTRY(playlist) pl_link;

  sp_playlist *pl_playlist;
  sp_playlist_type pl_type;
  uint64_t pl_folder_id;
  ptrvec_t pl_tracks;

  prop_t *pl_prop_root_flat;
  prop_t *pl_prop_root_tree;
  prop_t *pl_prop_canDelete;
  prop_t *pl_prop_url;
  prop_t *pl_prop_tracks;
  prop_t *pl_prop_title;
  prop_t *pl_prop_num_tracks;
  prop_t *pl_prop_user;
  prop_t *pl_prop_childs;
  prop_t *pl_prop_icon;

  prop_t *pl_prop_offline;
  prop_sub_t *pl_offline_sub;

  prop_t *pl_prop_collab;
  prop_sub_t *pl_collab_sub;

  prop_sub_t *pl_node_sub;
  prop_sub_t *pl_destroy_sub;

  int pl_flags;
#define PL_WITH_TRACKS 0x1
#define PL_MESSAGES    0x2
#define PL_SORT_ON_TIME 0x4

  struct playlist *pl_start; // End folder point to its start

  struct metadata_list pl_pending_metadata;

} playlist_t;


typedef struct playlist_item {
  prop_t *pli_prop_root;
  prop_t *pli_prop_metadata;
} playlist_item_t;

static void load_initial_playlists(sp_session *sess);
static void unload_initial_playlists(sp_session *sess);


static int spotify_pending_events;

typedef enum {
  SPOTIFY_LOGOUT,
  SPOTIFY_PLAY_TRACK,
  SPOTIFY_STOP_PLAYBACK,
  SPOTIFY_SEEK,
  SPOTIFY_PAUSE,
  SPOTIFY_GET_IMAGE,
  SPOTIFY_OPEN_PAGE,
  SPOTIFY_SEARCH,
  SPOTIFY_RESOLVE_ITEM,
} spotify_msg_type_t;

/**
 * A spotify URI
 */
typedef struct spotify_uri {
  const char *su_uri;

  char *su_errbuf;
  size_t su_errlen;
  int su_errcode;

  prop_t *su_list;     // for be_spotify_list()

  sp_track *su_track;

} spotify_uri_t;

static hts_cond_t spotify_cond_uri;
static spotify_uri_t *su_playing, *su_pending;


/**
 * A spotify page query
 */
typedef struct spotify_page {
  LIST_ENTRY(spotify_page) sp_query_link;
  
  prop_t *sp_urlprop;

  prop_t *sp_model;
  prop_t *sp_type;
  prop_t *sp_error;
  prop_t *sp_loading;
  prop_t *sp_contents;
  prop_t *sp_title;
  prop_t *sp_icon;
  prop_t *sp_album_name;
  prop_t *sp_album_year;
  prop_t *sp_album_art;
  prop_t *sp_artist_name;
  prop_t *sp_numtracks;
  prop_t *sp_user;

  prop_t *sp_nodes;
  prop_t *sp_items;
  prop_t *sp_filter;
  prop_t *sp_canFilter;
  prop_t *sp_canDelete;


  char *sp_url;
  sp_track *sp_track;

} spotify_page_t;

static LIST_HEAD(, spotify_page) pending_album_queries;
static LIST_HEAD(, spotify_page) pending_track_item_resolve;

/**
 * Message for communication with main thread
 */
typedef struct spotify_msg {
  TAILQ_ENTRY(spotify_msg) sm_link;
  spotify_msg_type_t sm_op;
  union {
    void *sm_ptr;
    int sm_int;
  };
} spotify_msg_t;


/**
 * Image load request
 */
typedef struct spotify_image {
  const char *si_url;
  int si_errcode;
  pixmap_t *si_pixmap;
} spotify_image_t;

static hts_cond_t spotify_cond_image;


typedef struct {
  int ssr_offset;
  int ssr_last_search;
  prop_t *ssr_nodes;
  prop_t *ssr_entries;
  prop_sub_t *ssr_sub;
  struct spotify_search *ssr_ss;
} spotify_search_request_t;

/**
 * Search query
 */
typedef struct spotify_search {
  int ss_ref;
  char *ss_query;

  spotify_search_request_t ss_reqs[3];


#define SS_TRACKS 0
#define SS_ALBUMS 1
#define SS_ARTISTS 2

} spotify_search_t;

static void spotify_relogin0(const char *reason);


static void parse_search_reply(sp_search *result, prop_t *nodes, 
			       prop_t *contents);

static playlist_t *pl_create(sp_playlist *plist,
			     const char *name,
			     prop_t *model,
			     prop_t *loading,
			     prop_t *type,
			     prop_t *title,
			     prop_t *icon,
			     prop_t *canDelete,
			     prop_t *url,
			     prop_t *numtracks,
			     prop_t *nodes,
			     prop_t *items,
			     prop_t *filter,
			     prop_t *canFilter,
			     prop_t *user,
			     int flags);

static void spotify_shutdown_early(void *opaque, int retcode);
static void spotify_shutdown_late(void *opaque, int retcode);

static void spotify_try_pending(void);

/**
 *
 */
static spotify_msg_t *
spotify_msg_build(spotify_msg_type_t op, void *ptr)
{
  spotify_msg_t *sm = malloc(sizeof(spotify_msg_t));
  sm->sm_op = op;
  sm->sm_ptr = ptr;
  return sm;
}


/**
 *
 */
static spotify_msg_t *
spotify_msg_build_int(spotify_msg_type_t op, int v)
{
  spotify_msg_t *sm = malloc(sizeof(spotify_msg_t));
  sm->sm_op = op;
  sm->sm_int = v;
  return sm;
}


/**
 *
 */
static void
spotify_msg_enq_locked(spotify_msg_t *sm)
{
  TAILQ_INSERT_TAIL(&spotify_msgs, sm, sm_link);
  hts_cond_signal(&spotify_cond_main);
}


/**
 *
 */
static void
spotify_msg_enq(spotify_msg_t *sm)
{
  hts_mutex_lock(&spotify_mutex);
  spotify_msg_enq_locked(sm);
  hts_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
static void
spotify_msg_enq_one(spotify_msg_t *sm)
{
  spotify_msg_t *sm2;
  hts_mutex_lock(&spotify_mutex);

  TAILQ_FOREACH(sm2, &spotify_msgs, sm_link) {
    if(sm2->sm_op == sm->sm_op) {
      sm2->sm_int = sm->sm_int;
      break;
    }
  }
  if(sm2 == NULL)
    spotify_msg_enq_locked(sm);
  else
    free(sm);

  hts_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
static void
spotify_page_destroy(spotify_page_t *sp)
{
  prop_ref_dec(sp->sp_model);
  prop_ref_dec(sp->sp_type);
  prop_ref_dec(sp->sp_error);
  prop_ref_dec(sp->sp_loading);
  prop_ref_dec(sp->sp_contents);
  prop_ref_dec(sp->sp_title);
  prop_ref_dec(sp->sp_icon);
  prop_ref_dec(sp->sp_album_name);
  prop_ref_dec(sp->sp_album_year);
  prop_ref_dec(sp->sp_album_art);
  prop_ref_dec(sp->sp_artist_name);
  prop_ref_dec(sp->sp_urlprop);

  prop_ref_dec(sp->sp_nodes);
  prop_ref_dec(sp->sp_items);
  prop_ref_dec(sp->sp_filter);
  prop_ref_dec(sp->sp_canFilter);

  prop_ref_dec(sp->sp_user);
  prop_ref_dec(sp->sp_numtracks);
  prop_ref_dec(sp->sp_canDelete);

  free(sp->sp_url);

  if(sp->sp_track != NULL)
    f_sp_track_release(sp->sp_track);

  free(sp);
}


/**
 *
 */
static void
spotify_open_page_fail(spotify_page_t *sp, const char *msg)
{
  if(sp->sp_error != NULL) {
    // XXX(l10n): Missing
    prop_set_string(sp->sp_error, msg);
    prop_set_string(sp->sp_type, "openerror");
  }
  prop_set_int(sp->sp_loading, 0);
  spotify_page_destroy(sp);
}


/**
 * Processing done, signal back to calling thread
 */
static void
spotify_uri_return(spotify_uri_t *su, int errcode)
{
  if(su->su_track != NULL)
    f_sp_track_release(su->su_track);

  hts_mutex_lock(&spotify_mutex);
  su->su_errcode = errcode;
  hts_cond_broadcast(&spotify_cond_uri);
  hts_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
static void
spotify_get_image_fail(spotify_image_t *si)
{
  hts_mutex_lock(&spotify_mutex);
  si->si_errcode = 1;
  hts_cond_broadcast(&spotify_cond_image);
  hts_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
static void
search_release(spotify_search_t *ss)
{
  int i;
  ss->ss_ref--;
  if(ss->ss_ref > 0)
    return;

  for(i = 0; i < 3; i++) {
    prop_unsubscribe(ss->ss_reqs[i].ssr_sub);
    prop_ref_dec(ss->ss_reqs[i].ssr_nodes);
    prop_ref_dec(ss->ss_reqs[i].ssr_entries);
  }

  free(ss->ss_query);
  free(ss);
}


/**
 *
 */
static void
fail_pending_messages(const char *msg)
{
  spotify_msg_t *sm;

  TRACE(TRACE_DEBUG, "spotify", "Closing pending requests : %s", msg);

  hts_mutex_lock(&spotify_mutex);

  while((sm = TAILQ_FIRST(&spotify_msgs)) != NULL) {
    TAILQ_REMOVE(&spotify_msgs, sm, sm_link);
    
    switch(sm->sm_op) {
    case SPOTIFY_LOGOUT:
    case SPOTIFY_STOP_PLAYBACK:
    case SPOTIFY_SEEK:
    case SPOTIFY_PAUSE:
      break;
    case SPOTIFY_OPEN_PAGE:
    case SPOTIFY_RESOLVE_ITEM:
      spotify_open_page_fail(sm->sm_ptr, msg);
      break;
    case SPOTIFY_PLAY_TRACK:
      spotify_uri_return(sm->sm_ptr, 1);
      break;

    case SPOTIFY_GET_IMAGE:
      spotify_get_image_fail(sm->sm_ptr);
      break;
      
    case SPOTIFY_SEARCH:
      search_release(sm->sm_ptr);
      break;
    }
    free(sm);
  }
  hts_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
static void
spotify_try_login(sp_session *s, int retry, const char *reason, int silent) 
{
  char *username;
  char *password;
  int r;

  if(pending_login)
    return;

  reason = reason ?: "Enter login credentials";

  if(!retry) {
    char ruser[256];

    if(f_sp_session_remembered_user(s, ruser, sizeof(ruser)) != -1) {

      if(f_sp_session_relogin(s) == SP_ERROR_OK) {
	pending_login = 1;
	TRACE(TRACE_INFO, "Spotify", "Automatic login attempt as user %s",
	      ruser);
	return;
      }
    }
  }

  int remember_me;
   
  if(silent) {
    fail_pending_messages("No credentials for autologin");
    return;
  }

  r = keyring_lookup("spotify", &username, &password, NULL, &remember_me,
		     "Spotify", reason,
		     KEYRING_QUERY_USER | KEYRING_SHOW_REMEMBER_ME | 
		     KEYRING_REMEMBER_ME_SET | KEYRING_ONE_SHOT);
  
  if(r) {
    login_rejected_by_user = 1;
    // Login canceled by user
    fail_pending_messages("Login canceled by user");
    return;
  }
  TRACE(TRACE_INFO, "Spotify", "Attempting to login in as user '%s'%s",
	username,
	remember_me ? ", and remembering credentials" : "");
  
  f_sp_session_login(s, username, password, remember_me);
  free(username);
  free(password);

  pending_login = 1;
}


/**
 *
 */
static int
is_permanent_error(sp_error e)
{
  switch(e) {
  case SP_ERROR_BAD_API_VERSION:
  case SP_ERROR_API_INITIALIZATION_FAILED:
  case SP_ERROR_BAD_APPLICATION_KEY:
  case SP_ERROR_APPLICATION_BANNED:
  case SP_ERROR_CLIENT_TOO_OLD:
  case SP_ERROR_OTHER_PERMANENT:
  case SP_ERROR_BAD_USER_AGENT:
  case SP_ERROR_MISSING_CALLBACK:
  case SP_ERROR_INVALID_INDATA:
  case SP_ERROR_UNABLE_TO_CONTACT_SERVER:
    return 1;
  default:
    return 0;
  }
}

/**
 *
 */
static void
spotify_logged_in(sp_session *sess, sp_error error)
{
  pending_login = 0;
  if(error == 0) {
    
    TRACE(TRACE_INFO, "Spotify", "Logged in");

    is_logged_in = 1;

    load_initial_playlists(sess);
#if SPOTIFY_WITH_SOCIAL
    spotify_userinfo_updated(sess);
#endif

  } else {

    const char *msg = f_sp_error_message(error);

    TRACE(TRACE_ERROR, "Spotify", "Failed to login : %s", msg);

    is_logged_in = 0;

    if(is_permanent_error(error)) {
      fail_pending_messages(msg);
    } else {
      spotify_try_login(sess, 1, f_sp_error_message(error), 0);
    }
  }
}

/**
 *
 */
static void
spotify_logged_out(sp_session *sess)
{
  pending_login = 0;
  is_logged_in = 0;
  fail_pending_messages("Logged out");

  if(pending_relogin != NULL) {
    spotify_try_login(sess, 1, pending_relogin, 0);
    pending_relogin = NULL;
  }
}


/**
 *
 */
static void
spotify_connection_error(sp_session *sess, sp_error error)
{
  if(error == SP_ERROR_BAD_USERNAME_OR_PASSWORD)
    return spotify_relogin0("Bad username or password");

  if(error != SP_ERROR_OK)
    notify_add(NULL, NOTIFY_INFO, NULL, 5, 
	       _("Spotify: Connection error: %s"),
	       f_sp_error_message(error));
}


/**
 *
 */
static void
spotify_events_pending(sp_session *sess)
{
  hts_mutex_lock(&spotify_mutex);
  spotify_pending_events = 1;
  hts_cond_signal(&spotify_cond_main);
  hts_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
static void
spotify_play_token_lost(sp_session *sess)
{

#define PMSG "Spotify: Playback paused, another client is using this account"

  if(spotify_mp != NULL)
    mp_enqueue_event(spotify_mp, event_create_str(EVENT_INTERNAL_PAUSE, PMSG));

}



/**
 * 
 */
static void
spotify_make_link(sp_link *link, char *buf, size_t len)
{
  f_sp_link_as_string(link, buf, len);
  f_sp_link_release(link);
}


/**
 *
 */
static void
spotify_end_of_track(sp_session *sess)
{
  media_pipe_t *mp = spotify_mp;

  if(mp != NULL)
    mp_enqueue_event(mp, event_create_type(EVENT_EOF));
}


/**
 *
 */
static int
spotify_music_delivery(sp_session *sess, const sp_audioformat *format, 
		       const void *frames, int num_frames)
{
  media_pipe_t *mp = spotify_mp;
  spotify_uri_t *su = su_playing;
  media_queue_t *mq;
  media_buf_t *mb;

  if(su == NULL || mp == NULL)
    return num_frames;

  mq = &mp->mp_audio;

  if(num_frames == 0) {
    play_position = (int64_t)seek_pos * format->sample_rate / 1000;
    return 0;
  }

  if(mq->mq_packets_current > 100)
    return 0;

  mb = media_buf_alloc_unlocked(mp,  num_frames * 2 * sizeof(int16_t));
  mb->mb_data_type = MB_AUDIO;
  
  mb->mb_channels = format->channels;
  mb->mb_rate = format->sample_rate;

  mb->mb_pts = mb->mb_time = play_position * 1000000LL / format->sample_rate;
  play_position += num_frames;

  memcpy(mb->mb_data, frames, mb->mb_size);

  mb_enqueue_always(mp, mq, mb);
  return num_frames;
}

/**
 *
 */
static void
spotify_play_track_try(void)
{
  spotify_uri_t *su = su_pending;
  sp_error err;

  if(su == NULL)
    return;

  err = f_sp_track_error(su->su_track);

  if(err == SP_ERROR_IS_LOADING) {
    TRACE(TRACE_DEBUG, "spotify", 
	  "Track requested for playback is not loaded, retrying");
    return;
  }

  su_pending = NULL;

  if(err == SP_ERROR_OK)
    err = f_sp_session_player_load(spotify_session, su->su_track);

  if(err != SP_ERROR_OK) {
    snprintf(su->su_errbuf, su->su_errlen, "Spotify: %s",
	     f_sp_error_message(err));
    spotify_uri_return(su, 1);
    return;
  }

  TRACE(TRACE_DEBUG, "spotify", "Starting playback of track: %s (%s)", 
	su->su_uri, f_sp_track_name(su->su_track));

  mp_become_primary(spotify_mp);
  spotify_mp->mp_audio.mq_stream = 0; // Must be set to somthing != -1
  play_position = 0;

  f_sp_session_player_play(spotify_session, 1);

  su_playing = su;
  spotify_uri_return(su, 0);
}


/**
 *
 */
static void
spotify_play_track(spotify_uri_t *su)
{
  sp_link *l;

  if((l = f_sp_link_create_from_string(su->su_uri)) == NULL) {
    snprintf(su->su_errbuf, su->su_errlen, "Invalid spotify URI");
    spotify_uri_return(su, 1);
    return;
  }

  if(f_sp_link_type(l) != SP_LINKTYPE_TRACK) {
    snprintf(su->su_errbuf, su->su_errlen, 
	     "Invalid URI for playback (not a track)");
    spotify_uri_return(su, 1);
    f_sp_link_release(l);
    return;
  }

  su->su_track = f_sp_link_as_track(l);
  f_sp_track_add_ref(su->su_track);
  f_sp_link_release(l);

  assert(su_pending == NULL);
  su_pending = su;

  spotify_play_track_try();
}


/**
 *
 */
static void
set_image_uri(prop_t *p, sp_link *link)
{
  char url[100];

  if(link == NULL)
    return;

  if(f_sp_link_as_string(link, url, sizeof(url)))
    prop_set_string(p, url);
  f_sp_link_release(link);
}


/**
 *
 */
typedef struct track_action_ctrl {
  prop_t *prop_star;
  sp_track *t;

} track_action_ctrl_t;


/**
 *
 */
static void
dispatch_action(track_action_ctrl_t *tac, const char *action)
{
  if(!strcmp(action, "starToggle")) {
    int on = f_sp_track_is_starred(spotify_session, tac->t);
    f_sp_track_set_starred(spotify_session, &tac->t, 1, !on);
    prop_set_int(tac->prop_star, !on);
  } else {
    TRACE(TRACE_DEBUG, "Spotify", "Unknown action '%s' on track", action);
  }
}


/**
 *
 */
static void
track_action_handler(void *opaque, prop_event_t event, ...)
{
  track_action_ctrl_t *tac = opaque;
  va_list ap;
  event_t *e;

  va_start(ap, event);

  switch(event) {
  case PROP_DESTROYED:
    f_sp_track_release(tac->t);
    prop_ref_dec(tac->prop_star);
    free(tac);
    break;

  case PROP_EXT_EVENT:
    e =  va_arg(ap, event_t *);

    if(event_is_type(e, EVENT_ACTION_VECTOR)) {
      event_action_vector_t *eav = (event_action_vector_t *)e;
      int i;
      for(i = 0; i < eav->num; i++)
	dispatch_action(tac, action_code2str(eav->actions[i]));
    
    } else if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
      dispatch_action(tac, e->e_payload);
    }
    break;

  default:
    break;
  }

  va_end(ap);
}


/**
 *
 */
static int
spotify_metadata_update_track(metadata_t *m)
{
  sp_track *track = m->m_source;
  sp_album *album;
  sp_artist *artist;
  char url[URL_MAX];
  int nartists, i;

  if(!f_sp_track_is_loaded(track))
    return -1;

  if(!m->m_static_set) {
    m->m_static_set = 1;

    album = f_sp_track_album(track);
    artist = f_sp_track_artist(track, 0);
  
    prop_set_int(m->m_available, f_sp_track_get_availability(spotify_session, track));

    prop_set_string(m->m_title, f_sp_track_name(track));
    prop_set_int(m->m_trackindex, f_sp_track_index(track));
    prop_set_float(m->m_duration, f_sp_track_duration(track) / 1000.0);

    prop_set_float(m->m_popularity, f_sp_track_popularity(track) / 100.0);

    if(album != NULL) {
      spotify_make_link(f_sp_link_create_from_album(album), url, sizeof(url));
      prop_set_link(m->m_album, f_sp_album_name(album), url);
      set_image_uri(m->m_album_art, f_sp_link_create_from_album_cover(album));
      prop_set_int(m->m_album_year, f_sp_album_year(album));
    }

    // Artists

    if(artist != NULL) {
      spotify_make_link(f_sp_link_create_from_artist(artist), url, sizeof(url));
      prop_set_link(m->m_artist, f_sp_artist_name(artist), url);
    }

    nartists = f_sp_track_num_artists(track);
    if(nartists > 1) {
      for(i = 1; i < nartists; i++) {
	artist = f_sp_track_artist(track, i);
	spotify_make_link(f_sp_link_create_from_artist(artist), url, sizeof(url));
	prop_t *p = prop_create(m->m_additional_artists, url);
	prop_set_link(prop_create(p, "artist"),
		      f_sp_artist_name(artist), url);
      }
    }

    if(artist != NULL) {
      if(f_sp_track_num_artists(track) > 0 && 
	 (artist = f_sp_track_artist(track, 0)) != NULL) {
      
	lastfm_artistpics_init(m->m_artist_images,
			       rstr_alloc(f_sp_artist_name(artist)));
      }
    }
  }
  
  prop_set_int(m->m_starred, f_sp_track_is_starred(spotify_session, track));

  if(m->m_status == NULL)
    return -1;

  const char *status = NULL;
  switch(f_sp_track_offline_get_status(track)) {
  case SP_TRACK_OFFLINE_NO:
    status = NULL;
    break;
  case SP_TRACK_OFFLINE_WAITING:
    status = "waiting";
    break;
  case SP_TRACK_OFFLINE_DOWNLOADING:
    status = "downloading";
    break;
  case SP_TRACK_OFFLINE_DONE:
  case SP_TRACK_OFFLINE_DONE_RESYNC:
    status = "downloaded";
    break;
  case SP_TRACK_OFFLINE_ERROR:
  case SP_TRACK_OFFLINE_DONE_EXPIRED:
  case SP_TRACK_OFFLINE_LIMIT_EXCEEDED:
    status = "error";
    break;
  }
  prop_set_string(m->m_status, status);
  return -1;
}



/**
 *
 */
static void
metadata_ref_dec(metadata_t *m)
{
  prop_ref_dec(m->m_available);
  prop_ref_dec(m->m_title);
  prop_ref_dec(m->m_trackindex);
  prop_ref_dec(m->m_duration);
  prop_ref_dec(m->m_popularity);
  prop_ref_dec(m->m_album);
  prop_ref_dec(m->m_album_art);
  prop_ref_dec(m->m_album_year);
  prop_ref_dec(m->m_artist);
  prop_ref_dec(m->m_additional_artists);
  prop_ref_dec(m->m_artist_images);
  prop_ref_dec(m->m_starred);
  prop_ref_dec(m->m_status);
}


/**
 *
 */
static void
metadata_destroy(metadata_t *m)
{
  prop_unsubscribe(m->m_sub);
  metadata_ref_dec(m);
  LIST_REMOVE(m, m_link);
  f_sp_track_release(m->m_source);
  free(m);
}


/**
 *
 */
static void
spotify_metadata_updated(sp_session *sess)
{
  spotify_play_track_try();
  spotify_try_pending();
}


/**
 *
 */
static void
spotify_metadata_list_update(sp_session *sess, struct metadata_list *l)
{
  metadata_t *m, *n;

  for(m = LIST_FIRST(l); m != NULL; m = n) {
    n = LIST_NEXT(m, m_link);

    if(!spotify_metadata_update_track(m))
      metadata_destroy(m);
  }
}


/**
 *
 */
static void
spotify_metadata_list_clear(struct metadata_list *l)
{
  metadata_t *m;

  while((m = LIST_FIRST(l)) != NULL) 
    metadata_destroy(m);
}



/**
 *
 */
static void
metadata_prop_cb(void *opaque, prop_event_t event, ...)
{
  if(event == PROP_DESTROYED) 
    metadata_destroy(opaque);
}


static void
metadata_create(prop_t *p, sp_track *source, struct metadata_list *list,
		int with_status)
{
  metadata_t *m = calloc(1, sizeof(metadata_t));

  m->m_source = source;

  m->m_available         = prop_ref_inc(prop_create(p, "available"));
  m->m_title             = prop_ref_inc(prop_create(p, "title"));
  m->m_trackindex        = prop_ref_inc(prop_create(p, "trackindex"));
  m->m_duration          = prop_ref_inc(prop_create(p, "duration"));
  m->m_popularity        = prop_ref_inc(prop_create(p, "popularity"));
  m->m_album             = prop_ref_inc(prop_create(p, "album"));
  m->m_album_art         = prop_ref_inc(prop_create(p, "album_art"));
  m->m_album_year        = prop_ref_inc(prop_create(p, "album_year"));
  m->m_artist            = prop_ref_inc(prop_create(p, "artist"));
  m->m_additional_artists= prop_ref_inc(prop_create(p, "additional_artists"));
  m->m_artist_images     = prop_ref_inc(prop_create(p, "artist_images"));
  m->m_starred           = prop_ref_inc(prop_create(p, "starred"));
  if(with_status)
    m->m_status          = prop_ref_inc(prop_create(p, "status"));

  if(!spotify_metadata_update_track(m)) {
    metadata_ref_dec(m);
    free(m);
    return;
  }

  if(list == NULL) {
    metadata_ref_dec(m);
    free(m);
    return;
  }

  f_sp_track_add_ref(source);
  LIST_INSERT_HEAD(list, m, m_link);
  m->m_sub = prop_subscribe(PROP_SUB_TRACK_DESTROY,
			    PROP_TAG_CALLBACK, metadata_prop_cb, m,
			    PROP_TAG_COURIER, spotify_courier,
			    PROP_TAG_ROOT, p,
			    NULL);
}


/**
 *
 */
static prop_t *
track_create(sp_track *track, prop_t **metadatap,
	     struct metadata_list *list, int with_status)
{
  char url[URL_MAX];
  prop_t *p = prop_create_root(NULL);
  prop_t *metadata;
  track_action_ctrl_t *tac = calloc(1, sizeof(track_action_ctrl_t));

  spotify_make_link(f_sp_link_create_from_track(track, 0), url, sizeof(url));
  prop_set_string(prop_create(p, "url"), url);
  prop_set_string(prop_create(p, "type"), "audio");

  metadata = prop_create(p, "metadata");
  if(metadatap != NULL)
    *metadatap = metadata;

  metadata_create(metadata, track, list, with_status);

  tac->prop_star = prop_ref_inc(prop_create(metadata, "starred"));

  tac->t = track;
  f_sp_track_add_ref(track);
  prop_subscribe(PROP_SUB_TRACK_DESTROY,
		 PROP_TAG_CALLBACK, track_action_handler, tac,
		 PROP_TAG_COURIER, spotify_courier,
		 PROP_TAG_ROOT, p,
		 NULL);
  return p;
}


/**
 *
 */
typedef struct browse_helper {
  spotify_page_t *sp;
  char *playme;
} browse_helper_t;


/**
 *
 */
static void
bh_free(browse_helper_t *bh)
{
  spotify_page_destroy(bh->sp);
  free(bh->playme);
  free(bh);
}


static void
bh_error(browse_helper_t *bh, const char *err)
{
  prop_set_string(bh->sp->sp_type, "openerror");
  // XXX(l10n): Missing
  prop_set_string(bh->sp->sp_error, err);
}


/**
 *
 */
static browse_helper_t *
bh_create(spotify_page_t *sp, const char *playme)
{
  struct prop_nf *pnf;
  browse_helper_t *bh = calloc(1, sizeof(browse_helper_t));

  prop_set_string(sp->sp_type, "directory");
  
  bh->sp = sp;
  pnf = prop_nf_create(sp->sp_nodes, sp->sp_items, sp->sp_filter, NULL,
		       PROP_NF_AUTODESTROY);
  prop_set_int(sp->sp_canFilter, 1);
  prop_nf_release(pnf);

  bh->playme = playme != NULL ? strdup(playme) : NULL;
  return bh;
}



/**
 * Load an album into the property tree passed via userdata
 */
static void
spotify_browse_album_callback(sp_albumbrowse *result, void *userdata)
{
  browse_helper_t *bh = userdata;
  prop_t *p;
  sp_track *track;
  int i, ntracks;
  
  if(f_sp_albumbrowse_error(result)) {
    bh_error(bh, "Album not found");
  } else {
    sp_track *playme = NULL;

    sp_album *alb = f_sp_albumbrowse_album(result);

    prop_set_string(bh->sp->sp_title, f_sp_album_name(alb));
    prop_set_string(bh->sp->sp_album_name, f_sp_album_name(alb));
    int y = f_sp_album_year(alb);
    if(y)
      prop_set_int(bh->sp->sp_album_year, y);

    set_image_uri(bh->sp->sp_icon, f_sp_link_create_from_album_cover(alb));
    set_image_uri(bh->sp->sp_album_art, f_sp_link_create_from_album_cover(alb));

    prop_set_string(bh->sp->sp_artist_name,
		    f_sp_artist_name(f_sp_album_artist(alb)));

    if(bh->playme) {
      sp_link *l;
      if((l = f_sp_link_create_from_string(bh->playme)) != NULL) {
	if(f_sp_link_type(l) == SP_LINKTYPE_TRACK) {
	  playme = f_sp_link_as_track(l);
	  f_sp_track_add_ref(playme);
	}
	f_sp_link_release(l);
      }
    }

    ntracks = f_sp_albumbrowse_num_tracks(result);

    prop_vec_t *pv = prop_vec_create(ntracks);

    for(i = 0; i < ntracks; i++) {
      track = f_sp_albumbrowse_track(result, i);
      p = track_create(track, NULL, NULL, 0);

      pv = prop_vec_append(pv, p);

      if(track == playme)
	playqueue_load_with_source(p, bh->sp->sp_model, 0);
    }

    prop_set_parent_vector(pv, bh->sp->sp_items, NULL, NULL);
    prop_vec_release(pv);

    spotify_metadata_updated(spotify_session);

    if(playme != NULL)
      f_sp_track_release(playme);

  }
  f_sp_albumbrowse_release(result);
  prop_set_int(bh->sp->sp_loading, 0);
  bh_free(bh);
}


/**
 * Helper struct for artist browse
 */
typedef struct album {
  sp_album *album;
  sp_albumtype type;
} album_t;


/**
 *
 */
static int
album_cmp(const void *A, const void *B)
{
  const album_t *a = A;
  const album_t *b = B;

  if(a->type < b->type)
    return -1;
  if(a->type > b->type)
    return 1;
  
  if(f_sp_album_year(a->album) > f_sp_album_year(b->album))
    return -1;
  if(f_sp_album_year(a->album) < f_sp_album_year(b->album))
    return 1;

  return strcasecmp(f_sp_album_name(a->album), f_sp_album_name(b->album));
}



/**
 *
 */
static sp_albumtype
my_album_type(sp_album *alb, sp_artist *a0)
{
  return f_sp_album_artist(alb) != a0 ?
    SP_ALBUMTYPE_COMPILATION : f_sp_album_type(alb);
}


static void
spotify_add_album(sp_album *album, sp_artist *artist, prop_t *parent)
{
  prop_t *metadata;
  prop_t *p = prop_create_root(NULL);
  char link[200];

  spotify_make_link(f_sp_link_create_from_album(album), link, sizeof(link));
  prop_set_string(prop_create(p, "url"), link);
  prop_set_string(prop_create(p, "type"), "album");
  
  metadata = prop_create(p, "metadata");
  prop_set_string(prop_create(metadata, "title"), f_sp_album_name(album));
  
  spotify_make_link(f_sp_link_create_from_artist(artist), link, sizeof(link));
  prop_set_link(prop_create(metadata, "artist"),
		f_sp_artist_name(artist), link);
  
  set_image_uri(prop_create(metadata, "album_art"),
		f_sp_link_create_from_album_cover(album));
  
  if(prop_set_parent(p, parent))
    prop_destroy(p);
}


/**
 *
 */
static void
spotify_browse_artist_callback(sp_artistbrowse *result, void *userdata)
{
  browse_helper_t *bh = userdata;
  int nalbums = 0, i, j;
  sp_album *album;
  sp_artist *artist;
  album_t *av;
  album_t *last = NULL;

  if(f_sp_artistbrowse_error(result)) {
    bh_error(bh, "Artist not found");
  } else {

    // libspotify does not return the albums in any particular order.
    // thus, we need to do some sorting and filtering

    nalbums = f_sp_artistbrowse_num_albums(result);
    artist = f_sp_artistbrowse_artist(result);
    prop_set_string(bh->sp->sp_title, f_sp_artist_name(artist));

    av = malloc(nalbums * sizeof(album_t));
    j = 0;

    for(i = 0; i < nalbums; i++) {
      album = f_sp_artistbrowse_album(result, i);
      
      if(!f_sp_album_is_available(album))
	continue;

      av[j].type = my_album_type(album, artist);
      av[j++].album = album;
    }

    qsort(av, j, sizeof(album_t), album_cmp);

    for(i = 0; i < j; i++) {
      album_t *a = av + i;

      if(last != NULL && !strcmp(f_sp_album_name(a->album),
				 f_sp_album_name(last->album)))
	continue;
      spotify_add_album(a->album, artist, bh->sp->sp_items);
      last = a;
    }
    free(av);

    spotify_metadata_updated(spotify_session);
  }

  f_sp_artistbrowse_release(result);
  prop_set_int(bh->sp->sp_loading, 0);
  bh_free(bh);
}


/**
 *
 */
static void
spotify_open_artist(sp_link *l, spotify_page_t *sp)
{
  sp_artist *artist = f_sp_link_as_artist(l);
  prop_set_string(sp->sp_contents, "items");

  f_sp_artistbrowse_create(spotify_session, artist,
			   SP_ARTISTBROWSE_NO_TRACKS,
			   spotify_browse_artist_callback,
			   bh_create(sp, NULL));
}


/**
 *
 */
static void
spotify_open_rootlist(spotify_page_t *sp, int flat)
{
  struct prop_nf *pnf;

  prop_set_string(sp->sp_type, "directory");
  prop_link(_p("Spotify playlists"), sp->sp_title);
  prop_link(current_user_rootlist->plc_pending, sp->sp_loading);

  pnf = prop_nf_create(sp->sp_nodes,
		       flat ? current_user_rootlist->plc_root_flat :
		       current_user_rootlist->plc_root_tree,
		       sp->sp_filter, NULL, PROP_NF_AUTODESTROY);
  prop_set_int(sp->sp_canFilter, 1);
  prop_nf_release(pnf);
}


#if SPOTIFY_WITH_SOCIAL
/**
 *
 */
static void
spotify_open_friends(spotify_page_t *sp)
{
  struct prop_nf *pnf;

  prop_set_string(sp->sp_type, "directory");
  prop_link(_p("Spotify friends"), sp->sp_title);
  prop_set_int(sp->sp_loading, 0);

  pnf = prop_nf_create(sp->sp_nodes, friend_nodes,
		       sp->sp_filter, NULL, PROP_NF_AUTODESTROY);
  prop_set_int(sp->sp_canFilter, 1);
  prop_nf_release(pnf);
}
#endif

/**
 *
 */
static void
spotify_open_search_done(sp_search *result, void *userdata)
{
  spotify_page_t *sp = userdata;

  parse_search_reply(result, sp->sp_nodes, sp->sp_contents);

  prop_set_string(sp->sp_type, "directory");
  prop_set_int(sp->sp_loading, 0);
  spotify_page_destroy(sp);
}

/**
 *
 */
static int
spotify_open_search(spotify_page_t *sp, const char *query)
{
  if(!strcmp(query, "tag:new")) {
    prop_link(_p("New albums on Spotify"), sp->sp_title);
  } else {
    prop_link(_p("Search result"), sp->sp_title);
  }

  return f_sp_search_create(spotify_session, query,
			    0, 250, 0, 250, 0, 250, 
			    spotify_open_search_done, sp) == NULL;
}

/**
 *
 */
static void
spotify_open_album(sp_album *alb, spotify_page_t *sp, const char *playme)
{
  f_sp_albumbrowse_create(spotify_session, alb, spotify_browse_album_callback,
			  bh_create(sp, playme));

  prop_set_string(sp->sp_contents, "albumTracks");
}


/**
 *
 */
static void
spotify_open_playlist(spotify_page_t *sp, sp_playlist *plist, const char *name,
		      int flags)
{
  pl_create(plist, name,
	    sp->sp_model,
	    sp->sp_loading,
	    sp->sp_type,
	    sp->sp_title,
	    sp->sp_icon,
	    sp->sp_canDelete,
	    sp->sp_urlprop,
	    sp->sp_numtracks,
	    sp->sp_nodes,
	    sp->sp_items,
	    sp->sp_filter,
	    sp->sp_canFilter,
	    sp->sp_user,
	    flags | PL_WITH_TRACKS);
}


/**
 *
 */  
static void
try_get_album(spotify_page_t *sp)
{
  sp_error err = f_sp_track_error(sp->sp_track);
  sp_album *alb;
  char aurl[URL_MAX];

  if(err == SP_ERROR_IS_LOADING) {
    TRACE(TRACE_DEBUG, "spotify", 
	  "Track requested for album resolve is not loaded, retrying");
    return;
  }

  LIST_REMOVE(sp, sp_query_link);

  if(err != SP_ERROR_OK) {
    spotify_open_page_fail(sp,  f_sp_error_message(err));
    return;
  }

  alb = f_sp_track_album(sp->sp_track);
  spotify_make_link(f_sp_link_create_from_album(alb), aurl, sizeof(aurl));
  prop_set_string(sp->sp_urlprop, aurl);
  spotify_open_album(alb, sp, sp->sp_url);
}



/**
 *
 */  
static void
try_resolve_track_item(spotify_page_t *sp)
{
  sp_error err = f_sp_track_error(sp->sp_track);
  sp_album *album;
  char url[URL_MAX];

  if(err == SP_ERROR_IS_LOADING)
    return;

  LIST_REMOVE(sp, sp_query_link);

  if(err != SP_ERROR_OK)
    return;
  prop_set_string(sp->sp_type, "audio");

  prop_set_string(sp->sp_title, f_sp_track_name(sp->sp_track));

  album = f_sp_track_album(sp->sp_track);
  if(album != NULL) {
    spotify_make_link(f_sp_link_create_from_album(album), url, sizeof(url));
    prop_set_link(sp->sp_album_name, f_sp_album_name(album), url);
    set_image_uri(sp->sp_album_art, f_sp_link_create_from_album_cover(album));
    prop_set_int(sp->sp_album_year, f_sp_album_year(album));
  }

  spotify_page_destroy(sp);
}



/**
 * Fill sp->sp_root with info from sp->sp_url
 */
static void
spotify_open_page(spotify_page_t *sp)
{
  sp_link *l;
  sp_linktype type;
  sp_playlist *plist;
  sp_user *user;

  if(!strcmp(sp->sp_url, "spotify:playlists")) {
    spotify_open_rootlist(sp, 0);
  } else if(!strcmp(sp->sp_url, "spotify:playlistsflat")) {
    spotify_open_rootlist(sp, 1);
#if SPOTIFY_WITH_SOCIAL
  } else if(!strcmp(sp->sp_url, "spotify:friends")) {
    spotify_open_friends(sp);
#endif
  } else if(!strcmp(sp->sp_url, "spotify:starred")) {
    
    plist = f_sp_session_starred_create(spotify_session);
    if(plist != NULL) 
      spotify_open_playlist(sp, plist, "Starred tracks", PL_SORT_ON_TIME);

  } else if(!strcmp(sp->sp_url, "spotify:inbox")) {
    
    plist = f_sp_session_inbox_create(spotify_session);
    if(plist != NULL) 
      spotify_open_playlist(sp, plist, "Inbox", PL_MESSAGES | PL_SORT_ON_TIME);

  } else if(!strncmp(sp->sp_url, "spotify:search:",
		     strlen("spotify:search:"))) {
    if(!spotify_open_search(sp, sp->sp_url + strlen("spotify:search:")))
      return;
  } else {
    if((l = f_sp_link_create_from_string(sp->sp_url)) == NULL) {
      spotify_open_page_fail(sp, "Invalid Spotify URI");
      return;
    }

    type = f_sp_link_type(l);

    switch(type) {
    case SP_LINKTYPE_ALBUM:
      spotify_open_album(f_sp_link_as_album(l), sp, NULL);
      sp = NULL;
      break;

    case SP_LINKTYPE_ARTIST:
      spotify_open_artist(l, sp);
      sp = NULL;
      break;

    case SP_LINKTYPE_PLAYLIST:
      plist = f_sp_playlist_create(spotify_session, l);
      if(plist != NULL) 
	spotify_open_playlist(sp, plist, NULL, 0);
      break;

    case SP_LINKTYPE_TRACK:
      sp->sp_track = f_sp_link_as_track(l);
      f_sp_track_add_ref(sp->sp_track);
      LIST_INSERT_HEAD(&pending_album_queries, sp, sp_query_link);
      try_get_album(sp);
      sp = NULL;
      break;

    case SP_LINKTYPE_PROFILE:
      user = f_sp_link_as_user(l);
      plc_for_user(spotify_session, sp, f_sp_user_canonical_name(user));
      break;

    default:
      spotify_open_page_fail(sp, "Unable to handle URI");
      sp = NULL;
      break;
    }
    f_sp_link_release(l);
  }
  if(sp != NULL)
    spotify_page_destroy(sp);
}


/**
 *
 */
static void
spotify_resolve_item(spotify_page_t *sp)
{
  sp_link *l;
  sp_linktype type;

  if((l = f_sp_link_create_from_string(sp->sp_url)) == NULL) {
    spotify_open_page_fail(sp, "Invalid Spotify URI");
    return;
  }

  type = f_sp_link_type(l);

  switch(type) {
  case SP_LINKTYPE_TRACK:
    sp->sp_track = f_sp_link_as_track(l);
    f_sp_track_add_ref(sp->sp_track);
    LIST_INSERT_HEAD(&pending_track_item_resolve, sp, sp_query_link);
    try_resolve_track_item(sp);
    break;
    
  default:
    break;
  }
  f_sp_link_release(l);
}



/**
 *
 */
static void
parse_search_reply(sp_search *result, prop_t *nodes, prop_t *contents)
{
  int i, nalbums, ntracks, nartists;
  sp_album *album, *album_prev = NULL;
  sp_artist *artist;

  nalbums  = f_sp_search_num_albums(result);
  nartists = f_sp_search_num_artists(result);
  ntracks  = f_sp_search_num_tracks(result);

  /**
   *
   */
  for(i = 0; i < nalbums; i++) {
    album = f_sp_search_album(result, i);
    artist = f_sp_album_artist(album);

    
    if(album_prev != NULL) {
      // Skip dupes
      if(f_sp_album_artist(album_prev) == artist &&
	 !strcmp(f_sp_album_name(album), f_sp_album_name(album_prev)))
	continue; 
    }

    spotify_add_album(album, artist, nodes);
    album_prev = album;
  }

  if(contents != NULL) {
    if(nalbums && nartists == 0 && ntracks == 0)
      prop_set_string(contents, "albums");
  }

  f_sp_search_release(result);
}


/**
 *
 */
static void
spotify_log_message(sp_session *session, const char *msg)
{
  int l = strlen(msg);
  if(l > 512)
    l = 512;
  char *s = alloca(l + 1);
  memcpy(s, msg, l);
  s[l] = 0;

  if(l > 0 && s[l - 1] == '\n')
    s[l - 1] = 0;
  TRACE(TRACE_DEBUG, "libspotify", "%s", s);
}

/**
 *
 */
static void
update_userdata(spotify_user_t *su)
{
  char url[200];
  const char *name;
#if SPOTIFY_WITH_SOCIAL
  name = f_sp_user_full_name(su->su_user);
  if(name == NULL)
    name = f_sp_user_display_name(su->su_user);
  if(name == NULL)
#endif
    name = f_sp_user_canonical_name(su->su_user);

  sp_link *l = f_sp_link_create_from_user(su->su_user);
  f_sp_link_as_string(l, url, sizeof(url));

  if(su->su_prop_title != NULL)
    prop_set_string(su->su_prop_title, name);

  if(su->su_prop_url != NULL)
    prop_set_string(su->su_prop_url, url);

  prop_set_link(su->su_prop_name, name, url);
  f_sp_link_release(l);

#if SPOTIFY_WITH_SOCIAL
  prop_set_string(su->su_prop_picture, f_sp_user_picture(su->su_user));
#endif
}


/**
 *
 */
static spotify_user_t *
find_user(sp_user *u)
{
  spotify_user_t *su;

  LIST_FOREACH(su, &spotify_users, su_link) {
    if(su->su_user == u) {
      LIST_REMOVE(su, su_link);
      break;
    }
  }

  if(su == NULL) {
    su = calloc(1, sizeof(spotify_user_t));
    f_sp_user_add_ref(u);
    su->su_user = u;

    su->su_prop = prop_create_root(NULL);
    su->su_prop_name = prop_create(su->su_prop, "name");
    su->su_prop_picture = prop_create(su->su_prop, "picture");

    update_userdata(su);
  }
  LIST_INSERT_HEAD(&spotify_users, su, su_link);
  return su;
}


/**
 *
 */
static void
clear_friend(spotify_user_t *su)
{
  if(su->su_prop_friend) {
    prop_destroy(su->su_prop_friend);
    su->su_prop_friend = NULL;
    su->su_prop_title = NULL;
    su->su_prop_url = NULL;
  }
}


#if SPOTIFY_WITH_SOCIAL
/**
 *
 */
static void
spotify_userinfo_updated(sp_session *session)
{
  spotify_user_t *su;
  prop_t *before = NULL;
  int num_friends = f_sp_session_num_friends(session);
  int i;

  LIST_FOREACH(su, &spotify_users, su_link) {
    su->su_mark = 1;
    update_userdata(su);
  }

  for(i = num_friends - 1; i >= 0; i--) {
    spotify_user_t *su = find_user(f_sp_session_friend(session, i));
    su->su_mark = 0;

    if(su->su_prop_friend == NULL) {
      su->su_prop_friend = prop_create_root(NULL);
      su->su_prop_url = prop_create(su->su_prop_friend, "url");
      prop_set_string(prop_create(su->su_prop_friend, "type"), "person");
      prop_t *metadata = prop_create(su->su_prop_friend, "metadata");
      
      su->su_prop_title = prop_create(metadata, "title");
      prop_link(su->su_prop_picture, prop_create(metadata, "picture"));
    }
    update_userdata(su);

    if(prop_set_parent_ex(su->su_prop_friend, friend_nodes, before, NULL))
      abort();
    before = su->su_prop_friend;
  }

  LIST_FOREACH(su, &spotify_users, su_link) {
    if(su->su_mark)
      clear_friend(su);
    su->su_mark = 0;
  }
}
#endif

/**
 *
 */
static void
clear_friends(void)
{
  spotify_user_t *su;
  LIST_FOREACH(su, &spotify_users, su_link)
    clear_friend(su);
}


/**
 *
 */
static void
spotify_streaming_error(sp_session *session, sp_error error)
{
  media_pipe_t *mp = spotify_mp;

  TRACE(TRACE_ERROR, "Spotify", "Unable to play track -- %s", 
	f_sp_error_message(error));

  if(mp != NULL)
    mp_enqueue_event(mp, event_create_type(EVENT_EOF));
}

#if SPOTIFY_WITH_SOCIAL
#include "networking/http.h"  // ugly
struct htsbuf_queue;

#define HTTP_DISABLE_AUTH  0x1
#define HTTP_REQUEST_DEBUG 0x2

int http_request(const char *url, const char **arguments, 
		 char **result, size_t *result_sizep,
		 char *errbuf, size_t errlen,
		 struct htsbuf_queue *postdata, const char *postcontenttype,
		 int flags, struct http_header_list *headers_out,
		 const struct http_header_list *headers_in, const char *method);


static void spotify_perform_http(sp_session *session, const char *url,
				 const void *postdata, size_t postlen,
				 void *opaque)
{
  char errbuf[256];

  char *result;
  size_t size;
  int r;
  htsbuf_queue_t *postdataq = NULL;

  if(postdata != NULL) {
    htsbuf_queue_t hq;
    htsbuf_queue_init(&hq, 0);
    htsbuf_append(&hq, postdata, postlen);
    postdataq = &hq;
  }
  
  r = http_request(url, NULL, &result, &size, errbuf, sizeof(errbuf), postdataq, NULL, 0, NULL, NULL, NULL);

  if(postdataq != NULL)
    htsbuf_queue_flush(postdataq);

  if(r == 0) {
    f_sp_http_perfomed(session, opaque, result, size, SP_ERROR_OK, 200);
  } else {
    f_sp_http_perfomed(session, opaque, result, size, SP_ERROR_OTHER_PERMANENT, 0);
  }
}
#endif


/**
 *
 */
static void
playlist_update_offline(sp_session *session, playlist_t *pl)
{
  sp_playlist_offline_status s;
  //  const char *status = NULL;
  s = f_sp_playlist_get_offline_status(session, pl->pl_playlist);

  prop_set_int_ex(prop_create(pl->pl_prop_offline, "value"),
		  pl->pl_offline_sub, !!s);

#if 0    
  switch(s) {
  case SP_PLAYLIST_OFFLINE_STATUS_NO:
    break;
  case SP_PLAYLIST_OFFLINE_STATUS_YES:
    status = "synchronized";
    break;

  case SP_PLAYLIST_OFFLINE_STATUS_DOWNLOADING:
    status = "downloading";
    f_sp_playlist_get_offline_download_completed(session,
						 pl->pl_playlist);
    break;

  case SP_PLAYLIST_OFFLINE_STATUS_WAITING:
    status = "waiting";
    break;
  }
  //  prop_set_string(pl->pl_prop_offline_status, status);
  //  prop_set_int(pl->pl_prop_offline_percentage, p);
#endif
}


/**
 *
 */
static void
spotify_offline_status_updated(sp_session *session)
{
  playlist_t *pl;
  LIST_FOREACH(pl, &playlists, pl_link)
    if(pl->pl_prop_offline != NULL)
      playlist_update_offline(session, pl);
}


/**
 *
 */
static void
spotify_offline_error(sp_session *session, sp_error error)
{
  if(error == SP_ERROR_OK) {
    return;
  } else {
    TRACE(TRACE_ERROR, "Spotify", "Offline error: %s",
	  f_sp_error_message(error));
  }
}

/**
 * Session callbacks
 */
static const sp_session_callbacks spotify_session_callbacks = {
  .logged_in           = spotify_logged_in,
  .logged_out          = spotify_logged_out,
  .connection_error    = spotify_connection_error,
  .metadata_updated    = spotify_metadata_updated,
  .notify_main_thread  = spotify_events_pending,
  .music_delivery      = spotify_music_delivery,
  .play_token_lost     = spotify_play_token_lost,
  .end_of_track        = spotify_end_of_track,
  .log_message         = spotify_log_message,
  .streaming_error     = spotify_streaming_error,
  .offline_status_updated = spotify_offline_status_updated,
  .offline_error       = spotify_offline_error,

#if SPOTIFY_WITH_SOCIAL
  .userinfo_updated    = spotify_userinfo_updated,
  .perform_http        = spotify_perform_http,
#endif
};



/**
 *
 */
static prop_t *
pl_add_track(playlist_t *pl, sp_track *t, int pos)
{
  playlist_item_t *pli = calloc(1, sizeof(playlist_item_t));

  if(f_sp_track_is_placeholder(t)) {
    char url[URL_MAX];
    spotify_make_link(f_sp_link_create_from_track(t,0), url, sizeof(url));

    pli->pli_prop_root = prop_create_root(NULL);
    prop_set_string(prop_create(pli->pli_prop_root, "url"), url);
    prop_set_string(prop_create(pli->pli_prop_root, "type"), "directory");
    pli->pli_prop_metadata = prop_create(pli->pli_prop_root, "metadata");
    prop_set_string(prop_create(pli->pli_prop_metadata, "title"), url);
    prop_set_int(prop_create(pli->pli_prop_metadata, "available"), 1);

  } else {

    pli->pli_prop_root = track_create(t, &pli->pli_prop_metadata,
				      &pl->pl_pending_metadata, 1);

  }
  sp_user *u = f_sp_playlist_track_creator(pl->pl_playlist, pos);
  
  if(u != NULL) {
    spotify_user_t *su = find_user(u);
    prop_link(su->su_prop, prop_create(pli->pli_prop_metadata, "user"));
  }

  int when = f_sp_playlist_track_create_time(pl->pl_playlist, pos);
  if(when > 1)
    prop_set_int(prop_create(pli->pli_prop_metadata, "timestamp"), when);
  
  if(pl->pl_flags & PL_MESSAGES) {
    const char *msg = f_sp_playlist_track_message(pl->pl_playlist, pos);
    if(msg != NULL)
      prop_set_string(prop_create(pli->pli_prop_metadata, "message"), msg);
  }
  ptrvec_insert_entry(&pl->pl_tracks, pos, pli);
  return pli->pli_prop_root;
}


/**
 *
 */
static void 
tracks_added(sp_playlist *plist, sp_track * const * tracks,
	     int num_tracks, int position, void *userdata)
{
  playlist_t *pl = userdata;
  playlist_item_t *before;
  int i;
  prop_vec_t *pv = prop_vec_create(num_tracks);

  before = ptrvec_get_entry(&pl->pl_tracks, position);

  for(i = 0; i < num_tracks; i++)
    pv = prop_vec_append(pv, pl_add_track(pl, tracks[i], position + i));
  
  prop_set_parent_vector(pv, pl->pl_prop_tracks, 
			 before ? before->pli_prop_root : NULL,
			 pl->pl_node_sub);

  prop_vec_release(pv);
  
  prop_set_int(pl->pl_prop_num_tracks, f_sp_playlist_num_tracks(plist));
}


/**
 *
 */
static void 
tracks_added_simple(sp_playlist *plist, sp_track * const * tracks,
		    int num_tracks, int position, void *userdata)
{
  playlist_t *pl = userdata;
  prop_set_int(pl->pl_prop_num_tracks, f_sp_playlist_num_tracks(plist));
}



/**
 *
 */
static int
intcmp_dec(const void *p1, const void *p2)
{
  return *(int *)p2 - *(int *)p1;
}

/**
 *
 */
static void
tracks_removed(sp_playlist *plist, const int *tracks,
	       int num_tracks, void *userdata)
{
  int *positions;
  playlist_t *pl = userdata;
  playlist_item_t *pli;
  int i;

  /* Sort so we always delete from the end. Better safe then sorry */
  positions = alloca(num_tracks * sizeof(int));
  memcpy(positions, tracks, sizeof(int) * num_tracks);
  qsort(positions, num_tracks, sizeof(int), intcmp_dec);

  for(i = 0; i < num_tracks; i++) {
    pli = ptrvec_remove_entry(&pl->pl_tracks, positions[i]);
    prop_destroy(pli->pli_prop_root);
    free(pli);
  }
  prop_set_int(pl->pl_prop_num_tracks, f_sp_playlist_num_tracks(plist));
}


/**
 *
 */
static void
tracks_removed_simple(sp_playlist *plist, const int *tracks,
	       int num_tracks, void *userdata)
{
  playlist_t *pl = userdata;
  prop_set_int(pl->pl_prop_num_tracks, f_sp_playlist_num_tracks(plist));
}


/**
 *
 */
static void
tracks_moved(sp_playlist *plist, const int *tracks,
	     int num_tracks, int new_position, void *userdata)
{
  playlist_t *pl = userdata;
  int i;
  int *positions;
  playlist_item_t *pli, *before, **vec;

  /* Sort so we always delete from the end. Better safe then sorry */
  positions = alloca(num_tracks * sizeof(int));
  memcpy(positions, tracks, sizeof(int) * num_tracks);
  qsort(positions, num_tracks, sizeof(int), intcmp_dec);

  before = ptrvec_get_entry(&pl->pl_tracks, new_position);
  vec = alloca(num_tracks * sizeof(playlist_item_t *));

  for(i = 0; i < num_tracks; i++) {
    vec[num_tracks-1-i] = ptrvec_remove_entry(&pl->pl_tracks, positions[i]);
    if(positions[i] < new_position)
      new_position--;
  }
  for(i = num_tracks - 1; i >= 0; i--) {
    pli = vec[i];
    before = ptrvec_get_entry(&pl->pl_tracks, new_position);
    ptrvec_insert_entry(&pl->pl_tracks, new_position, pli);

    if(pli->pli_prop_root != NULL)
      prop_move(pli->pli_prop_root, before ? before->pli_prop_root : NULL);
  }
}


/**
 *
 */
static void
track_update_created(sp_playlist *playlist, int position, sp_user *user,
		     int when, void *userdata)
{
  playlist_t *pl = userdata;
  playlist_item_t *pli = ptrvec_get_entry(&pl->pl_tracks, position);
  spotify_user_t *su = find_user(user);
  prop_link(su->su_prop, prop_create(pli->pli_prop_metadata, "user"));
  if(when > 0)
    prop_set_int(prop_create(pli->pli_prop_metadata, "timestamp"), when);
}



/**
 *
 */
static const char *
playlist_name_update(sp_playlist *plist, playlist_t *pl)
{
  const char *name = f_sp_playlist_name(plist);

  if(pl->pl_prop_title == NULL)
    return NULL;
  prop_set_string(pl->pl_prop_title, name);

  char buf[200];

  snprintf(buf, sizeof(buf),
	   "%s 0x%016"PRId64, name, pl->pl_folder_id);
  return name;
}

/**
 *
 */
static void 
playlist_renamed(sp_playlist *plist, void *userdata)
{
  const char *name = playlist_name_update(plist, userdata);
  if(name)
    TRACE(TRACE_DEBUG, "spotify", "Playlist renamed to %s", name);
}


/**
 *
 */
static void
playlist_update_meta(playlist_t *pl)
{
  sp_playlist *plist = pl->pl_playlist;
  sp_user *owner = f_sp_playlist_owner(pl->pl_playlist);
  const int ownedself = owner == f_sp_session_user(spotify_session);

  int colab = f_sp_playlist_is_collaborative(pl->pl_playlist);

  prop_set_int(pl->pl_prop_canDelete, ownedself || colab);

  if(f_sp_playlist_is_loaded(plist)) {
    sp_link *l = f_sp_link_create_from_playlist(plist);
    
    if(l != NULL) {
      char url[URL_MAX];
      spotify_make_link(l, url, sizeof(url));
      prop_set_string(pl->pl_prop_url, url);
      prop_set_int_ex(prop_create(pl->pl_prop_collab, "value"),
		      pl->pl_collab_sub, colab);
    }
  }

  if(!ownedself) {
    spotify_user_t *su = find_user(owner);
    prop_link(su->su_prop, pl->pl_prop_user);
  }
}


/**
 *
 */
static void 
playlist_state_changed(sp_playlist *plist, void *userdata)
{
  playlist_t *pl = userdata;
  playlist_update_meta(pl);

  if(pl->pl_prop_offline != NULL)
    playlist_update_offline(spotify_session, pl);
}

/**
 *
 */
static void
playlist_metadata_updated(sp_playlist *plist, void *userdata)
{
  playlist_t *pl = userdata;
  spotify_metadata_list_update(spotify_session, &pl->pl_pending_metadata);
}


/**
 *
 */
static void
playlist_set_image(playlist_t *pl, const byte *b)
{
  char uri[80];
  if(b == NULL) {
    prop_set_void(pl->pl_prop_icon);
    return;
  }
  snprintf(uri, sizeof(uri), "spotify:image:"
	   "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
	   "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
	   b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],
	   b[10],b[11],b[12],b[13],b[14],b[15],b[16],b[17],b[18],b[19]);
  prop_set_string(pl->pl_prop_icon, uri);
}


/**
 *
 */
static void
playlist_image_changed(sp_playlist *plist, const byte *image, void *userdata)
{
  playlist_t *pl = userdata;
  playlist_set_image(pl, image);
}


/**
 * Callbacks for individual playlists
 */
static sp_playlist_callbacks pl_callbacks = {
  .tracks_added     = tracks_added_simple,
  .tracks_removed   = tracks_removed_simple,
  .playlist_renamed = playlist_renamed,
  .image_changed    = playlist_image_changed,
  .playlist_state_changed = playlist_state_changed,
};


/**
 * Callbacks for individual playlists
 */
static sp_playlist_callbacks pl_callbacks_withtracks = {
  .tracks_added     = tracks_added,
  .tracks_removed   = tracks_removed,
  .tracks_moved     = tracks_moved,
  .playlist_renamed = playlist_renamed,
  .image_changed    = playlist_image_changed,
  .playlist_state_changed = playlist_state_changed,
  .track_created_changed = track_update_created,
  .playlist_metadata_updated = playlist_metadata_updated,
};


/**
 *
 */
static void
spotify_delete_tracks(playlist_t *pl, prop_vec_t *pv)
{
  playlist_item_t *pli;
  int *targets;
  int k = 0, i, j = 0, m = 0, ntracks = prop_vec_len(pv);
  if(ntracks == 0)
    return;

  targets = malloc(sizeof(int) * ntracks);

  for(i = 0; i < pl->pl_tracks.size; i++) {
    pli = pl->pl_tracks.vec[i];
    for(j = k; j < ntracks; j++) {
      if(prop_vec_get(pv, j) == pli->pli_prop_root) {
	if(j == k)
	  k++;
	targets[m++] = i;
      }
    }
    if(k == ntracks)
      break;
  }

  f_sp_playlist_remove_tracks(pl->pl_playlist, targets, m);
  free(targets);
}
  

/**
 *
 */
static void
playlist_node_callback(void *opaque, prop_event_t event, ...)
{
  playlist_t *pl = opaque;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_REQ_DELETE_VECTOR:
    spotify_delete_tracks(pl, va_arg(ap, prop_vec_t *));
    break;
  }
}


/**
 *
 */
static void
delete_from_container(playlistcontainer_t *plc, prop_vec_t *pv)
{
  int i, j;
  int num = prop_vec_len(pv);
  if(num == 0)
    return;

  for(i = 0; i < num; i++) {
    prop_t *p = prop_vec_get(pv, i);
    for(j = plc->plc_playlists.size - 1; j >= 0; j--) {
      playlist_t *pl = ptrvec_get_entry(&plc->plc_playlists, j);
      if(p == pl->pl_prop_root_tree || p == pl->pl_prop_root_flat) {
	if(pl->pl_type == SP_PLAYLIST_TYPE_PLAYLIST) {
	  TRACE(TRACE_DEBUG, "Spotify", "Deleting playlist on index %d", j);
	  f_sp_playlistcontainer_remove_playlist(plc->plc_pc, j);
	}
      }
    }
  }
}



/**
 *
 */
static void
playlist_container_delete_callback(void *opaque, prop_event_t event, ...)
{
  playlistcontainer_t *plc = opaque;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    (void)va_arg(ap, prop_t *);
    prop_unsubscribe(va_arg(ap, prop_sub_t *));
    break;

  case PROP_REQ_DELETE_VECTOR:
    delete_from_container(plc, va_arg(ap, prop_vec_t *));
    break;
  }
}

/**
 *
 */
static void
playlist_destroy_sub(void *opaque, prop_event_t event, ...)
{
  playlist_t *pl = opaque;

  if(event != PROP_DESTROYED)
    return;
  
  spotify_metadata_list_clear(&pl->pl_pending_metadata);
  
  if(pl->pl_offline_sub != NULL)
    prop_unsubscribe(pl->pl_offline_sub);

  if(pl->pl_node_sub) {
    f_sp_playlist_remove_callbacks(pl->pl_playlist,
				   &pl_callbacks_withtracks, pl);
    int i;
    prop_unsubscribe(pl->pl_node_sub);
    for(i = 0; i < pl->pl_tracks.size; i++)
      free(pl->pl_tracks.vec[i]);
  } else {
    f_sp_playlist_remove_callbacks(pl->pl_playlist, &pl_callbacks, pl);
  }

  f_sp_playlist_release(pl->pl_playlist);

  prop_unsubscribe(pl->pl_destroy_sub);

  free(pl->pl_tracks.vec);
  LIST_REMOVE(pl, pl_link);
  free(pl);
}


/**
 *
 */
static prop_t *
item_opt_add_toggle(prop_t *parent, prop_t *title,
		    int on, prop_callback_int_t *cb,
		    void *opaque, prop_courier_t *pc,
		    prop_sub_t **sp)
{
  prop_sub_t *s;
  prop_t *n = prop_create_root(NULL);
  prop_t *v = prop_create(n, "value");
  prop_set_string(prop_create(n, "type"), "toggle");
  prop_set_int(prop_create(n, "enabled"), 1);
  prop_set_int(v, on);
  prop_link(title, prop_create(n, "title"));
  
  s = prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_IGNORE_VOID |
		     PROP_SUB_AUTO_DESTROY,
		     PROP_TAG_CALLBACK_INT, cb, opaque,
		     PROP_TAG_ROOT, v,
		     PROP_TAG_COURIER, pc,
		     NULL);

  if(prop_set_parent(n, parent)) {
    prop_destroy(n);
    return NULL;
  }
  if(sp != NULL)
    *sp = s;
  return n;
}



/**
 *
 */
static prop_t *
item_opt_add_separator(prop_t *parent)
{
  prop_t *n = prop_create_root(NULL);
  prop_set_string(prop_create(n, "type"), "separator");

  if(prop_set_parent(n, parent)) {
    prop_destroy(n);
    return NULL;
  }
  return n;
}



/**
 *
 */
static void
set_offline_status(void *opaque, int v)
{
  playlist_t *pl = opaque;
  f_sp_playlist_set_offline_mode(spotify_session, pl->pl_playlist, v);
}


/**
 *
 */
static void
set_collab_status(void *opaque, int v)
{
  playlist_t *pl = opaque;
  f_sp_playlist_set_collaborative(pl->pl_playlist, v);
}


/**
 *
 */
static playlist_t *
pl_create(sp_playlist *plist, const char *name,
	  prop_t *model,
	  prop_t *loading,
	  prop_t *type,
	  prop_t *title,
	  prop_t *icon,
	  prop_t *canDelete,
	  prop_t *url,
	  prop_t *numtracks,
	  prop_t *nodes,
	  prop_t *items,
	  prop_t *filter,
	  prop_t *canFilter,
	  prop_t *user,
	  int flags)
{
  playlist_t *pl = calloc(1, sizeof(playlist_t));
  int i, n, v;
  uint8_t img[20];

  prop_t *options = prop_create(model, "options");

  pl->pl_type = SP_PLAYLIST_TYPE_PLAYLIST;
  LIST_INSERT_HEAD(&playlists, pl, pl_link);
  pl->pl_flags = flags;

  f_sp_playlist_add_ref(plist);

  pl->pl_playlist = plist;

  prop_set_int(loading, 0);

  prop_set_string(type, flags & PL_WITH_TRACKS ? "directory" : "playlist");


  v = f_sp_playlist_get_offline_status(spotify_session, plist);
  pl->pl_prop_offline =
    prop_ref_inc(item_opt_add_toggle(options, _p("Offline"), v,
				     set_offline_status, pl, 
				     spotify_courier,
				     &pl->pl_offline_sub));

  item_opt_add_separator(options);

  v = f_sp_playlist_is_collaborative(plist);
  pl->pl_prop_collab =
    prop_ref_inc(item_opt_add_toggle(options, _p("Collaborative"), v,
				     set_collab_status, pl, 
				     spotify_courier,
				     &pl->pl_collab_sub));
		      
  // Reference leakage here. Fix some day
  pl->pl_prop_title = prop_ref_inc(title);
  pl->pl_prop_icon = prop_ref_inc(icon);
  pl->pl_prop_canDelete = prop_ref_inc(canDelete);
  pl->pl_prop_url = prop_ref_inc(url);
  pl->pl_prop_num_tracks = prop_ref_inc(numtracks);
  pl->pl_prop_user = prop_ref_inc(user);

  if(name != NULL) {
    prop_set_string(pl->pl_prop_title, name);
  } else {
    playlist_name_update(plist, pl);
  }

  playlist_update_meta(pl);

  prop_set_int(pl->pl_prop_num_tracks, f_sp_playlist_num_tracks(plist));

  if(f_sp_playlist_get_image(plist, img))
    playlist_set_image(pl, img);

  if(pl->pl_flags & PL_WITH_TRACKS) {

    pl->pl_prop_tracks = prop_ref_inc(items);

    struct prop_nf *pnf;

    pnf = prop_nf_create(nodes, pl->pl_prop_tracks, filter,
			 pl->pl_flags & PL_SORT_ON_TIME ? "node.metadata.timestamp" : NULL,
			 PROP_NF_AUTODESTROY | PROP_NF_SORT_DESC);

    prop_nf_pred_int_add(pnf, "node.metadata.available",
			 PROP_NF_CMP_EQ, 0, NULL, 
			 PROP_NF_MODE_EXCLUDE);

    prop_nf_release(pnf);

    prop_set_int(canFilter, 1);

    n = f_sp_playlist_num_tracks(plist);
    prop_vec_t *pv = prop_vec_create(n);

    for(i = 0; i < n; i++)
      pv = prop_vec_append(pv,
			   pl_add_track(pl, f_sp_playlist_track(plist, i), i));

    prop_set_parent_vector(pv, pl->pl_prop_tracks, NULL, NULL);
    prop_vec_release(pv);
  
    prop_set_int(pl->pl_prop_num_tracks, f_sp_playlist_num_tracks(plist));

    pl->pl_node_sub = 
      prop_subscribe(0,
		     PROP_TAG_CALLBACK, playlist_node_callback, pl,
		     PROP_TAG_ROOT, pl->pl_prop_tracks,
		     PROP_TAG_COURIER, spotify_courier,
		     NULL);


    f_sp_playlist_add_callbacks(plist, &pl_callbacks_withtracks, pl);
  } else {
    f_sp_playlist_add_callbacks(plist, &pl_callbacks, pl);
  }

  pl->pl_destroy_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, playlist_destroy_sub, pl,
		   PROP_TAG_ROOT, model,
		   PROP_TAG_COURIER, spotify_courier,
		   NULL);
  return pl;
}



/**
 *
 */
static int
rootlist_bind_folders(playlistcontainer_t *plc, int start, playlist_t *parent)
{
  int i;
  for(i = start; i < plc->plc_playlists.size; i++) {
    playlist_t *pl = ptrvec_get_entry(&plc->plc_playlists, i);
    switch(pl->pl_type) {

    case SP_PLAYLIST_TYPE_START_FOLDER:
      i = rootlist_bind_folders(plc, i + 1, pl);
      if(i == -1)
	return -1;
      break;

    case SP_PLAYLIST_TYPE_END_FOLDER:
      if(parent == NULL) {
	TRACE(TRACE_DEBUG, "libspotify",
	      "stray folder end marker %016llx at %d", pl->pl_folder_id, i);
	return -1;
      }

      if(pl->pl_folder_id != parent->pl_folder_id) {
	TRACE(TRACE_DEBUG, "libspotify",
	      "mismatching folder end marker %016llx at %d expected %016llx "
	      "folder-start at %d",
	      pl->pl_folder_id, i, parent->pl_folder_id, start);
	return -1;
      }
      pl->pl_start = parent;
      return i;

    default:
      break;
    }
  }

  if(parent != NULL) {
    TRACE(TRACE_DEBUG, "libspotify",
	  "missing folder end marker for %016llx at %d",
	  parent->pl_folder_id, start);
    return -1;
  }
  return 0;
}




/**
 *
 */
static int
place_playlists_in_node(playlistcontainer_t *plc, int i, playlist_t *parent)
{
  playlist_t *before = NULL;
  prop_t *container = parent ? parent->pl_prop_childs : plc->plc_root_tree;

  while(i >= 0) {
    playlist_t *pl = ptrvec_get_entry(&plc->plc_playlists, i);
    assert(pl != NULL);
    if(pl == parent) {
      assert(pl->pl_type == SP_PLAYLIST_TYPE_START_FOLDER);
      return i;
    }

    if(pl->pl_type == SP_PLAYLIST_TYPE_END_FOLDER) {
      i = place_playlists_in_node(plc, i - 1, pl->pl_start);
      continue;
    }

    if(pl->pl_prop_root_tree != NULL) {
      if(prop_set_parent_ex(pl->pl_prop_root_tree, container,
			    before ? before->pl_prop_root_tree : NULL, NULL))
	abort();
      before = pl;
    }
    i--;
  }
  return i;
}


/**
 *
 */
static void
place_playlists_in_list(playlistcontainer_t *plc)
{
  playlist_t *before = NULL;
  int i;

  for(i = plc->plc_playlists.size-1; i >= 0; i--) {
    playlist_t *pl = ptrvec_get_entry(&plc->plc_playlists, i);

    if(pl->pl_prop_root_flat != NULL) {
      if(prop_set_parent_ex(pl->pl_prop_root_flat, plc->plc_root_flat,
			    before ? before->pl_prop_root_flat : NULL, NULL)) {
	/* Did not manage to insert, this is not fatal as the container
	   about to be destroyed */
	break;
      }
      before = pl;
    }
  }
}


#if 0
/**
 *
 */
static void
print_my_view(void)
{
  int i;
  for(i = 0; i < playlists.size; i++) {
    playlist_t *pl = ptrvec_get_entry(&playlists, i);
    printf("%4d: %s\n", i, pl->pl_intname);
  }
}
#endif

/**
 *
 */
static void
place_playlists_in_tree(playlistcontainer_t *plc)
{
  if(rootlist_bind_folders(plc, 0, NULL))
    return;
  if(plc->plc_root_tree != NULL)
    place_playlists_in_node(plc, plc->plc_playlists.size-1, NULL);
  if(plc->plc_root_flat != NULL)
    place_playlists_in_list(plc);
}


/**
 *
 */
static playlist_t *
pl_create2(sp_playlist *plist)
{
  prop_t *model = prop_create_root(NULL);
  prop_t *metadata = prop_create(model, "metadata");
  
  playlist_t *pl = pl_create(plist, NULL,
			     model,
			     prop_create(model, "loading"),
			     prop_create(model, "type"),
			     prop_create(metadata, "title"),
			     prop_create(metadata, "icon"),
			     prop_create(model, "canDelete"),
			     prop_create(model, "url"),
			     prop_create(metadata, "tracks"),
			     prop_create(model, "nodes"),
			     prop_create(model, "items"),
			     prop_create(model, "filter"),
			     prop_create(model, "canFilter"),
			     prop_create(metadata, "user"),
			     0);

  pl->pl_prop_root_flat = model;
  pl->pl_prop_root_tree = prop_create_root(NULL);

  prop_link(pl->pl_prop_root_flat, pl->pl_prop_root_tree);
  return pl;
}    




/**
 *
 */
static void
playlistcontainer_rethink(playlistcontainer_t *plc)
{
  if(plc->plc_rethink)
    return;
  plc->plc_rethink = 1;
  LIST_INSERT_HEAD(&rethink_playlistcontainers, plc, plc_rethink_link);
}


/**
 * A new playlist has been added to the users rootlist
 */
static void
playlist_added(sp_playlistcontainer *pc, sp_playlist *plist,
	       int position, void *userdata)
{
  playlistcontainer_t *plc = userdata;
  sp_playlist_type type = f_sp_playlistcontainer_playlist_type(pc, position);
  playlist_t *pl;
  prop_t *metadata;
  char url[200], buf[200];
  const char *name;

  switch(type) {
  case SP_PLAYLIST_TYPE_PLAYLIST:

    pl = pl_create2(plist);
    name = f_sp_playlist_name(plist);
    break;

  case SP_PLAYLIST_TYPE_START_FOLDER:
    pl = calloc(1, sizeof(playlist_t));
    f_sp_playlistcontainer_playlist_folder_name(pc, position, buf, sizeof(buf));
    name = buf;

    pl->pl_folder_id = f_sp_playlistcontainer_playlist_folder_id(pc, position);

    pl->pl_prop_root_tree = prop_create_root(NULL);
    prop_set_string(prop_create(pl->pl_prop_root_tree, "type"), "directory");

    metadata = prop_create(pl->pl_prop_root_tree, "metadata");
    prop_set_string(prop_create(metadata, "title"), name);
    prop_set_string(prop_create(metadata, "logo"), SPOTIFY_ICON_URL);

    backend_prop_make(pl->pl_prop_root_tree, url, sizeof(url));
    prop_set_string(prop_create(pl->pl_prop_root_tree, "url"), url);
    pl->pl_prop_childs = prop_create(pl->pl_prop_root_tree, "nodes");

    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, playlist_container_delete_callback, plc,
		   PROP_TAG_ROOT, pl->pl_prop_childs,
		   PROP_TAG_COURIER, spotify_courier,
		   NULL);
    break;

  case SP_PLAYLIST_TYPE_END_FOLDER:
    pl = calloc(1, sizeof(playlist_t));
    pl->pl_folder_id = f_sp_playlistcontainer_playlist_folder_id(pc, position);
    name = "<end folder>";
    break;

  case SP_PLAYLIST_TYPE_PLACEHOLDER:
    pl = calloc(1, sizeof(playlist_t));
    name = "<placeholder>";
    break;

  default:
    return;
  }

  snprintf(url, sizeof(url),
	   "%s 0x%016"PRId64, name, pl->pl_folder_id);

  pl->pl_type = type;

  ptrvec_insert_entry(&plc->plc_playlists, position, pl);

  TRACE(TRACE_DEBUG, "spotify", "Playlist %d added (%s)", position, name);
  playlistcontainer_rethink(plc);
}


/**
 * A playlist has been removed
 */
static void
playlist_removed(sp_playlistcontainer *pc, sp_playlist *plist,
		 int position, void *userdata)
{
  playlistcontainer_t *plc = userdata;
  playlist_t *pl = ptrvec_remove_entry(&plc->plc_playlists, position);

  TRACE(TRACE_DEBUG, "spotify", "Playlist %d removed (%s) (type:%d)", 
	position, f_sp_playlist_name(plist), pl->pl_type);

  switch(pl->pl_type) {
  case SP_PLAYLIST_TYPE_PLAYLIST:
    prop_destroy(pl->pl_prop_root_flat);
    prop_destroy(pl->pl_prop_root_tree);
    break;

  case SP_PLAYLIST_TYPE_START_FOLDER:
    prop_unparent_childs(pl->pl_prop_childs);
    prop_destroy(pl->pl_prop_root_tree);
    /* FALLTHRU */
  default:
    free(pl);
    break;
  }
  playlistcontainer_rethink(plc);
}


/**
 * A playlist has been moved
 */
static void
playlist_moved(sp_playlistcontainer *pc, sp_playlist *plist,
	       int old_position, int new_position, void *userdata)
{
  playlistcontainer_t *plc = userdata;
  playlist_t *pl;

  TRACE(TRACE_DEBUG, "spotify", "Playlist %d (%s) moved from %d to %d", 
	old_position, f_sp_playlist_name(plist), old_position, new_position);

  pl = ptrvec_remove_entry(&plc->plc_playlists, old_position);

  if(new_position > old_position)
    new_position--;
  
  ptrvec_insert_entry(&plc->plc_playlists, new_position, pl);

  if(pl->pl_type == SP_PLAYLIST_TYPE_START_FOLDER)
    return; // Wait for the END_FOLDER to move
  playlistcontainer_rethink(plc);
}


/**
 *
 */
static void
container_loaded(sp_playlistcontainer *pc, void *userdata)
{
  playlistcontainer_t *plc = userdata;
  TRACE(TRACE_INFO, "spotify", "Container for user \"%s\" loaded",
	plc->plc_name);
  prop_set_int(plc->plc_pending, 0);
}

/**
 * Playlist container callbacks
 */
static sp_playlistcontainer_callbacks pc_callbacks = {
  .playlist_added   = playlist_added,
  .playlist_removed = playlist_removed,
  .playlist_moved   = playlist_moved,
  .container_loaded = container_loaded,
};


/**
 *
 */
static void 
star_added(sp_playlist *plist, sp_track * const * tracks,
	   int num_tracks, int position, void *userdata)
{
  spotify_metadata_updated(spotify_session);
}


/**
 *
 */
static void
star_removed(sp_playlist *plist, const int *tracks,
	     int num_tracks, void *userdata)
{
  spotify_metadata_updated(spotify_session);
}



/**
 * Callbacks for starred playlist
 * We need this to catch updates to starred status
 */
static sp_playlist_callbacks star_callbacks = {
  .tracks_added     = star_added,
  .tracks_removed   = star_removed,
};


/**
 *
 */
static void
playlistcontainer_bind(sp_session *sess, playlistcontainer_t *plc,
		       sp_playlistcontainer *pc)
{
  int i, n = f_sp_playlistcontainer_num_playlists(pc);

  for(i = 0; i < n; i++)
    playlist_added(pc, f_sp_playlistcontainer_playlist(pc, i), i, plc);

  f_sp_playlistcontainer_add_callbacks(pc, &pc_callbacks, plc);

  plc->plc_pc = pc;

  if(!f_sp_playlistcontainer_is_loaded(pc))
    return;

  TRACE(TRACE_INFO, "spotify", "Container for user \"%s\" already loaded",
	plc->plc_name);
  prop_set_int(plc->plc_pending, 0);
}


/**
 *
 */
static void
playlistcontainer_unbind(sp_session *sess, playlistcontainer_t *plc,
			 sp_playlistcontainer *pc)
{
  f_sp_playlistcontainer_remove_callbacks(pc, &pc_callbacks, plc);
  int i, n = f_sp_playlistcontainer_num_playlists(pc);

  for(i = 0; i < n; i++)
    playlist_removed(pc, f_sp_playlistcontainer_playlist(pc, i), 0, plc);

}


/**
 *
 */
static playlistcontainer_t *
playlistcontainer_create(const char *name, int can_delete)
{
  playlistcontainer_t *plc = calloc(1, sizeof(playlistcontainer_t));
  plc->plc_root_tree = prop_create_root(NULL);

  if(can_delete)
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, playlist_container_delete_callback, plc,
		   PROP_TAG_ROOT, plc->plc_root_tree,
		   PROP_TAG_COURIER, spotify_courier,
		   NULL);

  plc->plc_root_flat = prop_create_root(NULL);
  plc->plc_pending = prop_create_root(NULL);
  prop_set_int(plc->plc_pending, 1);
  plc->plc_name = strdup(name);
  return plc;
}



/**
 *
 */
static void
load_initial_playlists(sp_session *sess)
{
  playlistcontainer_bind(sess, current_user_rootlist,
			 f_sp_session_playlistcontainer(sess));

  f_sp_playlist_add_callbacks(f_sp_session_starred_create(sess),
			      &star_callbacks, NULL);
}


/**
 *
 */
static void
unload_initial_playlists(sp_session *sess)
{
  playlistcontainer_unbind(sess, current_user_rootlist,
			   f_sp_session_playlistcontainer(sess));
}

/**
 *
 */
static void
playlistcontainer_destroy_sub(void *opaque, prop_event_t event, ...)
{
  playlistcontainer_t *plc = opaque;

  if(event != PROP_DESTROYED)
    return;
  
  f_sp_playlistcontainer_remove_callbacks(plc->plc_pc, &pc_callbacks, plc);

  if(plc->plc_rethink)
    LIST_REMOVE(plc, plc_rethink_link);

  prop_unsubscribe(plc->plc_destroy_sub);

  f_sp_playlistcontainer_release(plc->plc_pc);
  prop_ref_dec(plc->plc_root_flat);
  prop_ref_dec(plc->plc_pending);
  free(plc->plc_name);
  free(plc);
}


/**
 * When 'tracking_prop' is destroyed we will destroy ourselfs
 */
static void
plc_for_user(sp_session *sess, spotify_page_t *sp, const char *username)
{
  struct prop_nf *pnf;

  sp_playlistcontainer *pc;
  playlistcontainer_t *plc = calloc(1, sizeof(playlistcontainer_t));

  plc->plc_root_tree = NULL;
  plc->plc_root_flat = prop_ref_inc(sp->sp_items);
  plc->plc_pending = prop_ref_inc(sp->sp_loading);
  prop_set_int(plc->plc_pending, 1);

  plc->plc_name = strdup(username);

  pc = f_sp_session_publishedcontainer_for_user_create(sess, username);
  playlistcontainer_bind(sess, plc, pc);

  plc->plc_destroy_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, playlistcontainer_destroy_sub, plc,
		   PROP_TAG_ROOT, sp->sp_items,
		   PROP_TAG_COURIER, spotify_courier,
		   NULL);

  prop_set_string(sp->sp_type, "directory");
  prop_link(_p("Spotify playlists"), sp->sp_title);

  pnf = prop_nf_create(sp->sp_nodes, sp->sp_items,
		       sp->sp_filter, NULL, PROP_NF_AUTODESTROY);
  prop_set_int(sp->sp_canFilter, 1);
  prop_nf_release(pnf);
}


/**
 *
 */
static void
spotify_got_image(sp_image *image, void *userdata)
{
  spotify_image_t *si = userdata;
  size_t size;
  const void *pixels = f_sp_image_data(image, &size);

  si->si_pixmap = pixmap_alloc_coded(pixels, size, PIXMAP_JPEG);

  hts_mutex_lock(&spotify_mutex);
  si->si_errcode = 0;
  hts_cond_broadcast(&spotify_cond_image);
  hts_mutex_unlock(&spotify_mutex);
  f_sp_image_release(image);
}


/**
 *
 */
static void
spotify_get_image(spotify_image_t *si)
{
  sp_link *l = f_sp_link_create_from_string(si->si_url);
  if(l == NULL || f_sp_link_type(l) != SP_LINKTYPE_IMAGE) {
    si->si_errcode = 1;
    return;
  }

  f_sp_image_add_load_callback(f_sp_image_create_from_link(spotify_session, l),
			       spotify_got_image, si);
  f_sp_link_release(l);
}



/**
 *
 */
static void
ss_fill_tracks(sp_search *result, spotify_search_request_t *ssr)
{
  int ntracks = f_sp_search_num_tracks(result);
  int total = f_sp_search_total_tracks(result);
  int i;
  prop_vec_t *pv = prop_vec_create(ntracks);


  for(i = 0; i < ntracks; i++)
    pv = prop_vec_append(pv, track_create(f_sp_search_track(result, i), NULL,
					  NULL, 0));

  prop_set_parent_vector(pv, ssr->ssr_nodes, NULL, NULL);
  prop_vec_release(pv);

  ssr->ssr_offset += ntracks;
  prop_set_int(ssr->ssr_entries, total);

  if(ssr->ssr_offset != total)
    prop_have_more_childs(ssr->ssr_nodes);
}


/**
 *
 */
static void
ss_fill_albums(sp_search *result, spotify_search_request_t *ssr)
{
  int nalbums = f_sp_search_num_albums(result);
  int i;
  prop_t *p, *metadata;
  sp_album *album, *album_prev = NULL;
  sp_artist *artist;
  char link[URL_MAX];
  int inc = 0;
  prop_vec_t *pv = prop_vec_create(nalbums);

  prop_have_more_childs(ssr->ssr_nodes);
  for(i = 0; i < nalbums; i++) {
    album = f_sp_search_album(result, i);
    artist = f_sp_album_artist(album);

    
    if(album_prev != NULL) {
      // Skip dupes
      if(f_sp_album_artist(album_prev) == artist &&
	 !strcmp(f_sp_album_name(album), f_sp_album_name(album_prev)))
	continue; 
    }

    p = prop_create_root(NULL);

    spotify_make_link(f_sp_link_create_from_album(album), link, sizeof(link));
    prop_set_string(prop_create(p, "url"), link);
    prop_set_string(prop_create(p, "type"), "album");

    metadata = prop_create(p, "metadata");
    prop_set_string(prop_create(metadata, "title"), f_sp_album_name(album));

    spotify_make_link(f_sp_link_create_from_artist(artist), link, sizeof(link));
    prop_set_link(prop_create(metadata, "artist"),
		  f_sp_artist_name(artist), link);

    set_image_uri(prop_create(metadata, "icon"),
		  f_sp_link_create_from_album_cover(album));

    pv = prop_vec_append(pv, p);
    album_prev = album;
    inc++;
  }

  prop_set_parent_vector(pv, ssr->ssr_nodes, NULL, NULL);
  prop_vec_release(pv);
  prop_add_int(ssr->ssr_entries, inc);

  ssr->ssr_offset += nalbums;
}



/**
 *
 */
static void
ss_fill_artists(sp_search *result, spotify_search_request_t *ssr)
{
  int nartists = f_sp_search_num_artists(result);
  int i, inc = 0;
  prop_t *p, *metadata;
  sp_artist *artist;
  char link[URL_MAX];
  prop_vec_t *pv = prop_vec_create(nartists);

  prop_have_more_childs(ssr->ssr_nodes);

  for(i = 0; i < nartists; i++) {
    artist = f_sp_search_artist(result, i);
    
    p = prop_create_root(NULL);

    spotify_make_link(f_sp_link_create_from_artist(artist), link, sizeof(link));
    prop_set_string(prop_create(p, "url"), link);
    prop_set_string(prop_create(p, "type"), "artist");

    metadata = prop_create(p, "metadata");
    prop_set_string(prop_create(metadata, "title"), f_sp_artist_name(artist));

    sp_link *l = f_sp_link_create_from_artist_portrait(artist);
    if(l != NULL) {
      spotify_make_link(l, link, sizeof(link));
      prop_set_string(prop_create(metadata, "icon"), link);
    }

    pv = prop_vec_append(pv, p);
    inc++;
  }

  prop_set_parent_vector(pv, ssr->ssr_nodes, NULL, NULL);
  prop_vec_release(pv);

  prop_add_int(ssr->ssr_entries, inc);

  ssr->ssr_offset += nartists;
}

/**
 *
 */
static void
spotify_search_done(sp_search *result, void *userdata)
{
  spotify_search_t *ss = userdata;

  TRACE(TRACE_DEBUG, "spotify",
	"Search '%s' completed (%s) %d tracks, %d albums, %d artists",
	ss->ss_query,
	f_sp_error_message(f_sp_search_error(result)),
	f_sp_search_num_tracks(result),
	f_sp_search_num_albums(result),
	f_sp_search_num_artists(result));

  ss_fill_tracks(result,  &ss->ss_reqs[SS_TRACKS]);
  ss_fill_albums(result,  &ss->ss_reqs[SS_ALBUMS]);
  ss_fill_artists(result, &ss->ss_reqs[SS_ARTISTS]);

  search_release(ss);
}

#define SEARCH_LIMIT 250


/**
 *
 */
static void
search_nodesub(void *opaque, prop_event_t event, ...)
{
  spotify_search_request_t *ssr = opaque;
  spotify_search_t *ss = ssr->ssr_ss;
  va_list ap;

  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    search_release(ss);
    break;

  case PROP_WANT_MORE_CHILDS:
    if(ssr->ssr_last_search == ssr->ssr_offset) {
      prop_have_more_childs(ssr->ssr_nodes);
      break;
    }

    ssr->ssr_last_search = ssr->ssr_offset;
    ss->ss_ref++;

    if(ssr == &ss->ss_reqs[SS_TRACKS]) {
      f_sp_search_create(spotify_session, ss->ss_query,
			 ssr->ssr_offset, SEARCH_LIMIT, 0, 0, 0, 0, 
			 spotify_search_done, ss);
    } else if(ssr == &ss->ss_reqs[SS_ALBUMS]) {
      f_sp_search_create(spotify_session, ss->ss_query,
			 0, 0, ssr->ssr_offset, SEARCH_LIMIT, 0, 0,
			 spotify_search_done, ss);
    } else {
      f_sp_search_create(spotify_session, ss->ss_query,
			 0, 0,  0, 0, ssr->ssr_offset, SEARCH_LIMIT,
			 spotify_search_done, ss);
    }
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void
spotify_search(spotify_search_t *ss)
{
  int i;
  ss->ss_ref = 4;

  for(i = 0; i < 3; i++) {
    spotify_search_request_t *ssr = &ss->ss_reqs[i];
    ssr->ssr_ss = ss;
    ssr->ssr_sub = 
      prop_subscribe(PROP_SUB_TRACK_DESTROY,
		     PROP_TAG_CALLBACK, search_nodesub, ssr,
		     PROP_TAG_ROOT, ssr->ssr_nodes,
		     PROP_TAG_COURIER, spotify_courier,
		     NULL);
  }

  TRACE(TRACE_DEBUG, "spotify",
	"Initial search for '%s'", ss->ss_query);

  f_sp_search_create(spotify_session, ss->ss_query,
		     0, SEARCH_LIMIT, 0, SEARCH_LIMIT, 0, SEARCH_LIMIT,
		     spotify_search_done, ss);
}


/**
 *
 */
static void
spotify_try_pending(void)
{
  spotify_page_t *sp, *next;

  for(sp = LIST_FIRST(&pending_album_queries); sp != NULL; sp = next) {
    next = LIST_NEXT(sp, sp_query_link);
    try_get_album(sp);
  }

  for(sp = LIST_FIRST(&pending_track_item_resolve); sp != NULL; sp = next) {
    next = LIST_NEXT(sp, sp_query_link);
    try_resolve_track_item(sp);
  }
}


/**
 *
 */
static int
find_cachedir(char *path, size_t pathlen)
{
#if defined(LOCK_EX) && defined(LOCK_NB)
  int i, fd;
  char buf[PATH_MAX];

  snprintf(buf, sizeof(buf), "%s/libspotify", showtime_cache_path);
  if(mkdir(buf, 0770)) {
    if(errno != EEXIST)
      return -1;
  }

  i = 0;
  for(i = 0; i < 64; i++) {
    snprintf(buf, sizeof(buf), "%s/libspotify/%d.lock", showtime_cache_path, i);
    
    fd = open(buf, O_CREAT | O_RDWR, 0770);
    if(fd == -1)
      return -1;

    if(flock(fd, LOCK_EX | LOCK_NB)) {
      close(fd);
      continue;
    }

    snprintf(path, pathlen, "%s/libspotify/%d.cache", showtime_cache_path, i);
    return 0;
  }
#endif

  snprintf(path, pathlen, "%s/libspotify", showtime_cache_path);
  if(mkdir(path, 0770)) {
    if(errno != EEXIST)
      return 1;
  }
  return 0;
}


/**
 *
 */
static void
do_rethink_playlistcontainers(void)
{
  playlistcontainer_t *plc;
  while((plc = LIST_FIRST(&rethink_playlistcontainers)) != NULL) {
    plc->plc_rethink = 0;
    LIST_REMOVE(plc, plc_rethink_link);
    place_playlists_in_tree(plc);
  }
}


/**
 *
 */
static void *
spotify_thread(void *aux)
{
  sp_session_config sesconf;
  sp_error error;
  sp_session *s;
  spotify_msg_t *sm;
  int next_timeout = 0;
  char cache[PATH_MAX];
  int high_bitrate = 0;
  int offline_bitrate_96 = 0;

  memset(&sesconf, 0, sizeof(sesconf));

  sesconf.api_version = SPOTIFY_API_VERSION;

  if(find_cachedir(cache, sizeof(cache)))
    sesconf.cache_location = "/tmp/libspotify";
  else
    sesconf.cache_location = cache;

  sesconf.settings_location = sesconf.cache_location;

  TRACE(TRACE_DEBUG, "spotify", "Cache location: %s", sesconf.cache_location);

  sesconf.application_key = appkey;
  sesconf.application_key_size = sizeof(appkey);
  sesconf.user_agent = "Showtime";
  sesconf.callbacks = &spotify_session_callbacks;
  

  error = f_sp_session_create(&sesconf, &s);
  if(error) {
    fail_pending_messages(f_sp_error_message(error));
    is_thread_running = 0;
    return NULL;
  }

  spotify_session = s;

  spotify_try_login(s, 0, NULL, silent_start);

  /* Wakeup any sleepers that are waiting for us to start */

  while(1) {
    sm = NULL;

    do_rethink_playlistcontainers();

    hts_mutex_lock(&spotify_mutex);

    while(!spotify_pending_events) {

      if((sm = TAILQ_FIRST(&spotify_msgs)) != NULL) {
	if(!is_logged_in) {
	  sm = NULL;
	  login_rejected_by_user = 0;
	}
	break;
      }

     if(next_timeout == 0) 
       hts_cond_wait(&spotify_cond_main, &spotify_mutex);
     else if(hts_cond_wait_timeout(&spotify_cond_main,
				   &spotify_mutex, next_timeout))
       break;
    }

    if(sm != NULL)
      TAILQ_REMOVE(&spotify_msgs, sm, sm_link);

    spotify_pending_events = 0;

    hts_mutex_unlock(&spotify_mutex);

    if(sm != NULL && !is_logged_in && !login_rejected_by_user) {
      spotify_try_login(s, 0, NULL, 0);
    }
    if(high_bitrate != spotify_high_bitrate) {
      high_bitrate = spotify_high_bitrate;
      f_sp_session_preferred_bitrate(s, 
				     high_bitrate ? SP_BITRATE_320k : 
				     SP_BITRATE_160k);

      TRACE(TRACE_DEBUG, "spotify", "Bitrate set to %dk",
	    high_bitrate ? 320 : 160);
    }


    if(offline_bitrate_96 != spotify_offline_bitrate_96) {
      offline_bitrate_96 = spotify_offline_bitrate_96;
      f_sp_session_preferred_offline_bitrate(s, 
					     offline_bitrate_96  ? SP_BITRATE_96k : 
					     SP_BITRATE_160k, 0);

      TRACE(TRACE_DEBUG, "spotify", "Bitrate set to %dk",
	    offline_bitrate_96 ? 96 : 160);
    }


    if(sm != NULL) {
      switch(sm->sm_op) {
      case SPOTIFY_LOGOUT:
	TRACE(TRACE_INFO, "spotify", "Requesting logout");
	if(!pending_relogin)
	  f_sp_session_logout(s);
	pending_relogin = NULL;
	break;
      case SPOTIFY_OPEN_PAGE:
	spotify_open_page(sm->sm_ptr);
	break;
      case SPOTIFY_RESOLVE_ITEM:
	spotify_resolve_item(sm->sm_ptr);
	break;
      case SPOTIFY_PLAY_TRACK:
	spotify_play_track(sm->sm_ptr);
	break;
      case SPOTIFY_STOP_PLAYBACK:
	f_sp_session_player_unload(s);
	break;
      case SPOTIFY_SEEK:
	if(spotify_mp == NULL)
	  break;

	mp_flush(spotify_mp, 0);
	
	seek_pos = sm->sm_int;
	f_sp_session_player_seek(s, sm->sm_int);
	break;

      case SPOTIFY_PAUSE:
	f_sp_session_player_play(s, !sm->sm_int);
	break;

      case SPOTIFY_GET_IMAGE:
	spotify_get_image(sm->sm_ptr);
	break;

      case SPOTIFY_SEARCH:
	spotify_search(sm->sm_ptr);
	break;
      }

      free(sm);
    }

    prop_courier_poll(spotify_courier);

    do {
      f_sp_session_process_events(s, &next_timeout);
    } while(next_timeout == 0);
  }
}


/**
 *
 */
static int
spotify_start(char *errbuf, size_t errlen, int silent)
{
  if(!spotify_is_enabled) {
    snprintf(errbuf, errlen, "Spotify is not enabled");
    return -1;
  }

  hts_mutex_lock(&spotify_mutex);

  if(!is_thread_running) {
    is_thread_running = 1;
    silent_start = silent;
    hts_thread_create_detached("spotify", spotify_thread, NULL,
			       THREAD_PRIO_NORMAL);
    shutdown_hook_add(spotify_shutdown_early, NULL, 1);
    shutdown_hook_add(spotify_shutdown_late, NULL, 0);
  }

  hts_mutex_unlock(&spotify_mutex);
  return 0;
}


/**
 *
 */
static void
add_dir(prop_t *parent, const char *url, prop_t *title, const char *subtype)
{
  prop_t *p = prop_create_root(NULL);
  prop_t *metadata = prop_create(p, "metadata");

  prop_set_string(prop_create(p, "type"), "directory");
  prop_set_string(prop_create(p, "url"), url);

  prop_link(title, prop_create(metadata, "title"));
  prop_set_string(prop_create(metadata, "subtype"), subtype);
  if(prop_set_parent(p, parent))
    abort();

}

/**
 *
 */
static void
startpage(prop_t *page)
{
  prop_t *model = prop_create(page, "model");
  prop_t *metadata = prop_create(model, "metadata");

  prop_set_string(prop_create(model, "type"), "directory");
  prop_set_string(prop_create(model, "contents"), "items");
  prop_set_string(prop_create(metadata, "logo"), SPOTIFY_ICON_URL);
  prop_set_string(prop_create(metadata, "title"), "Spotify");

  prop_t *nodes = prop_create(model, "nodes");

  add_dir(nodes, "spotify:playlists", _p("Playlists"), "playlists");
  add_dir(nodes, "spotify:search:tag:new", _p("New releases"), NULL);
  add_dir(nodes, "spotify:starred", _p("Starred"), "starred");
  add_dir(nodes, "spotify:inbox", _p("Inbox"), "inbox");
#if SPOTIFY_WITH_SOCIAL
  add_dir(nodes, "spotify:friends", _p("Friends"), "friends");
#endif
}


/**
 *
 */
static void
add_metadata_props(spotify_page_t *sp)
{
  prop_t *m = prop_create(sp->sp_model, "metadata");


  sp->sp_title = prop_ref_inc(prop_create(m, "title"));
  sp->sp_icon = prop_ref_inc(prop_create(m, "logo"));
  prop_set_string(sp->sp_icon, SPOTIFY_ICON_URL);

  sp->sp_album_name = prop_ref_inc(prop_create(m, "album_name"));
  sp->sp_album_year = prop_ref_inc(prop_create(m, "album_year"));

  sp->sp_album_art  = prop_ref_inc(prop_create(m, "album_art"));

  sp->sp_artist_name = prop_ref_inc(prop_create(m, "aritst_name"));

  sp->sp_numtracks = prop_ref_inc(prop_create(m, "tracks"));

  sp->sp_user = prop_ref_inc(prop_create(m, "user"));
}


/**
 *
 */
static int
be_spotify_open(prop_t *page, const char *url)
{
  char errbuf[200];

  if(spotify_start(errbuf, sizeof(errbuf), 0))
    return nav_open_error(page, errbuf);

  if(!strcmp(url, "spotify:start")) {
    startpage(page);
    return 0;
  }

  spotify_page_t *sp = calloc(1, sizeof(spotify_page_t));

  sp->sp_url = strdup(url);

  sp->sp_urlprop = prop_ref_inc(prop_create(page, "url"));
  sp->sp_model   = prop_ref_inc(prop_create(page, "model"));
  sp->sp_type    = prop_ref_inc(prop_create(sp->sp_model, "type"));
  sp->sp_error   = prop_ref_inc(prop_create(sp->sp_model, "error"));
  sp->sp_loading = prop_ref_inc(prop_create(sp->sp_model, "loading"));
  sp->sp_contents = prop_ref_inc(prop_create(sp->sp_model, "contents"));
  sp->sp_nodes = prop_ref_inc(prop_create(sp->sp_model, "nodes"));
  sp->sp_items = prop_ref_inc(prop_create(sp->sp_model, "items"));
  sp->sp_filter = prop_ref_inc(prop_create(sp->sp_model, "filter"));
  sp->sp_canFilter = prop_ref_inc(prop_create(sp->sp_model, "canFilter"));
  sp->sp_canDelete = prop_ref_inc(prop_create(sp->sp_model, "canDelete"));
  
  add_metadata_props(sp);
  prop_set_int(sp->sp_loading, 1);
  spotify_msg_enq(spotify_msg_build(SPOTIFY_OPEN_PAGE, sp));
  return 0;
}


/**
 *
 */
static void
delta_seek(media_pipe_t *mp, int64_t d)
{
  int64_t n = mp->mp_current_time + d;
  if(n < 0)
    n = 0;
  spotify_msg_enq_one(spotify_msg_build_int(SPOTIFY_SEEK, n / 1000));
}


/**
 * Play given track.
 *
 * We only expect this to be called from the playqueue system.
 */
static event_t *
be_spotify_play(const char *url, media_pipe_t *mp, 
		char *errbuf, size_t errlen, int hold,
		const char *mimetype)
{
  spotify_uri_t su;
  event_t *e, *eof = NULL;
  event_ts_t *ets;
  int lost_focus = 0;
  media_queue_t *mq = &mp->mp_audio;
  
  memset(&su, 0, sizeof(su));

  if(!strcmp(url, "spotify:track:0000000000000000000000")) {
    /* Invalid track - happens for localtracks */
    snprintf(errbuf, errlen, "Invalid track");
    return NULL;
  }

  if(spotify_start(errbuf, errlen, 0))
    return NULL;

  assert(spotify_mp == NULL);
  spotify_mp = mp;

  su.su_uri = url;
  su.su_errbuf = errbuf;
  su.su_errlen = errlen;
  su.su_errcode = -1;

  hts_mutex_lock(&spotify_mutex);
  
  spotify_msg_enq_locked(spotify_msg_build(SPOTIFY_PLAY_TRACK, &su));

  while(su.su_errcode == -1)
    hts_cond_wait(&spotify_cond_uri, &spotify_mutex);

  if(su.su_errcode) {
    spotify_mp = NULL;
    spotify_msg_enq_locked(spotify_msg_build(SPOTIFY_STOP_PLAYBACK, NULL));
    hts_mutex_unlock(&spotify_mutex);
    return NULL;
  }

  hts_mutex_unlock(&spotify_mutex);

  mp_configure(mp, MP_PLAY_CAPS_SEEK | MP_PLAY_CAPS_PAUSE,
	       MP_BUFFER_NONE);

  mp_set_playstatus_by_hold(mp, hold, NULL);

  /* Playback successfully started, wait for events */
  while(1) {

    if(eof != NULL) {
      /* End of file, wait a while for queues to drain more */
      e = mp_wait_for_empty_queues(mp);
      if(e == NULL) {
	e = eof;
	eof = NULL;
	break;
      }

    } else {
      e = mp_dequeue_event(mp);
    }

    if(event_is_type (e, EVENT_EOF)) {
      eof = e;
      continue;
    }


    if(event_is_action(e, ACTION_PREV_TRACK) ||
       event_is_action(e, ACTION_NEXT_TRACK) ||
       event_is_action(e, ACTION_STOP) ||
       event_is_type  (e, EVENT_PLAYQUEUE_JUMP)) {
      
      mp_flush(mp, 0);
      break;
      
    } else if(event_is_type(e, EVENT_SEEK)) {

      ets = (event_ts_t *)e;
      spotify_msg_enq_one(spotify_msg_build_int(SPOTIFY_SEEK, ets->ts / 1000));

    } else if(event_is_action(e, ACTION_PLAYPAUSE) ||
	      event_is_action(e, ACTION_PLAY) ||
	      event_is_action(e, ACTION_PAUSE)) {

      hold = action_update_hold_by_event(hold, e);
      spotify_msg_enq(spotify_msg_build_int(SPOTIFY_PAUSE, hold));
      mp_send_cmd_head(mp, mq, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      mp_set_playstatus_by_hold(mp, hold, NULL);
      lost_focus = 0;

    } else if(event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

      hold = 1;
      lost_focus = 1;
      spotify_msg_enq(spotify_msg_build_int(SPOTIFY_PAUSE, 1));
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_type(e, EVENT_MP_IS_PRIMARY)) {

      if(lost_focus) {
	hold = 0;
	lost_focus = 0;
	spotify_msg_enq(spotify_msg_build_int(SPOTIFY_PAUSE, 0));
	mp_send_cmd_head(mp, mq, MB_CTRL_PLAY);
	mp_set_playstatus_by_hold(mp, hold, NULL);
      }

    } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

      hold = 1;
      lost_focus = 0;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_action(e, ACTION_SEEK_FAST_BACKWARD)) {

      delta_seek(mp, -60000000);

    } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {

      delta_seek(mp, -15000000);

    } else if(event_is_action(e, ACTION_SEEK_FAST_FORWARD)) {

      delta_seek(mp,  60000000);

    } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {

      delta_seek(mp,  15000000);
    }
    event_release(e);
  }

  if(eof != NULL)
    event_release(eof);

  if(hold) {
    // If we were paused, release playback again.
    mp_send_cmd(mp, mq, MB_CTRL_PLAY);
    mp_set_playstatus_by_hold(mp, 0, NULL);
  }

  spotify_mp = NULL;
  spotify_msg_enq(spotify_msg_build(SPOTIFY_STOP_PLAYBACK, NULL));
  return e;
}


/**
 *
 */
static pixmap_t *
be_spotify_imageloader(const char *url, const image_meta_t *im,
		       const char **vpaths, char *errbuf, size_t errlen)
{
  spotify_image_t si = {0};

  if(spotify_start(errbuf, errlen, 0))
    return NULL;

  hts_mutex_lock(&spotify_mutex);

  si.si_url = url;
  si.si_errcode = -1;

  spotify_msg_enq_locked(spotify_msg_build(SPOTIFY_GET_IMAGE, &si));

  while(si.si_errcode == -1)
    hts_cond_wait(&spotify_cond_image, &spotify_mutex);

  hts_mutex_unlock(&spotify_mutex);

  if(si.si_errcode == 0)
    return si.si_pixmap;

  snprintf(errbuf, errlen, "Unable to load image");
  return NULL;
}


#ifdef CONFIG_LIBSPOTIFY_LOAD_RUNTIME
/**
 *
 */
static int
be_spotify_dlopen(void)
{
  void *h;
  const char *sym;
  char libname[64];

  snprintf(libname, sizeof(libname), "libspotify.so.%d", SPOTIFY_API_VERSION);

  h = dlopen(libname, RTLD_NOW);
  if(h == NULL) {
    TRACE(TRACE_INFO, "spotify", "Unable to load %s: %s", libname, dlerror());
    return 1;
  }
  if((sym = resolvesym(h)) != NULL) {
    TRACE(TRACE_ERROR, "spotify", "Unable to resolve symbol \"%s\"", sym);
    dlclose(h);
    return 1;
  }
  return 0;
}
#endif


/**
 *
 */
static void
courier_notify(void *opaque)
{
  hts_mutex_lock(&spotify_mutex);
  spotify_pending_events = 1;
  hts_cond_signal(&spotify_cond_main);
  hts_mutex_unlock(&spotify_mutex);
}

static service_t *spotify_service;
static int spotify_autologin;

/**
 *
 */
static void
spotify_set_enable(void *opaque, int value)
{
  spotify_is_enabled = value;
}


/**
 *
 */
static void
spotify_set_bitrate(void *opaque, int value)
{
  hts_mutex_lock(&spotify_mutex);
  spotify_high_bitrate = value;
  spotify_pending_events = 1;
  hts_cond_signal(&spotify_cond_main);
  hts_mutex_unlock(&spotify_mutex);
}

/**
 *
 */
static void
spotify_set_offline_bitrate(void *opaque, int value)
{
  hts_mutex_lock(&spotify_mutex);
  spotify_offline_bitrate_96 = value;
  spotify_pending_events = 1;
  hts_cond_signal(&spotify_cond_main);
  hts_mutex_unlock(&spotify_mutex);
}


static void
spotify_relogin0(const char *reason)
{
  TRACE(TRACE_INFO, "spotify", "Attempting to relogin: %s", reason);
  unload_initial_playlists(spotify_session);
  clear_friends();
  f_sp_session_logout(spotify_session);
  f_sp_session_forget_me(spotify_session);
  pending_relogin = reason;
}


static void
spotify_relogin(void *opaque, prop_event_t event, ...)
{
  spotify_relogin0("Requested by user");
}

static void
spotify_forget_me(void *opaque, prop_event_t event, ...)
{
  f_sp_session_forget_me(spotify_session);
}


/**
 *
 */
static void
spotify_dispatch_action(const char *ev)
{
  if(!strcmp(ev, "relogin")) {
    spotify_relogin0("Requested by user");
  }
}


/**
 *
 */
static void
spotify_control(void *opaque, prop_event_t event, ...)
{
  va_list ap;
  event_t *e;
  if(event != PROP_EXT_EVENT)
    return;

  va_start(ap, event);

  e = va_arg(ap, event_t *);

  if(event_is_type(e, EVENT_ACTION_VECTOR)) {
    event_action_vector_t *eav = (event_action_vector_t *)e;
    int i;
    for(i = 0; i < eav->num; i++)
      spotify_dispatch_action(action_code2str(eav->actions[i]));
    
  } else if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
    spotify_dispatch_action(e->e_payload);
  }
}

/**
 *
 */
static int
be_spotify_init(void)
{
  prop_t *spotify;
  prop_t *s, *ctrl;
  setting_t *ena;

#ifdef CONFIG_LIBSPOTIFY_LOAD_RUNTIME
  if(be_spotify_dlopen())
    return 1;
#endif

  TRACE(TRACE_INFO, "Spotify", "Using library version %s", f_sp_build_id());

  prop_t *title = prop_create_root(NULL);
  prop_set_string(title, "Spotify");

  s = settings_add_dir(settings_apps, title, NULL, SPOTIFY_ICON_URL,
		       _p("Spotify music service"));

  spotify_courier = prop_courier_create_notify(courier_notify, NULL);

  spotify = prop_create(prop_get_global(), "spotify");

  friend_nodes = prop_create(spotify, "friends");

  current_user_rootlist = playlistcontainer_create("Self", 1);

  TAILQ_INIT(&spotify_msgs);

  hts_mutex_init(&spotify_mutex);
  hts_cond_init(&spotify_cond_main, &spotify_mutex);
  hts_cond_init(&spotify_cond_uri, &spotify_mutex);
  hts_cond_init(&spotify_cond_image, &spotify_mutex);

  // Configuration

  htsmsg_t *store = htsmsg_store_load("spotify") ?: htsmsg_create_map();

  settings_create_info(s, 
		       "bundle://resources/spotify/spotify-core-logo-96x96.png",
		       _p("Spotify offers you legal and free access to a huge library of music. To use Spotify in Showtime you need a Spotify Preemium account.\nFor more information about Spotify, visit http://www.spotify.com/\n\nYou will be prompted for your Spotify username and password when first accessing any of the Spotify features in Showtime."));

  spotify_service = service_create("Spotify", "spotify:start",
				   "music", SPOTIFY_ICON_URL, 0, 0);

  ena = settings_create_bool(s, "enable", _p("Enable Spotify"), 0, 
			     store, spotify_set_enable, NULL,
			     SETTINGS_INITIAL_UPDATE, NULL,
			     settings_generic_save_settings, (void *)"spotify");

  settings_create_bool(s, "autologin", 
		       _p("Automatic login when Showtime starts"), 1, 
		       store, settings_generic_set_bool, &spotify_autologin,
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, (void *)"spotify");

  settings_create_bool(s, "highbitrate", _p("High bitrate"), 0,
		       store, spotify_set_bitrate, NULL,
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, (void *)"spotify");

  settings_create_bool(s, "offlinebitrate", _p("Offline sync in 96kbps"), 0,
		       store, spotify_set_offline_bitrate, NULL,
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, (void *)"spotify");

  settings_create_action(s, "relogin", _p("Relogin (switch user)"),
			 spotify_relogin, NULL, spotify_courier);

  settings_create_action(s, "forgetme", _p("Forget me"),
			 spotify_forget_me, NULL, spotify_courier);

  prop_link(settings_get_value(ena),
	    prop_create(spotify_service->s_root, "enabled"));

  if(spotify_is_enabled && spotify_autologin) {
    TRACE(TRACE_DEBUG, "spotify", "Autologin");
    spotify_start(NULL, 0, 1);
  }

  spotify = prop_create(prop_get_global(), "spotify");
  ctrl = prop_create(spotify, "control");

  prop_subscribe(0,
		 PROP_TAG_CALLBACK, spotify_control, NULL,
		 PROP_TAG_ROOT, ctrl,
		 PROP_TAG_COURIER, spotify_courier,
		 NULL);

  return 0;
}


/**
 *
 */
static int
be_spotify_canhandle(const char *url)
{
  if(!strncmp(url, "spotify:", strlen("spotify:")))
    return 1;

  if(!strncmp(url, "http://open.spotify.com/", 
	      strlen("http://open.spotify.com/")))
    return 2;
  return 0;
}


/**
 *
 */
static void
spotify_shutdown_early(void *opaque, int exitcode)
{
  pending_login = 1;

  hts_mutex_lock(&spotify_mutex);

  if(is_logged_in)
    spotify_msg_enq_locked(spotify_msg_build(SPOTIFY_LOGOUT, NULL));

  hts_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
static void
spotify_shutdown_late(void *opaque, int exitcode)
{
  int i;
  hts_mutex_lock(&spotify_mutex);

  for(i = 0; i < 50 && is_logged_in; i++)
    usleep(100000);

  hts_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
static void
be_spotify_search(prop_t *source, const char *query)
{
  if(spotify_start(NULL, 0, 0))
    return;
  
  spotify_search_t *ss = calloc(1, sizeof(spotify_search_t));
  int i;

  prop_t *n = prop_create(source, "nodes");
  for(i = 0; i < 3; i++) {
    spotify_search_request_t *ssr = &ss->ss_reqs[i];
    const char *title = NULL;

    switch(i) {
    case SS_TRACKS:
      title = "Spotify tracks";
      break;
    case SS_ALBUMS:
      title = "Spotify albums";
      break;
    case SS_ARTISTS:
      title = "Spotify artists";
      break;
    }

    search_class_create(n, &ssr->ssr_nodes, &ssr->ssr_entries, title,
			SPOTIFY_ICON_URL);
  }

  ss->ss_query = strdup(query);
  
  spotify_msg_enq(spotify_msg_build(SPOTIFY_SEARCH, ss));
}


/**
 *
 */
static int
be_resolve_item(const char *url, prop_t *item)
{
  if(spotify_start(NULL, 0, 0))
    return -1;

  spotify_page_t *sp = calloc(1, sizeof(spotify_page_t));

  sp->sp_url = strdup(url);

  sp->sp_model = prop_ref_inc(item);
  
  sp->sp_type = prop_ref_inc(prop_create(sp->sp_model, "type"));

  add_metadata_props(sp);
  spotify_msg_enq(spotify_msg_build(SPOTIFY_RESOLVE_ITEM, sp));
  return 0;
}


/**
 *
 */
static backend_t be_spotify = {
  .be_init = be_spotify_init,
  .be_canhandle = be_spotify_canhandle,
  .be_open = be_spotify_open,
  .be_play_audio = be_spotify_play,
  .be_imageloader = be_spotify_imageloader,
  .be_search = be_spotify_search,
  .be_resolve_item = be_resolve_item,
};

BE_REGISTER(spotify);
