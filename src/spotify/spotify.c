/*
 *  Backend using Spotify
 *  Copyright (C) 2009 Andreas Öman
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

#include "showtime.h"
#include "navigator.h"
#include "playqueue.h"
#include "media.h"
#include "notifications.h"
#include "api.h"
#include "keyring.h"
#include "ptrvec.h"

#include <dlfcn.h>
#include "apifunctions.h"
#include "spotify_app_key.h"


/**
 *
 */
static media_pipe_t *spotify_mp;
static hts_mutex_t spotify_mutex;
static hts_cond_t spotify_cond_main;
static hts_cond_t spotify_cond_login;
static int spotify_started;
static int spotify_login_result = -1;

static int play_position;
static int in_seek;
static int seek_pos;

static sp_session *spotify_session;
TAILQ_HEAD(spotify_msg_queue, spotify_msg);
static struct spotify_msg_queue spotify_msgs;

typedef enum {
  METADATA_TRACK,
  METADATA_ALBUM_NAME,
  METADATA_ALBUM_YEAR,
  METADATA_ALBUM_ARTIST_NAME,
  METADATA_ALBUM_IMAGE,
  METADATA_ARTIST_NAME,
} metadata_type_t;


/**
 * Binds a property to metadata in spotify
 * This will make sure the properties stays updated with the
 * spotify version of the track.
 * The structure self-destructs when the property tree is deleted (by
 * a subscription that checks PROP_DESTROYED)
 */
typedef struct metadata {
  LIST_ENTRY(metadata) m_link;
  prop_t *m_prop;
  void *m_source;
  metadata_type_t m_type;
} metadata_t;

static LIST_HEAD(, metadata) metadatas;
static hts_mutex_t meta_mutex;

/**
 * Playlist support
 *
 *
 */
prop_t *prop_playlists;

static ptrvec_t playlists;


typedef struct playlist {
  
  sp_playlist *pl_playlist;

  char *pl_url;
  ptrvec_t pl_tracks;
  int pl_position;
  prop_t *pl_prop_root;
  prop_t *pl_prop_tracks;
  prop_t *pl_prop_title;

} playlist_t;


typedef struct playlist_track {
  sp_track *plt_track;

  prop_t *plt_prop_root;
  prop_t *plt_prop_metadata;
  prop_t *plt_prop_title;

} playlist_track_t;

typedef enum {
  SPOTIFY_PENDING_EVENT, //< event pending from libspotify
  SPOTIFY_LOGOUT,
  SPOTIFY_LOAD_URI,
  SPOTIFY_PLAY_TRACK,
  SPOTIFY_SCANDIR,
  SPOTIFY_STOP_PLAYBACK,
  SPOTIFY_SEARCH,
  SPOTIFY_RELEASE_TRACK,
  SPOTIFY_RELEASE_ALBUM,
  SPOTIFY_RELEASE_ARTIST,
  SPOTIFY_RELEASE_IMAGE,
  SPOTIFY_SEEK,
  SPOTIFY_PAUSE,
  SPOTIFY_GET_IMAGE,
} spotify_msg_type_t;

/**
 * A spotify URI
 */
typedef struct spotify_uri {
  const char *su_uri;

  char *su_errbuf;
  size_t su_errlen;
  int su_errcode;

  int su_content_type; /* from showtime.h */
  prop_t *su_metadata;
  nav_dir_t *su_dir;

  prop_t *su_nodes;

  const char *su_preferred_view;

  prop_t *su_prop_artist_name;
  prop_t *su_prop_album_name;
  prop_t *su_prop_album_year;
  prop_t *su_prop_album_image;
  
  prop_t *su_playlist_title;

  sp_link *su_playlist_link;

  sp_track *su_track;

} spotify_uri_t;

static hts_cond_t spotify_cond_uri;
static spotify_uri_t *su_playing, *su_pending;


/**
 * A spotify search query
 */
typedef struct spotify_search {
  char *ss_query;
  prop_t *ss_results;
} spotify_search_t;


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
  uint8_t *si_id;

  int si_errcode;
  
  prop_pixmap_t *si_pixmap;

} spotify_image_t;

static hts_cond_t spotify_cond_image;


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
static int
spotify_try_login(sp_session *s, int retry, const char *reason) 
{
  char *username;
  char *password;
  int r;

  r = keyring_lookup("Login to Spotify", &username, &password, NULL, retry,
		     "spotify:", reason);

  if(r == -1) {
    hts_mutex_lock(&spotify_mutex);
    spotify_login_result = -2;
    pthread_cond_broadcast(&spotify_cond_login);
    hts_mutex_unlock(&spotify_mutex);
    return -1;
  }

  if(r == 1) {
    /* Nothing found, but we must have a username / password */
    return spotify_try_login(s, 1, NULL);
  }

  f_sp_session_login(s, username, password);

  free(username);
  free(password);
  return 0;
}


/**
 *
 */
static void
spotify_logged_in(sp_session *sess, sp_error error)
{
  if(error == 0) {
    notify_add(NOTIFY_INFO, NULL, 5, "Spotify: Logged in");
    hts_mutex_lock(&spotify_mutex);
    spotify_login_result = 0;
    pthread_cond_broadcast(&spotify_cond_login);
    hts_mutex_unlock(&spotify_mutex);
  } else {
    notify_add(NOTIFY_ERROR, NULL, 5, "Spotify: Login failed -- %s",
	       f_sp_error_message(error));
    spotify_try_login(sess, 1, f_sp_error_message(error));
  }
}


/**
 *
 */
