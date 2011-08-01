/*
 *  CD Audio player
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
#include "config.h"

#include "event.h"
#include "media.h"
#include "showtime.h"
#include "navigator.h"
#include "backend/backend.h"
#include "playqueue.h"

#include <cdio/cdda.h>
#if ENABLE_CDDB
#include <cddb/cddb.h>
#endif

#include <libavutil/sha.h>


hts_mutex_t cd_meta_mutex;

LIST_HEAD(cd_meta_list, cd_meta);

struct cd_meta_list cd_metas;

/**
 * Internal representation of a CD track
 */
typedef struct cd_track {
  prop_t *ct_metadata;
  prop_t *ct_root;
  int ct_start;
  int ct_end;

} cd_track_t;


/**
 * Internal representation of a CD disc
 * 
 * We never destroy those, I don't think the user will load that many
 * CDs during the lifetime of the app so this will be a problem
 */
typedef struct cd_meta {

  LIST_ENTRY(cd_meta) cm_link;

  uint8_t cm_id[20];

  prop_t *cm_root;

  prop_t *cm_nodes;
  prop_t *cm_model;
  prop_t *cm_meta;

  int cm_length; // in frames

  int cm_ntracks;

  cd_track_t cm_tracks[0]; // Must be last
} cd_meta_t;


/**
 * Create a SHA hash based on a CD
 */
static void
makeid(uint8_t *out, CdIo_t *cdio)
{
  int ntracks = cdio_get_num_tracks(cdio);

  int *lba = alloca(sizeof(int) * (ntracks + 1));
  int i;

  for(i = 0; i < ntracks + 1; i++)
    lba[i] = cdio_get_track_lba(cdio, i + 1);

  struct AVSHA *shactx = alloca(av_sha_size);
  av_sha_init(shactx, 160);
  av_sha_update(shactx, (const uint8_t *)lba, sizeof(int) * ntracks + 1);
  av_sha_final(shactx, out);
}

/**
 *
 */
#if ENABLE_CDDB
static void *
cddb_thread(void *aux)
{
  cd_meta_t *cm = aux;
  int i, r;
  cddb_disc_t *disc = cddb_disc_new();
  cddb_conn_t *conn = cddb_new();

  cddb_set_charset(conn, "UTF-8");

  cddb_disc_set_length(disc, FRAMES_TO_SECONDS(cm->cm_length));
  for(i = 0; i < cm->cm_ntracks; i++) {
    cd_track_t *ct = &cm->cm_tracks[i];
    cddb_track_t *track = cddb_track_new();
    cddb_disc_add_track(disc, track);
    cddb_track_set_frame_offset(track, ct->ct_start);
  }

  cddb_disc_calc_discid(disc);

  if((r = cddb_query(conn, disc)) > 0) {

    r = cddb_read(conn, disc);
    if(r == 1) {
      rstr_t *title = rstr_alloc(cddb_disc_get_title(disc));
      
      prop_set_rstring(prop_create(cm->cm_meta, "title"), title);
      prop_set_rstring(prop_create(cm->cm_meta, "album_name"), title);

      prop_set_string(prop_create(cm->cm_meta, "artist_name"),
		      cddb_disc_get_artist(disc));

      int year = cddb_disc_get_year(disc);
      if(year)
	prop_set_int(prop_create(cm->cm_meta, "album_year"), year);

      prop_set_string(prop_create(cm->cm_meta, "genre"),
		      cddb_disc_get_genre(disc));

      for(i = 0; i < cm->cm_ntracks; i++) {
	cd_track_t *ct = &cm->cm_tracks[i];
	cddb_track_t *track = cddb_disc_get_track(disc, i);
      
	prop_set_string(prop_create(ct->ct_metadata, "title"),
			cddb_track_get_title(track));
	prop_set_string(prop_create(ct->ct_metadata, "artist"),
			cddb_track_get_artist(track));
	prop_set_rstring(prop_create(ct->ct_metadata, "album"), title);
      }

      rstr_release(title);

    } else {
      TRACE(TRACE_INFO, "CDDB", "Read failed: %s", 
	    cddb_error_str(cddb_errno(conn)));
    }
  } else if(r < 0) {
    TRACE(TRACE_INFO, "CDDB", "Query failed: %s", 
	    cddb_error_str(cddb_errno(conn)));
  } else {
    TRACE(TRACE_DEBUG, "CDDB", "No match for disc");
  }

  cddb_disc_destroy(disc);
  cddb_destroy(conn);
  return NULL;
}
#endif