static void
spotify_logged_out(sp_session *sess)
{
  notify_add(NOTIFY_INFO, NULL, 5, "Spotify: Logged out");

  hts_mutex_lock(&spotify_mutex);
  spotify_login_result = -1;
  pthread_cond_broadcast(&spotify_cond_login);
  hts_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
static void
spotify_connection_error(sp_session *sess, sp_error error)
{
  notify_add(NOTIFY_ERROR, NULL, 5, "Spotify: Connection error\n%s",
	     f_sp_error_message(error));
}


/**
 *
 */
static void
spotify_events_pending(sp_session *sess)
{
  spotify_msg_enq(spotify_msg_build(SPOTIFY_PENDING_EVENT, NULL));
}


/**
 *
 */
static void
spotify_play_token_lost(sp_session *sess)
{
  notify_add(NOTIFY_ERROR, NULL, 5, 
	     "Spotify: Playback paused, another client is using this account");
  if(spotify_mp != NULL)
    mp_enqueue_event(spotify_mp, event_create_type(EVENT_INTERNAL_PAUSE));

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
 * Processing done, signal back to calling thread
 */
static void
spotify_uri_return(spotify_uri_t *su, int errcode)
{
  if(su->su_playlist_link != NULL)
    f_sp_link_release(su->su_playlist_link);

  if(su->su_track != NULL)
    f_sp_track_release(su->su_track);

  pthread_mutex_lock(&spotify_mutex);
  su->su_errcode = errcode;
  hts_cond_broadcast(&spotify_cond_uri);
  pthread_mutex_unlock(&spotify_mutex);
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
  media_queue_t *mq = &mp->mp_audio;
  media_buf_t *mb;

  if(su == NULL)
    return num_frames;

  if(num_frames == 0) {

    if(in_seek) {
      play_position = (int64_t)seek_pos * format->sample_rate / 1000;
      in_seek--;
    } else {
      mp_enqueue_event(mp, event_create_type(EVENT_EOF));
    }
    return 0;
  }

  if(mq->mq_len > 100)
    return 0;

  mb = media_buf_alloc();
  mb->mb_data_type = MB_AUDIO;
  
  mb->mb_size = num_frames * 2 * sizeof(int16_t);
  mb->mb_data = malloc(mb->mb_size);
  mb->mb_channels = format->channels;
  mb->mb_rate = format->sample_rate;

  mb->mb_time = play_position * 1000000LL / format->sample_rate;
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
    snprintf(su->su_errbuf, su->su_errlen, "Unable to load track:\n%s",
	     f_sp_error_message(err));
    spotify_uri_return(su, 1);
    return;
  }

  TRACE(TRACE_DEBUG, "spotify", "Starting playback of track: %s (%s)", 
	su->su_uri, f_sp_track_name(su->su_track));

  mp_become_primary(spotify_mp);
  spotify_mp->mp_audio.mq_stream = 0; // Must be set to somthing != -1
  play_position = 0;

  if((err = f_sp_session_player_play(spotify_session, 1))) {
    snprintf(su->su_errbuf, su->su_errlen, "Unable to play track:\n%s",
	     f_sp_error_message(err));
    spotify_uri_return(su, 1);
    return;
  }

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
set_image_uri(prop_t *p, const uint8_t *id)
{
  if(id == NULL)
    return;

  prop_set_stringf(p, "spotify:image:"
		   "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
		   "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
		   id[ 0],id[ 1],id[ 2],id[ 3],id[ 4], 
		   id[ 5],id[ 6],id[ 7],id[ 8],id[ 9], 
		   id[10],id[11],id[12],id[13],id[14], 
		   id[15],id[16],id[17],id[18],id[19]);
}


/**
 *
 */
static void
spotify_metadata_update_track(prop_t *meta, sp_track *track)
{
  sp_album *album;
  char txt[1024];
  int nartists, i;

  if(!f_sp_track_is_loaded(track))
    return;

  album = f_sp_track_album(track);

  txt[0] = 0;
  nartists = f_sp_track_num_artists(track);
  for(i = 0; i < nartists; i++)
    snprintf(txt + strlen(txt), sizeof(txt) - strlen(txt),
	     "%s%s", strlen(txt) ? ", " : "", 
	     f_sp_artist_name(f_sp_track_artist(track, i)));

  prop_set_string(prop_create(meta, "mediaformat"), "spotify");
  prop_set_string(prop_create(meta, "type"), "audio");
  prop_set_string(prop_create(meta, "title"), f_sp_track_name(track));
  prop_set_int(prop_create(meta, "trackindex"), f_sp_track_index(track));
  prop_set_float(prop_create(meta, "duration"), 
		 (float)f_sp_track_duration(track) / 1000.0);
  if(album != NULL) {
    prop_set_string(prop_create(meta, "album"), f_sp_album_name(album));
    set_image_uri(prop_create(meta, "album_art"), f_sp_album_cover(album));
  }
		  
  prop_set_string(prop_create(meta, "artist"), txt);
}


 
/**
 *
 */
static void
spotify_metadata_update_albumname(prop_t *p, sp_album *album)
{
  prop_set_string(p, f_sp_album_name(album));
}

 
/**
 *
 */
static void
spotify_metadata_update_albumyear(prop_t *p, sp_album *album)
{
  int y = f_sp_album_year(album);
  if(y)
    prop_set_int(p, y);
}


/**
 *
 */
static void
spotify_metadata_update_albumartistname(prop_t *p, sp_album *album)
{
  sp_artist *artist = f_sp_album_artist(album);
  if(artist != NULL)
    prop_set_string(p, f_sp_artist_name(artist));
}


/**
 *
 */
static void
spotify_metadata_update_artistname(prop_t *p, sp_artist *artist)
{
  prop_set_string(p, f_sp_artist_name(artist));
}


/**
 *
 */
static void
spotify_metadata_update_albumimage(prop_t *p, sp_album *album)
{
  set_image_uri(p, f_sp_album_cover(album));
}


/**
 *
 */
static void
metadata_update(metadata_t *m)
{
  switch(m->m_type) {
  case METADATA_TRACK:
    spotify_metadata_update_track(m->m_prop, m->m_source);
    break;
    
  case METADATA_ALBUM_NAME:
    spotify_metadata_update_albumname(m->m_prop, m->m_source);
    break;

  case METADATA_ALBUM_YEAR:
    spotify_metadata_update_albumyear(m->m_prop, m->m_source);
    break;

  case METADATA_ALBUM_ARTIST_NAME:
    spotify_metadata_update_albumartistname(m->m_prop, m->m_source);
    break;

  case METADATA_ALBUM_IMAGE:
    spotify_metadata_update_albumimage(m->m_prop, m->m_source);
    break;
    
  case METADATA_ARTIST_NAME:
    spotify_metadata_update_artistname(m->m_prop, m->m_source);
    break;
  }
}

/**
 *
 */
static void
spotify_metadata_updated(sp_session *sess)
{
  metadata_t *m;

  LIST_FOREACH(m, &metadatas, m_link)
    metadata_update(m);

  spotify_play_track_try();
}

/**
 *
 */
static void
metadata_prop_cb(void *opaque, prop_event_t event, ...)
{
  metadata_t *m = opaque;
  spotify_msg_type_t r;
  prop_t *p;
  prop_sub_t *s;
  va_list ap;
  va_start(ap, event);

  if(event != PROP_DESTROYED) 
    return;

  p = va_arg(ap, prop_t *);
  s = va_arg(ap, prop_sub_t *);

  prop_unsubscribe(s);

  LIST_REMOVE(m, m_link);
  prop_ref_dec(m->m_prop);

  /* Since we are not in the spotify thread, but all accessed to
     the API must go there, we post a request to release
     the reference */
  
  switch(m->m_type) {
  case METADATA_TRACK:
    r = SPOTIFY_RELEASE_TRACK;
    break;
    
  case METADATA_ALBUM_NAME:
  case METADATA_ALBUM_YEAR:
  case METADATA_ALBUM_ARTIST_NAME:
  case METADATA_ALBUM_IMAGE:
    r = SPOTIFY_RELEASE_ALBUM;
    break;

   case METADATA_ARTIST_NAME:
    r = SPOTIFY_RELEASE_ARTIST;
    break;
  default:
    abort();
  }

  spotify_msg_enq(spotify_msg_build(r, m->m_source));
  free(m);
}


static void
metadata_create(prop_t *p, metadata_type_t type, void *source)
{
  metadata_t *m = malloc(sizeof(metadata_t));

  prop_ref_inc(p);
  m->m_prop = p;
  m->m_type = type;
  m->m_source = source;

  switch(m->m_type) {
  case METADATA_TRACK:
    f_sp_track_add_ref(source);
    break;
    
  case METADATA_ALBUM_NAME:
  case METADATA_ALBUM_YEAR:
  case METADATA_ALBUM_IMAGE:
  case METADATA_ALBUM_ARTIST_NAME:
    f_sp_album_add_ref(source);
    break;

  case METADATA_ARTIST_NAME:
    f_sp_artist_add_ref(source);
    break;
  }

  hts_mutex_lock(&meta_mutex);
  LIST_INSERT_HEAD(&metadatas, m, m_link);
  hts_mutex_unlock(&meta_mutex);

  prop_subscribe(PROP_SUB_TRACK_DESTROY,
		 PROP_TAG_CALLBACK, metadata_prop_cb, m,
		 PROP_TAG_MUTEX, &meta_mutex,
		 PROP_TAG_ROOT, p,
		 NULL);
  
  metadata_update(m);
}




/**
 * Load an album into the property tree passed via userdata
 */
static void
spotify_browse_album_callback(sp_albumbrowse *result, void *userdata)
{
  prop_t *nodes = userdata, *p;
  sp_track *track;
  sp_album *album;
  int i, ntracks;
  char url[256];

  ntracks = f_sp_albumbrowse_num_tracks(result);
  album = f_sp_albumbrowse_album(result);

  for(i = 0; i < ntracks; i++) {
    track = f_sp_albumbrowse_track(result, i);

    p = prop_create(NULL, "node");

    spotify_make_link(f_sp_link_create_from_track(track, 0), 
		      url, sizeof(url));
    prop_set_string(prop_create(p, "url"), url);

    metadata_create(prop_create(p, "metadata"), METADATA_TRACK, track);

    if(prop_set_parent(p, nodes))
      prop_destroy(p);

  }
  prop_ref_dec(nodes);
  f_sp_albumbrowse_release(result);
}

#if 0
/**
 *
 */
static void
spotify_browse_artist_callback(sp_artistbrowse *result, void *userdata)
{
  prop_t *nodes = userdata;
#if 0
  prop_t *p;
  sp_artist *artist;
  sp_album *album;
  int i, nalbums;
  char link[256];
  prop_t *metadata, *tracks, *p_img;

  artist = f_sp_artistbrowse_artist(result);
  nalbums = f_sp_artistbrowse_num_albums(result);
  
  for(i = 0; i < nalbums; i++) {
    album = f_sp_artistbrowse_album(result, i);
    artist = f_sp_album_artist(album);

    p = prop_create(NULL, "node");

    spotify_make_link(f_sp_album_link(album), link, sizeof(link));
    prop_set_string(prop_create(p, "url"), link);

    metadata = prop_create(p, "metadata");
    prop_set_string(prop_create(metadata, "type"), "album");
    prop_set_string(prop_create(metadata, "title"), f_sp_album_name(album));
    prop_set_string(prop_create(metadata, "artist"), f_sp_artist_name(artist));

    tracks = prop_create(p, "tracks");
    prop_ref_inc(tracks);
    f_sp_create_albumbrowse(spotify_session, album, 
			  spotify_browse_album_callback, tracks);
    
    spotify_load_image(f_sp_image_create(spotify_session, 
					 f_sp_album_cover(album)),
		       prop_create(metadata, "icon"));

    if(prop_set_parent(p, nodes))
      prop_destroy(p);
  }

#endif
  prop_ref_dec(nodes);
  f_sp_artistbrowse_release(result);
}
#endif


/**
 *
 */
static playlist_t *
playlist_find(const char *url)
{
  playlist_t *pl;
  int i = 0;

  for(i = 0; i < ptrvec_size(&playlists); i++) {
    pl = ptrvec_get_entry(&playlists, i);
    if(pl->pl_url != NULL && !strcmp(pl->pl_url, url))
      return pl;
  }
  return NULL;
}


/**
 *
 */
static void
spotify_load_uri(spotify_uri_t *su)
{
  sp_link *l;
  sp_linktype type;
  playlist_t *pl;

  if((l = f_sp_link_create_from_string(su->su_uri)) == NULL) {
    snprintf(su->su_errbuf, su->su_errlen, "Invalid spotify URI");
    spotify_uri_return(su, 1);
    return;
  }

  type = f_sp_link_type(l);

  switch(type) {
  case SP_LINKTYPE_TRACK:
    su->su_content_type = CONTENT_AUDIO;

    su->su_metadata = prop_create(NULL, "metadata");
    metadata_create(su->su_metadata, METADATA_TRACK, f_sp_link_as_track(l));
    break;

  case SP_LINKTYPE_ALBUM:
    su->su_content_type = CONTENT_DIR;

    su->su_preferred_view = "album";
    su->su_nodes = prop_create(NULL, "nodes");

    /* Launch a browse request */

    prop_ref_inc(su->su_nodes);
    f_sp_albumbrowse_create(spotify_session, f_sp_link_as_album(l), 
			    spotify_browse_album_callback, su->su_nodes);

    su->su_prop_album_name = prop_create(NULL, "album_name");
    metadata_create(su->su_prop_album_name, METADATA_ALBUM_NAME, 
		    f_sp_link_as_album(l));

    su->su_prop_album_year = prop_create(NULL, "album_year");
    metadata_create(su->su_prop_album_year, METADATA_ALBUM_YEAR,
		    f_sp_link_as_album(l));

    su->su_prop_album_image = prop_create(NULL, "album_art");
    metadata_create(su->su_prop_album_image, METADATA_ALBUM_IMAGE,
		    f_sp_link_as_album(l));

    su->su_prop_artist_name = prop_create(NULL, "artist_name");
    metadata_create(su->su_prop_artist_name, METADATA_ALBUM_ARTIST_NAME, 
		    f_sp_link_as_album(l));
    break;

  case SP_LINKTYPE_PLAYLIST:

    pl = playlist_find(su->su_uri);

    if(pl == NULL) {
      snprintf(su->su_errbuf, su->su_errlen, "Playlist %s not found",
	       su->su_uri);
      f_sp_link_release(l);
      spotify_uri_return(su, 1);
      return;
    }

    su->su_playlist_title = pl->pl_prop_title;
    prop_ref_inc(su->su_playlist_title);

    su->su_nodes = pl->pl_prop_tracks;
    su->su_content_type = CONTENT_PLAYLIST;
    break;

  default:
    snprintf(su->su_errbuf, su->su_errlen, "Can not handle linktype %d",
	     type);
    f_sp_link_release(l);
    spotify_uri_return(su, 1);
    return;
  }

  f_sp_link_release(l);
  spotify_uri_return(su, 0);
}


/**
 *
 */
static void
spotify_scandir_album_callback(sp_albumbrowse *result, void *userdata)
{
  spotify_uri_t *su = userdata;
  sp_track *track;
  sp_album *album;
  nav_dir_t *nd;
  int i, ntracks;
  char url[256];
  prop_t *metadata;

  nd = nav_dir_alloc();
  
  ntracks = f_sp_albumbrowse_num_tracks(result);
  album = f_sp_albumbrowse_album(result);

  for(i = 0; i < ntracks; i++) {
    track = f_sp_albumbrowse_track(result, i);

    metadata = prop_create(NULL, "metadata");
    metadata_create(metadata, METADATA_TRACK, track);

    spotify_make_link(f_sp_link_create_from_track(track, 0), 
		      url, sizeof(url));
    nav_dir_add(nd, url, f_sp_track_name(track), CONTENT_AUDIO, metadata);
  }

  su->su_dir = nd;
  spotify_uri_return(su, 0);
}


/**
 *
 */
static void
spotify_scandir_from_playlist(spotify_uri_t *su, playlist_t *pl)
{
  playlist_track_t *plt;
  nav_dir_t *nd = nav_dir_alloc();
  char url[256];
  prop_t *metadata;
  int i;

  for(i = 0; i < ptrvec_size(&pl->pl_tracks); i++) {
    plt = ptrvec_get_entry(&pl->pl_tracks, i);

    metadata = prop_create(NULL, "metadata");

    prop_link(plt->plt_prop_metadata, metadata);

    spotify_make_link(f_sp_link_create_from_track(plt->plt_track, 0), 
		      url, sizeof(url));
    nav_dir_add(nd, url, f_sp_track_name(plt->plt_track),
		CONTENT_AUDIO, metadata);
  }
  su->su_dir = nd;
  spotify_uri_return(su, 0);
}


/**
 *
 */
static void
spotify_scandir(spotify_uri_t *su)
{
  sp_link *l;
  sp_linktype type;
  playlist_t *pl;

  if((l = f_sp_link_create_from_string(su->su_uri)) == NULL) {
    snprintf(su->su_errbuf, su->su_errlen, "Invalid spotify URI");
    spotify_uri_return(su, 1);
    return;
  }

  type = f_sp_link_type(l);

  switch(type) {
  case SP_LINKTYPE_ALBUM:
    f_sp_albumbrowse_create(spotify_session, f_sp_link_as_album(l),
			    spotify_scandir_album_callback, su);
    return;

  case SP_LINKTYPE_PLAYLIST:
    if((pl = playlist_find(su->su_uri)) == NULL) {
      snprintf(su->su_errbuf, su->su_errlen, "Playlist %s not found",
	       su->su_uri);
      break;
    }

    spotify_scandir_from_playlist(su, pl);
    return;

  default:
    snprintf(su->su_errbuf, su->su_errlen, "Can not handle linktype %d",
	     type);
    break;
  }
  f_sp_link_release(l);
  spotify_uri_return(su, 1);
}


/**
 *
 */
static void
search_cleanup(spotify_search_t *ss)
{
  prop_ref_dec(ss->ss_results);
  free(ss->ss_query);
  free(ss);
}


/**
 *
 */
static void
search_completed(sp_search *result, void *userdata)
{
  spotify_search_t *ss = userdata;
  int i, nalbums, ntracks;
  sp_track *track;
  sp_album *album;
  sp_artist *artist;
  sp_image *image;
  char link[256];
  prop_t *p, *metadata, *tracks, *p_img;


  printf("Search \"%s\" completed\n", ss->ss_query);
  printf("  %d tracks (total: %d)\n", f_sp_search_num_tracks(result),
	 f_sp_search_total_tracks(result));
  printf("  %d albums\n", f_sp_search_num_albums(result));
  printf("  %d artists\n", f_sp_search_num_artists(result));

  /**
   * Albums
   */
  nalbums = f_sp_search_num_albums(result);
  
  for(i = 0; i < nalbums; i++) {
    album = f_sp_search_album(result, i);
    artist = f_sp_album_artist(album);

    p = prop_create(NULL, "node");

    spotify_make_link(f_sp_link_create_from_album(album), link, sizeof(link));
    prop_set_string(prop_create(p, "url"), link);

    metadata = prop_create(p, "metadata");
    prop_set_string(prop_create(metadata, "type"), "album");
    prop_set_string(prop_create(metadata, "title"), f_sp_album_name(album));
    prop_set_string(prop_create(metadata, "artist"), f_sp_artist_name(artist));


    tracks = prop_create(p, "tracks");
    prop_ref_inc(tracks);
    f_sp_albumbrowse_create(spotify_session, album, 
			  spotify_browse_album_callback, tracks);

    
    image = f_sp_image_create(spotify_session, f_sp_album_cover(album));

    p_img = prop_create(metadata, "icon");
    prop_ref_inc(p_img);
    //    f_sp_image_add_load_callback(image, spotify_image_to_property, p_img);

    if(prop_set_parent(p, ss->ss_results))
      prop_destroy(p);
  }

  /**
   * Tracks
   */
  ntracks = f_sp_search_num_tracks(result);

  for(i = 0; i < ntracks; i++) {
    track = f_sp_search_track(result, i);

    p = prop_create(NULL, "node");

    spotify_make_link(f_sp_link_create_from_track(track, 0), link, sizeof(link));
    prop_set_string(prop_create(p, "url"), link);

    metadata_create(prop_create(p, "metadata"), METADATA_TRACK, track);

    if(prop_set_parent(p, ss->ss_results))
      prop_destroy(p);
  }

  f_sp_search_release(result);
  search_cleanup(ss);
}



/**
 *
 */
static void
spotify_search(spotify_search_t *ss)
{
  if(f_sp_search_create(spotify_session, ss->ss_query,
		      0, 250, search_completed, ss) == NULL)
    search_cleanup(ss);
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
};


/**
 *
 */
static void 
tracks_added(sp_playlist *plist, const sp_track **tracks,
	     int num_tracks, int position, void *userdata)
{
  playlist_t *pl = userdata;
  sp_track *t;
  playlist_track_t *plt, *before;
  int i, pos;
  char url[128];

  for(i = 0; i < num_tracks; i++) {
    pos = position + i;
    plt = calloc(1, sizeof(playlist_track_t));
    t = (sp_track *)tracks[i];
    
    before = ptrvec_get_entry(&pl->pl_tracks, pos);

    plt->plt_prop_root = prop_create(NULL, NULL);
    plt->plt_track = t;

    prop_set_string(prop_create(plt->plt_prop_root, "type"), "audio");

    spotify_make_link(f_sp_link_create_from_track(t, 0), url, sizeof(url));
    prop_set_string(prop_create(plt->plt_prop_root, "url"), url);

    plt->plt_prop_metadata = prop_create(plt->plt_prop_root, "metadata");

    metadata_create(plt->plt_prop_metadata, METADATA_TRACK, t);

    if(prop_set_parent_ex(plt->plt_prop_root, pl->pl_prop_tracks,
			  before ? before->plt_prop_root : NULL, NULL)) {
      abort();
    }

    ptrvec_insert_entry(&pl->pl_tracks, pos, plt);
  }
#if 0
  for(i = 0; i < ptrvec_size(&pl->pl_tracks); i++) {
    plt = ptrvec_get_entry(&pl->pl_tracks, i);
    printf("%4d. %s\n", i, f_sp_track_name(plt->plt_track));
  }
#endif
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
  playlist_track_t *plt;
  int i;

  /* Sort so we always delete from the end. Better safe then sorry */
  positions = alloca(num_tracks * sizeof(int));
  memcpy(positions, tracks, sizeof(int) * num_tracks);
  qsort(positions, num_tracks, sizeof(int), intcmp_dec);

  for(i = 0; i < num_tracks; i++) {
    plt = ptrvec_remove_entry(&pl->pl_tracks, positions[i]);
    prop_destroy(plt->plt_prop_root);
    free(plt);
  }

#if 0
  for(i = 0; i < ptrvec_size(&pl->pl_tracks); i++) {
    plt = ptrvec_get_entry(&pl->pl_tracks, i);
    printf("%4d. %s\n", i, f_sp_track_name(plt->plt_track));
  }
#endif
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
  playlist_track_t *before, *plt;
  const sp_track **tvec;

  /* Sort so we always delete from the end. Better safe then sorry */
  positions = alloca(num_tracks * sizeof(int));
  memcpy(positions, tracks, sizeof(int) * num_tracks);
  qsort(positions, num_tracks, sizeof(int), intcmp_dec);

  before = ptrvec_get_entry(&pl->pl_tracks, new_position);

  tvec = alloca(num_tracks * sizeof(sp_track *));

  for(i = 0; i < num_tracks; i++) {
    plt = ptrvec_remove_entry(&pl->pl_tracks, positions[i]);
    tvec[num_tracks - i - 1] = plt->plt_track;
    prop_destroy(plt->plt_prop_root);
    free(plt);
  }

  for(i = 0; i < ptrvec_size(&pl->pl_tracks); i++)
    if(before == ptrvec_get_entry(&pl->pl_tracks, i))
      break;

  tracks_added(plist, tvec, num_tracks, i, userdata);
}


/**
 *
 */
static void 
playlist_renamed(sp_playlist *plist, void *userdata)
{
  playlist_t *pl = userdata;
  const char *name = f_sp_playlist_name(plist);

  prop_set_string(pl->pl_prop_title, name);
  TRACE(TRACE_DEBUG, "spotify", "Playlist renamed to %s", name);
}


/**
 *
 */
static void 
playlist_update(sp_playlist *plist, bool done, void *userdata)
{
  playlist_t *pl = userdata;
  char url[128];

  if(!done || !f_sp_playlist_is_loaded(plist))
    return;

  spotify_make_link(f_sp_link_create_from_playlist(plist), url, sizeof(url));
  pl->pl_url = strdup(url);
  prop_set_string(prop_create(pl->pl_prop_root, "url"), url);
}


/**
 * Callbacks for individual playlists
 */
static sp_playlist_callbacks pl_callbacks = {
  .tracks_added     = tracks_added,
  .tracks_removed   = tracks_removed,
  .tracks_moved     = tracks_moved,
  .playlist_renamed = playlist_renamed,
  .playlist_update_in_progress = playlist_update,
};


/**
 * A new playlist has been added to the users rootlist
 */
static void
playlist_added(sp_playlistcontainer *pc, sp_playlist *plist,
	       int position, void *userdata)
{
  playlist_t *pl = calloc(1, sizeof(playlist_t));
  prop_t *metadata;
  
  pl->pl_playlist = plist;
  pl->pl_position = position;
  pl->pl_prop_root = prop_create(prop_playlists, NULL);

  pl->pl_prop_tracks = prop_create(pl->pl_prop_root, "nodes");

  prop_set_string(prop_create(pl->pl_prop_root, "type"), "directory");

  metadata = prop_create(pl->pl_prop_root, "metadata");

  pl->pl_prop_title = prop_create(metadata, "title");
  prop_set_string(pl->pl_prop_title, f_sp_playlist_name(plist));

  ptrvec_insert_entry(&playlists, position, pl);

  f_sp_playlist_add_callbacks(plist, &pl_callbacks, pl);

  TRACE(TRACE_DEBUG, "spotify", "Playlist %d added (%s)", 
	position, f_sp_playlist_name(plist));
}


/**
 * Playlist container callbacks
 */
static sp_playlistcontainer_callbacks pc_callbacks = {
  .playlist_added   = playlist_added,
  //  .playlist_removed = playlist_removed,
};


/**
 *
 */
static void
spotify_got_image(sp_image *image, void *userdata)
{
  spotify_image_t *si = userdata;
  int pitch;
  void *pixels;

  pixels = f_sp_image_lock_pixels(image, &pitch);
  si->si_pixmap = prop_pixmap_create(f_sp_image_width(image), 
				     f_sp_image_height(image),
				     pitch, PIX_FMT_RGB24, pixels);
  f_sp_image_unlock_pixels(image);

  pthread_mutex_lock(&spotify_mutex);
  si->si_errcode = 0;
  hts_cond_broadcast(&spotify_cond_image);
  pthread_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
static void
spotify_get_image(spotify_image_t *si)
{
  f_sp_image_add_load_callback(f_sp_image_create(spotify_session, si->si_id),
			       spotify_got_image, si);
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
  struct timespec ts;
  spotify_msg_t *sm;
  int next_timeout = 0;
  char ua[256];
  extern char *htsversion_full;

  sesconf.api_version = SPOTIFY_API_VERSION;
  sesconf.cache_location = "/tmp/spotify";
  sesconf.settings_location = "/tmp/spotify";
  sesconf.application_key = appkey;
  sesconf.application_key_size = sizeof(appkey);
  snprintf(ua, sizeof(ua), "Showtime %s", htsversion_full);
  sesconf.user_agent = ua;
  sesconf.callbacks = &spotify_session_callbacks;
  

  error = f_sp_session_init(&sesconf, &s);
  hts_mutex_lock(&spotify_mutex);
  if(error) {
    spotify_login_result = error;
    pthread_cond_broadcast(&spotify_cond_login);
    hts_mutex_unlock(&spotify_mutex);
    return NULL;
  }

  spotify_session = s;

  f_sp_playlistcontainer_add_callbacks(f_sp_session_playlistcontainer(s),
				       &pc_callbacks,
				       NULL);

  spotify_try_login(s, 0, NULL);

  /* Wakeup any sleepers that are waiting for us to start */

  while(1) {
     if(next_timeout == 0) {
      while((sm = TAILQ_FIRST(&spotify_msgs)) == NULL)
	pthread_cond_wait(&spotify_cond_main, &spotify_mutex);

    } else {

      clock_gettime(CLOCK_REALTIME, &ts);
      
      ts.tv_sec += next_timeout / 1000;
      ts.tv_nsec += (next_timeout % 1000) * 1000000;
      if(ts.tv_nsec > 1000000000) {
	ts.tv_sec++;
	ts.tv_nsec -= 1000000000;
      }

      while((sm = TAILQ_FIRST(&spotify_msgs)) == NULL)
	if(pthread_cond_timedwait(&spotify_cond_main,
				  &spotify_mutex, &ts) == ETIMEDOUT)
	  break;
    }

    if(sm != NULL)
      TAILQ_REMOVE(&spotify_msgs, sm, sm_link);
   
    pthread_mutex_unlock(&spotify_mutex);

    if(sm != NULL) {
      switch(sm->sm_op) {
      case SPOTIFY_PENDING_EVENT:
	break;
      case SPOTIFY_LOGOUT:
	TRACE(TRACE_DEBUG, "spotify", "Requesting logout");
	f_sp_session_logout(s);
	break;
      case SPOTIFY_LOAD_URI:
	spotify_load_uri(sm->sm_ptr);
	break;
      case SPOTIFY_SCANDIR:
	spotify_scandir(sm->sm_ptr);
	break;
      case SPOTIFY_PLAY_TRACK:
	spotify_play_track(sm->sm_ptr);
	break;
      case SPOTIFY_STOP_PLAYBACK:
	f_sp_session_player_unload(s);
	break;
      case SPOTIFY_SEARCH:
	spotify_search(sm->sm_ptr);
	break;
      case SPOTIFY_RELEASE_TRACK:
	f_sp_track_release(sm->sm_ptr);
	break;
      case SPOTIFY_RELEASE_ALBUM:
	f_sp_album_release(sm->sm_ptr);
	break;
      case SPOTIFY_RELEASE_ARTIST:
	f_sp_artist_release(sm->sm_ptr);
	break;
      case SPOTIFY_RELEASE_IMAGE:
	f_sp_image_release(sm->sm_ptr);
	break;
      case SPOTIFY_SEEK:
	if(spotify_mp == NULL)
	  break;

	mp_flush(spotify_mp);
	
	in_seek = 2;
	seek_pos = sm->sm_int;
	error = f_sp_session_player_seek(s, sm->sm_int);
	break;

      case SPOTIFY_PAUSE:
	f_sp_session_player_play(s, !sm->sm_int);
	break;

      case SPOTIFY_GET_IMAGE:
	spotify_get_image(sm->sm_ptr);
	break;
      }
      free(sm);
    }

    do {
      f_sp_session_process_events(s, &next_timeout);
    } while(next_timeout == 0);

    pthread_mutex_lock(&spotify_mutex);
  }
}


/**
 *
 */
static int
spotify_start(char *errbuf, size_t errlen)
{
  hts_mutex_lock(&spotify_mutex);
  
  if(spotify_started == 0) {
    hts_thread_create_detached(spotify_thread, NULL);
    spotify_started = 1;
  }

  while(spotify_login_result == -1)
    hts_cond_wait(&spotify_cond_login, &spotify_mutex);

  if(spotify_login_result != 0) {
    /* Login error occured */
    snprintf(errbuf, errlen, "Unable to login\n%s",
	     spotify_login_result == -2 ? "User rejected" :
	     f_sp_error_message(spotify_login_result));
    return 1;
  }
  return 0;
}


/**
 *
 */
static int
be_spotify_search(const char *url, const char *query, nav_page_t **npp,
		  char *errbuf, size_t errlen)
{
  spotify_search_t *ss = malloc(sizeof(spotify_search_t));
  nav_page_t *np;
  prop_t *p;

  np = nav_page_create(url, sizeof(nav_page_t), NULL,
		       NAV_PAGE_DONT_CLOSE_ON_BACK);
  p = np->np_prop_root;
  
  ss->ss_results = prop_create(p, "nodes");
  prop_ref_inc(ss->ss_results);

  ss->ss_query = strdup(query);

  spotify_msg_enq_locked(spotify_msg_build(SPOTIFY_SEARCH, ss));
  hts_mutex_unlock(&spotify_mutex);

  prop_set_string(prop_create(p, "type"), "directory");
  prop_set_string(prop_create(p, "view"), "list");
  *npp = np;
  return 0;
}


/**
 *
 */
static int
be_spotify_rootlist(const char *url, nav_page_t **npp,
		    char *errbuf, size_t errlen)
{
  nav_page_t *n;
  prop_t *nodes;

  hts_mutex_unlock(&spotify_mutex);

  *npp = n = nav_page_create(url, sizeof(nav_page_t), NULL,
			     NAV_PAGE_DONT_CLOSE_ON_BACK);

  prop_set_string(prop_create(n->np_prop_root, "type"), "directory");
  prop_set_string(prop_create(n->np_prop_root, "view"), "list");

  nodes = prop_create(n->np_prop_root, "nodes");
  prop_set_string(prop_create(n->np_prop_root, "title"), "Spotify playlists");

  prop_link(prop_playlists, nodes);
  return 0;
}


/**
 *
 */
static int
be_spotify_open(const char *url, const char *type, const char *parent,
		nav_page_t **npp, char *errbuf, size_t errlen)
{
  spotify_uri_t su;
  nav_page_t *np;
  prop_t *p;

  if(spotify_start(errbuf, errlen))
    return -1;

  if(!strncmp(url, "spotify:search:", strlen("spotify:search:")))
    return be_spotify_search(url, url + strlen("spotify:search:"),
					  npp, errbuf, errlen);

  if(!strcmp(url, "spotify:playlists"))
    return be_spotify_rootlist(url, npp, errbuf, errlen);

  memset(&su, 0, sizeof(su));
  su.su_uri = url;
  su.su_errbuf = errbuf;
  su.su_errlen = errlen;
  su.su_errcode = -1;
  su.su_content_type = CONTENT_UNKNOWN;
  su.su_preferred_view = "list";

  TRACE(TRACE_DEBUG, "spotify", "Loading URL: %s", url);

  spotify_msg_enq_locked(spotify_msg_build(SPOTIFY_LOAD_URI, &su));

  while(su.su_errcode == -1)
    hts_cond_wait(&spotify_cond_uri, &spotify_mutex);
  
  hts_mutex_unlock(&spotify_mutex);

  if(su.su_errcode)
    return -1; /* errbuf is filled in by spotify thread */

  switch(su.su_content_type) {

  case CONTENT_AUDIO:
    playqueue_play(url, parent, su.su_metadata, 0);
    *npp = NULL;
    break;

  case CONTENT_DIR:
    np = nav_page_create(url, sizeof(nav_page_t), NULL,
			  NAV_PAGE_DONT_CLOSE_ON_BACK);
    p = np->np_prop_root;

    prop_set_string(prop_create(p, "type"), "directory");
    prop_set_string(prop_create(p, "view"), su.su_preferred_view);

    if(prop_set_parent(su.su_nodes, p))
      abort();
    su.su_nodes = NULL;

    if(prop_set_parent(su.su_prop_artist_name, p))
      abort();
    su.su_prop_artist_name = NULL;

    if(prop_set_parent(su.su_prop_album_name, p))
      abort();
    su.su_prop_album_name = NULL;

    if(prop_set_parent(su.su_prop_album_year, p))
      abort();
    su.su_prop_album_year = NULL;

    if(prop_set_parent(su.su_prop_album_image, p))
      abort();
    su.su_prop_album_image = NULL;

    *npp = np;
    break;

  case CONTENT_PLAYLIST:
    np = nav_page_create(url, sizeof(nav_page_t), NULL,
			  NAV_PAGE_DONT_CLOSE_ON_BACK);
    p = np->np_prop_root;

    prop_set_string(prop_create(p, "type"), "directory");
    prop_set_string(prop_create(p, "view"), su.su_preferred_view);
    prop_link(su.su_playlist_title, prop_create(p, "title"));
    prop_link(su.su_nodes, prop_create(p, "nodes"));

    su.su_nodes = NULL;

    *npp = np;
    break;

    break;

  default:
    snprintf(errbuf, errlen, "Can not handle contents");
    return -1;
  }

  assert(su.su_nodes == NULL);
  assert(su.su_prop_artist_name == NULL);
  assert(su.su_prop_album_name == NULL);
  assert(su.su_prop_album_year == NULL);
  assert(su.su_prop_album_image == NULL);
  return 0;
}


/**
 * Play given track.
 *
 * We only expect this to be called from the playqueue system.
 */
static event_t *
be_spotify_play(const char *url, media_pipe_t *mp, 
		char *errbuf, size_t errlen)
{
  spotify_uri_t su;
  event_t *e;
  event_ts_t *ets;
  int hold = 0, lost_focus = 0;
  media_queue_t *mq = &mp->mp_audio;
  
  memset(&su, 0, sizeof(su));

  if(spotify_start(errbuf, errlen))
    return NULL;

  assert(spotify_mp == NULL);
  spotify_mp = mp;

  su.su_uri = url;
  su.su_errbuf = errbuf;
  su.su_errlen = errlen;
  su.su_errcode = -1;
  
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

  mp_set_playstatus_by_hold(mp, hold);

  /* Playback successfully started, wait for events */
  while(1) {
    e = mp_dequeue_event(mp);

    if(event_is_action(e, ACTION_PREV_TRACK) ||
       event_is_action(e, ACTION_NEXT_TRACK) ||
       event_is_action(e, ACTION_STOP) ||
       event_is_type  (e, EVENT_PLAYQUEUE_JUMP) ||
       event_is_type (e, EVENT_EOF)) {

      mp_flush(mp);
      break;

    } else if(event_is_type(e, EVENT_SEEK)) {

      ets = (event_ts_t *)e;
      spotify_msg_enq(spotify_msg_build_int(SPOTIFY_SEEK, ets->pts / 1000));

    } else if(event_is_action(e, ACTION_PLAYPAUSE) ||
	      event_is_action(e, ACTION_PLAY) ||
	      event_is_action(e, ACTION_PAUSE)) {

      hold = action_update_hold_by_event(hold, e);
      spotify_msg_enq(spotify_msg_build_int(SPOTIFY_PAUSE, hold));
      mp_send_cmd_head(mp, mq, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      mp_set_playstatus_by_hold(mp, hold);
      lost_focus = 0;

    } else if(event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

      hold = 1;
      lost_focus = 1;
      spotify_msg_enq(spotify_msg_build_int(SPOTIFY_PAUSE, 1));
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold);

    } else if(event_is_type(e, EVENT_MP_IS_PRIMARY)) {

      if(lost_focus) {
	hold = 0;
	lost_focus = 0;
	spotify_msg_enq(spotify_msg_build_int(SPOTIFY_PAUSE, 0));
	mp_send_cmd_head(mp, mq, MB_CTRL_PLAY);
	mp_set_playstatus_by_hold(mp, hold);
      }

    } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

      hold = 1;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);

    }
    event_unref(e);
  }

  if(hold) {
    // If we were paused, release playback again.
    mp_send_cmd(mp, mq, MB_CTRL_PLAY);
    mp_set_playstatus_by_hold(mp, 0);
  }

  spotify_mp = NULL;
  spotify_msg_enq(spotify_msg_build(SPOTIFY_STOP_PLAYBACK, NULL));
  return e;
}



/**
 *
 */
static nav_dir_t *
be_spotify_scandir(const char *url, char *errbuf, size_t errlen)
{
  spotify_uri_t su;

  memset(&su, 0, sizeof(su));

  if(spotify_start(errbuf, errlen))
    return NULL;

  su.su_uri = url;
  su.su_errbuf = errbuf;
  su.su_errlen = errlen;
  su.su_errcode = -1;

  spotify_msg_enq_locked(spotify_msg_build(SPOTIFY_SCANDIR, &su));

  while(su.su_errcode == -1)
    hts_cond_wait(&spotify_cond_uri, &spotify_mutex);
  
  hts_mutex_unlock(&spotify_mutex);
  return su.su_dir;
}


/**
 *
 */
static unsigned int
hex2v(int s)
{
  switch(s) {
  case '0' ... '9':
    return s - '0';
  case 'a' ... 'f':
    return s - 'a' + 10;
  case 'A' ... 'F':
    return s - 'A' + 10;
  default:
    return 0;
  }
}



/**
 *
 */
static int
parse_image_url(uint8_t *out, const char *url)
{
  int i;
  uint8_t v;

  if(strncmp(url, "spotify:image:", strlen("spotify:image:")))
    return -1;

  url += strlen("spotify:image:");

  for(i = 0; i < 20; i++) {

    if(*url == 0)
      return -1;

    v = hex2v(*url++);
    if(*url == 0)
      return -1;

    v = (v << 4) | hex2v(*url++);
    *out++ = v;
  }
  return 0;
}



/**
 *
 */
static int
be_spotify_imageloader(const char *url, char *errbuf, size_t errlen,
		       int *thumb, void **data, size_t *datasize,
		       int *codecid, const char *theme, prop_pixmap_t **pp)
{
  spotify_image_t si;
  uint8_t id[20];

  memset(&si, 0, sizeof(si));

  if(parse_image_url(id, url)) {
    snprintf(errbuf, errlen, "Invalid URL for Spotify imageloader");
    return -1;
  }

  if(spotify_start(errbuf, errlen))
    return -1;

  si.si_id = id;
  si.si_errcode = -1;

  spotify_msg_enq_locked(spotify_msg_build(SPOTIFY_GET_IMAGE, &si));

  while(si.si_errcode == -1)
    hts_cond_wait(&spotify_cond_image, &spotify_mutex);

  hts_mutex_unlock(&spotify_mutex);

  if(si.si_errcode == 0) {
    *pp = si.si_pixmap;
    return 0;
  } else {
    snprintf(errbuf, errlen, "Unable to load image");
    return -1;
  }

}


/**
 *
 */
static int
be_spotify_init(void)
{
  void *h;
  const char *sym;

  h = dlopen("libspotify.so", RTLD_LAZY);
  if(h == NULL) {
    TRACE(TRACE_INFO, "spotify", "Unable to load libspotify.so: %s",
	  dlerror());
    return 1;
  }
  if((sym = resolvesym(h)) != NULL) {
    TRACE(TRACE_ERROR, "spotify", "Unable to resolve symbol \"%s\"", sym);
    dlclose(h);
    return 1;
  }

  prop_playlists = prop_create(prop_get_global(), "spotify_playlists");

  TAILQ_INIT(&spotify_msgs);

  hts_mutex_init(&spotify_mutex);
  hts_cond_init(&spotify_cond_main);
  hts_cond_init(&spotify_cond_login);
  hts_cond_init(&spotify_cond_uri);
  hts_cond_init(&spotify_cond_image);

  /* Metadata tracking */
  hts_mutex_init(&meta_mutex);

  return 0;
}


/**
 *
 */
static int
be_spotify_canhandle(const char *url)
{
  return !strncmp(url, "spotify", strlen("spotify"));
}


/**
 *
 */
void spotify_shutdown(void);

void
spotify_shutdown(void)
{
  int done;
  struct timespec ts;

  return;


  ts.tv_sec = time(NULL) + 5; // Wait max 5 seconds for logout to succeed
  ts.tv_nsec = 0;

  hts_mutex_lock(&spotify_mutex);

  if(spotify_started && spotify_login_result == 0) {

    done = 0;

    spotify_msg_enq_locked(spotify_msg_build(SPOTIFY_LOGOUT, &done));

    while(spotify_login_result != -1)
      if(hts_cond_wait_timeout(&spotify_cond_login, 
			       &spotify_mutex, &ts) == ETIMEDOUT)
	break;
  }

  hts_mutex_unlock(&spotify_mutex);
}


/**
 *
 */
nav_backend_t be_spotify = {
  .nb_init = be_spotify_init,
  .nb_canhandle = be_spotify_canhandle,
  .nb_open = be_spotify_open,
  .nb_play_audio = be_spotify_play,
  .nb_scandir = be_spotify_scandir,
  .nb_imageloader = be_spotify_imageloader,
};