/**
 *
 */
static cd_meta_t *
get_cd_meta(const char *device)
{
  uint8_t id[20];
  cd_meta_t *cm;
  int tracks, i;
  CdIo_t *cdio;

  if((cdio = cdio_open(device, DRIVER_UNKNOWN)) == NULL)
    return NULL;


  makeid(id, cdio);

  hts_mutex_lock(&cd_meta_mutex);

  LIST_FOREACH(cm, &cd_metas, cm_link)
    if(!memcmp(id, cm->cm_id, 20))
      break;

  if(cm == NULL) {

    tracks = cdio_get_num_tracks(cdio);

    cm = calloc(1, sizeof(cd_meta_t) + tracks * sizeof(cd_track_t));
    memcpy(cm->cm_id, id, 20);

    cm->cm_length = cdio_get_track_lba(cdio, CDIO_CDROM_LEADOUT_TRACK);
  
    cm->cm_ntracks = tracks;

    cm->cm_root   = prop_create_root(NULL);
    cm->cm_model = prop_create(cm->cm_root, "model");
    cm->cm_nodes  = prop_create(cm->cm_model, "nodes");
    cm->cm_meta   = prop_create(cm->cm_model, "metadata");

    prop_set_string(prop_create(cm->cm_model, "type"), "directory");
    prop_set_string(prop_create(cm->cm_model, "contents"), "albumTracks");

    rstr_t *audio = rstr_alloc("audio");
  
    for(i = 0; i < tracks; i++) {
      cd_track_t *ct = &cm->cm_tracks[i];

      char trackurl[URL_MAX];
      char title[64];

      ct->ct_root = prop_create_root(NULL);

      prop_set_rstring(prop_create(ct->ct_root, "type"), audio);

      snprintf(trackurl, sizeof(trackurl), "audiocd:%s/%d", device, i + 1);
      prop_set_string(prop_create(ct->ct_root, "url"), trackurl);
    
      ct->ct_metadata = prop_create(ct->ct_root, "metadata");

      snprintf(title, sizeof(title), "Track #%d", i + 1);
      prop_set_string(prop_create(ct->ct_metadata, "title"), title);

      ct->ct_start = cdio_get_track_lba(cdio, i + 1);
      ct->ct_end   = cdio_get_track_lba(cdio, i + 2);

      if(ct->ct_start != CDIO_INVALID_LBA && ct->ct_end != CDIO_INVALID_LBA)
	prop_set_float(prop_create(ct->ct_metadata, "duration"), 
		       (float)(ct->ct_end - ct->ct_start)
		       / CDIO_CD_FRAMES_PER_SEC);

      if(prop_set_parent(ct->ct_root, cm->cm_nodes))
	abort();
    }
    rstr_release(audio);
    LIST_INSERT_HEAD(&cd_metas, cm, cm_link);

#if ENABLE_CDDB
    hts_thread_create_detached("CDDB query", cddb_thread, cm,
			       THREAD_PRIO_NORMAL);
#endif
  }
  hts_mutex_unlock(&cd_meta_mutex);
  cdio_destroy(cdio);
  return cm;
}


/**
 *
 */
static cd_track_t *
get_track(cd_meta_t *cm, int track)
{
  if(track < 1 || track > cm->cm_ntracks)
    return NULL;
  return &cm->cm_tracks[track - 1];
}


/**
 *
 */
static int
parse_audiocd_url(const char *url, char *device, size_t devlen)
{
  const char *t;
  int track;

  if(strncmp(url, "audiocd:", strlen("audiocd:")))
    return -1;

  url += strlen("audiocd:");

  if((t = strrchr(url, '/')) == NULL)
    return -1;

  if(isdigit((int)t[1])) {
    track = atoi(t + 1);
    if(track < 1)
      return -1;
  } else {
    track = 0;
    t = NULL;
  }
  
  if(device != NULL) {
    while(devlen > 1 && *url && url != t) {
      *device++ = *url++;
      devlen--;
    }
    *device = 0;
  }
  return track;
}


/**
 *
 */
static int
canhandle(const char *url)
{
  return parse_audiocd_url(url, NULL, 0) >= 0;
}


/**
 *
 */
static int
openpage(prop_t *page, const char *url)
{
  int track;
  char device[32];
  
  if((track = parse_audiocd_url(url, device, sizeof(device))) < 0)
    return nav_open_errorf(page, _("Invalid CD URL"));

  cd_meta_t *cm = get_cd_meta(device);

  if(cm == NULL)
    return nav_open_errorf(page, _("Unable to open CD"));

  if(track) {
    cd_track_t *ct = get_track(cm, track);
    if(ct == NULL)
      return nav_open_errorf(page, _("Invalid CD track"));

    prop_t *meta = prop_create_root("metadata");
    prop_link(ct->ct_metadata, meta);

    playqueue_play(url, meta, 0);
    return playqueue_open(page);
  }

  prop_set_int(prop_create(page, "directClose"), 1);
  prop_link(cm->cm_root, page);
  return 0;
}


/**
 *
 */
static int
cdseek(media_pipe_t *mp, media_buf_t **mbp, int first, int last, int lsn)
{
  mp_flush(mp, 0);
  
  if(*mbp != NULL) {
    media_buf_free(*mbp);
    *mbp = NULL;
  }

  if(lsn < first)
    lsn = first;
  if(lsn > last)
    lsn = last;
  return lsn;
}


/**
 *
 */
static event_t *
playaudio(const char *url, media_pipe_t *mp, char *errstr, size_t errlen,
	  int hold, const char *mimetype)
{
  int track;
  char device[32];
  lsn_t lsn, track_first, track_last;
  cdrom_drive_t *cdda;
  media_buf_t *mb = NULL;
  media_queue_t *mq;
  event_t *e;
  int lost_focus = 0, eject = 0;

  if((track = parse_audiocd_url(url, device, sizeof(device))) < 1) {
    snprintf(errstr, errlen, "Invalid URL");
    return NULL;
  }

  if((cdda = cdio_cddap_identify(device, 0, NULL)) == NULL) {
    snprintf(errstr, errlen, "Unable to open CD");
    return NULL;
  }

  cdio_cddap_open(cdda);

  cdio_cddap_speed_set(cdda, 2);
  
  lsn = track_first = cdio_cddap_track_firstsector(cdda, track);
  track_last = cdio_cddap_track_lastsector(cdda, track);
 
  TRACE(TRACE_DEBUG, "AudioCD", "Starting playback of track %d", track);

  mp_configure(mp, MP_PLAY_CAPS_SEEK | MP_PLAY_CAPS_PAUSE | 
	       MP_PLAY_CAPS_EJECT, MP_BUFFER_NONE);
  mp_become_primary(mp);
  mq = &mp->mp_audio;

  mp_set_playstatus_by_hold(mp, hold, NULL);

  while(1) {

    if(lsn > track_last) {
      e = event_create_type(EVENT_EOF);
      break;
    }

    if(mb == NULL) {

      mb = media_buf_alloc();
      mb->mb_data_type = MB_AUDIO;
      
      mb->mb_size = CDIO_CD_FRAMESIZE_RAW * 2;
      mb->mb_data = malloc(mb->mb_size);
      mb->mb_channels = 2;
      mb->mb_rate = 44100;
      mb->mb_time = (lsn - track_first) * 1000000LL / CDIO_CD_FRAMES_PER_SEC;

      if(cdio_cddap_read(cdda, mb->mb_data, lsn, 2) != 2)
	memset(mb->mb_data, 0, mb->mb_size);
      lsn+=2;
    }

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }      

    if(event_is_type(e, EVENT_PLAYQUEUE_JUMP)) {
      mp_flush(mp, 0);
      break;

    } else if(event_is_type(e, EVENT_SEEK)) {
      event_ts_t *ets = (event_ts_t *)e;
      
      lsn = cdseek(mp, &mb, track_first, track_last,
		   track_first + ets->pts * CDIO_CD_FRAMES_PER_SEC /1000000LL);
      
    } else if(event_is_action(e, ACTION_SEEK_FAST_BACKWARD)) {

      lsn = cdseek(mp, &mb, track_first, track_last,
		   lsn - CDIO_CD_FRAMES_PER_SEC * 60);

    } else if(event_is_action(e, ACTION_SEEK_BACKWARD)) {

      lsn = cdseek(mp, &mb, track_first, track_last,
		   lsn - CDIO_CD_FRAMES_PER_SEC * 15);

    } else if(event_is_action(e, ACTION_SEEK_FAST_FORWARD)) {

      lsn = cdseek(mp, &mb, track_first, track_last,
		   lsn + CDIO_CD_FRAMES_PER_SEC * 60);

    } else if(event_is_action(e, ACTION_SEEK_FORWARD)) {

      lsn = cdseek(mp, &mb, track_first, track_last,
		   lsn + CDIO_CD_FRAMES_PER_SEC * 15);

    } else if(event_is_action(e, ACTION_PLAYPAUSE) ||
	      event_is_action(e, ACTION_PLAY) ||
	      event_is_action(e, ACTION_PAUSE)) {

      hold = action_update_hold_by_event(hold, e);
      mp_send_cmd_head(mp, mq, hold ? MB_CTRL_PAUSE : MB_CTRL_PLAY);
      lost_focus = 0;
      mp_set_playstatus_by_hold(mp, hold, NULL);

    } else if(event_is_type(e, EVENT_MP_NO_LONGER_PRIMARY)) {

      hold = 1;
      lost_focus = 1;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_type(e, EVENT_MP_IS_PRIMARY)) {

      if(lost_focus) {
	hold = 0;
	lost_focus = 0;
	mp_send_cmd_head(mp, mq, MB_CTRL_PLAY);
	mp_set_playstatus_by_hold(mp, hold, NULL);
      }

    } else if(event_is_type(e, EVENT_INTERNAL_PAUSE)) {

      hold = 1;
      lost_focus = 0;
      mp_send_cmd_head(mp, mq, MB_CTRL_PAUSE);
      mp_set_playstatus_by_hold(mp, hold, e->e_payload);

    } else if(event_is_action(e, ACTION_PREV_TRACK) ||
	      event_is_action(e, ACTION_NEXT_TRACK) ||
	      event_is_action(e, ACTION_STOP)) {
      mp_flush(mp, 0);
      break;
    } else if(event_is_action(e, ACTION_EJECT)) {
      mp_flush(mp, 0);
      eject = 1;
      break;
    }
    event_release(e);
  }

  if(mb != NULL)
    media_buf_free(mb);

  cdio_cddap_close(cdda);

  if(eject)
    cdio_eject_media_drive(device);

  return e;
}



/**
 *
 */
static backend_t be_cdda = {
  .be_canhandle = canhandle,
  .be_open = openpage,
  .be_play_audio = playaudio,
};

BE_REGISTER(cdda);
