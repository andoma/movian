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
#include <stdio.h>

#include "prop/prop.h"
#include "networking/http_server.h"
#include "htsmsg/htsmsg_xml.h"
#include "event.h"
#include "playqueue.h"
#include "misc/str.h"

#include "upnp.h"

static char *upnp_current_url;
static char *upnp_current_title;
static char *upnp_current_album;
static char *upnp_current_album_art;
static char *upnp_current_artist;
static char *upnp_current_playstatus;
static char *upnp_current_type;
static int   upnp_current_track_duration;
static int   upnp_current_track_time;
static int   upnp_current_total_tracks;
static int   upnp_current_track;
static int   upnp_current_shuffle;
static int   upnp_current_repeat;
static int   upnp_current_canSkipBackward;
static int   upnp_current_canSkipForward;
static int   upnp_current_canSeek;
static int   upnp_current_canPause;
static int   upnp_current_canStop;


  // TransportState
static const char *
current_playstate(void)
{
  if(upnp_current_playstatus == NULL)
    return "NO_MEDIA_PRESENT";
  else if(!strcmp(upnp_current_playstatus, "play"))
    return "PLAYING";
  else if(!strcmp(upnp_current_playstatus, "pause"))
    return "PAUSED_PLAYBACK";
  else
    return "NO_MEDIA_PRESENT";
}

/**
 *
 */
static const char *
current_playMode(void)
{
  if(upnp_current_shuffle)
    return "SHUFFLE";
  else if(upnp_current_repeat)
    return "REPEAT_ALL";
  return "NORMAL";
}


/**
 *
 */
static void
current_transportActions(char *str, size_t len)
{  
  snprintf(str, len, "Play%s%s%s%s%s",
	   upnp_current_canSkipBackward ? ",Previous" : "",
	   upnp_current_canSkipForward  ? ",Next"     : "",
	   upnp_current_canSeek         ? ",Seek"     : "",
	   upnp_current_canPause        ? ",Pause"    : "",
	   upnp_current_canStop         ? ",Stop"     : "");
}



/**
 *
 */
static htsmsg_t *
avt_Stop(http_connection_t *hc, htsmsg_t *args,
	 const char *myhost, int myport)
{
  event_dispatch(event_create_action(ACTION_STOP));
  return NULL;
}


/**
 *
 */
static htsmsg_t *
avt_Pause(http_connection_t *hc, htsmsg_t *args,
	  const char *myhost, int myport)
{
  event_dispatch(event_create_action(ACTION_PAUSE));
  return NULL;
}




/**
 *
 */
static htsmsg_t *
avt_Seek(http_connection_t *hc, htsmsg_t *args,
	  const char *myhost, int myport)
{
  return NULL;
}


/**
 *
 */
static htsmsg_t *
avt_Play(http_connection_t *hc, htsmsg_t *args,
	 const char *myhost, int myport)
{
  event_dispatch(event_create_action(ACTION_PLAY));
  return NULL;
}

/**
 *
 */
static htsmsg_t *
avt_Next(http_connection_t *hc, htsmsg_t *args,
	 const char *myhost, int myport)
{
  event_dispatch(event_create_action(ACTION_SKIP_FORWARD));
  return NULL;
}

/**
 *
 */
static htsmsg_t *
avt_Previous(http_connection_t *hc, htsmsg_t *args,
	     const char *myhost, int myport)
{
  event_dispatch(event_create_action(ACTION_SKIP_BACKWARD));
  return NULL;
}


/**
 *
 */
static int
play_with_context(const char *uri, htsmsg_t *meta)
{
  const char *parentid, *id;
  upnp_service_t *us;

  parentid =
    htsmsg_get_str_multi(meta,
			 "DIDL-Lite",
			 "item",
			 "parentID",
			 NULL);

  if(parentid == NULL)
    return 1;

  id =
    htsmsg_get_str_multi(meta,
			 "DIDL-Lite",
			 "item",
			 "id",
			 NULL);

  if(id == NULL)
    return 1;

  UPNP_TRACE("Playing %s (id: %s, parent: %s)", uri, id, parentid);

  hts_mutex_lock(&upnp_lock);

  us = upnp_service_guess(uri);

  if(us != NULL) {
    UPNP_TRACE("Using controlpoint %s", us->us_control_url);

    prop_t *model = prop_create_root(NULL);
    prop_t *nodes = prop_create(model, "nodes");
    prop_t *t = NULL;

    if(upnp_browse_children(us->us_control_url, parentid, nodes, id, &t) 
       || t == NULL) {

      prop_destroy(model);

    } else {
      playqueue_load_with_source(t, model, PQ_PAUSED);
      hts_mutex_unlock(&upnp_lock);
      return 0;
    }
  }

  hts_mutex_unlock(&upnp_lock);
  return 1;
}


/**
 *
 */
static htsmsg_t *
avt_SetAVTransportURI(http_connection_t *hc, htsmsg_t *args,
		      const char *myhost, int myport)
{
  const char *uri = htsmsg_get_str(args, "CurrentURI");
  const char *metaxml = htsmsg_get_str(args, "CurrentURIMetaData");
  char errbuf[200];
  htsmsg_t *meta;

  if(uri == NULL)
    return NULL;

  if(metaxml == NULL) {
    playqueue_play(uri, prop_create_root(NULL), 1);
    return NULL;
  }

  meta = htsmsg_xml_deserialize_cstr(metaxml, errbuf, sizeof(errbuf));
  if(meta == NULL) {
    TRACE(TRACE_ERROR, "UPNP", 
	  "SetAVTransportURI: Unable to parse metadata -- %s", errbuf);
    return NULL;
  }
  
  if(play_with_context(uri, meta)) {
    // Failed to play from context
    // TODO: Fix metadata here
    playqueue_play(uri, prop_create_root(NULL), 1);
  }
  htsmsg_release(meta);
  return NULL;
}


/**
 *
 */
static char *
build_didl(const char *myhost, int myport)
{
  htsbuf_queue_t hq;

  htsbuf_queue_init(&hq, 0);

  htsbuf_qprintf(&hq,
		 "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
		 "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
		 "xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0\" "
		 "xmlns:pv=\"http://www.pv.com/pvns/\" "
		 "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">"
		 "<item id=\"101\" parentID=\"100\" restricted=\"0\">"
		 "<upnp:class xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">object.item.audioItem.musicTrack</upnp:class>");

  if(upnp_current_title) {
    htsbuf_qprintf(&hq,
		   "<dc:title xmlns:dc=\"http://purl.org/dc/elements/1.1/\">");
    htsbuf_append_and_escape_xml(&hq, upnp_current_title);
    htsbuf_qprintf(&hq, "</dc:title>");
  }


  if(upnp_current_artist) {
    htsbuf_qprintf(&hq,
		   "<upnp:artist xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">");
    htsbuf_append_and_escape_xml(&hq, upnp_current_artist);
    htsbuf_qprintf(&hq, "</upnp:artist>");
  }
  


  if(upnp_current_album) {
    htsbuf_qprintf(&hq,
		   "<upnp:album xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">");
    htsbuf_append_and_escape_xml(&hq, upnp_current_album);
    htsbuf_qprintf(&hq, "</upnp:album>");
  }

  if(upnp_current_album_art) {

    char url[URL_MAX];
    const char *arturl;

    if(strncmp(upnp_current_album_art, "http://", strlen("http://"))) {
      snprintf(url, sizeof(url), "http://%s:%d/api/image/%s",
	       myhost, myport, upnp_current_album_art);
      arturl = url;
    } else {
      arturl = upnp_current_album_art;
    }

    htsbuf_qprintf(&hq,
		   "<upnp:albumArtURI xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\">");
    htsbuf_append_and_escape_xml(&hq, arturl);
    htsbuf_qprintf(&hq, "</upnp:albumArtURI>");
  }
  
  htsbuf_qprintf(&hq, "</item></DIDL-Lite>");

  return htsbuf_to_string(&hq);
}




/**
 *
 */
static void
fmttime(char *out, size_t outlen, unsigned int t)
{
  snprintf(out, outlen, "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
}


/**
 *
 */
static htsmsg_t *
avt_GetPositionInfo(http_connection_t *hc, htsmsg_t *args,
		    const char *myhost, int myport)
{
  htsmsg_t *out = htsmsg_create_map();
  char tbuf[16];

  hts_mutex_lock(&upnp_lock);

  char *didl = build_didl(http_get_my_host(hc), http_get_my_port(hc));

  htsmsg_add_u32(out, "Track", upnp_current_track);

  fmttime(tbuf, sizeof(tbuf), upnp_current_track_duration);
  htsmsg_add_str(out, "TrackDuration", tbuf);
  
  htsmsg_add_str(out, "TrackMetaData", didl);
  free(didl);
  htsmsg_add_str(out, "TrackURI", upnp_current_url ?: "");
  
  fmttime(tbuf, sizeof(tbuf), upnp_current_track_time);
  htsmsg_add_str(out, "RelTime", tbuf);
  htsmsg_add_str(out, "AbsTime", tbuf); //"NOT_IMPLEMENTED");
  htsmsg_add_u32(out, "RelCount", 2147483647);
  htsmsg_add_u32(out, "AbsCount", 2147483647);

  hts_mutex_unlock(&upnp_lock);
  return out;
}



/**
 *
 */
static htsmsg_t *
avt_GetTransportInfo(http_connection_t *hc, htsmsg_t *args,
		     const char *myhost, int myport)
{
  htsmsg_t *out = htsmsg_create_map();

  hts_mutex_lock(&upnp_lock);
  htsmsg_add_str(out, "CurrentTransportState", current_playstate());
  htsmsg_add_str(out, "CurrentTransportStatus", "OK");
  htsmsg_add_str(out, "CurrentSpeed", "1");
 
  hts_mutex_unlock(&upnp_lock);
  return out;
}



/**
 *
 */
static htsmsg_t *
avt_GetTransportSettings(http_connection_t *hc, htsmsg_t *args,
			 const char *myhost, int myport)
{
  htsmsg_t *out = htsmsg_create_map();

  hts_mutex_lock(&upnp_lock);
  htsmsg_add_str(out, "PlayMode", current_playMode());
  hts_mutex_unlock(&upnp_lock);
  return out;
}


/**
 *
 */
static const char *
current_mediaCategory(void)
{
  if(upnp_current_playstatus == NULL)
    return "NO_MEDIA";
  if(!strcmp(upnp_current_type ?: "", "tracks"))
    return "TRACK_AWARE";
  return "TRACK_UNAWARE";
}


/**
 *
 */
static htsmsg_t *
avt_GetMediaInfo_common(http_connection_t *hc, htsmsg_t *args,
			const char *myhost, int myport, int ext)
{
  char str[256];
  htsmsg_t *out = htsmsg_create_map();

  hts_mutex_lock(&upnp_lock);
  if(ext)
    htsmsg_add_str(out, "CurrentType", current_mediaCategory());
  htsmsg_add_u32(out, "NrTracks", upnp_current_total_tracks);
  fmttime(str, sizeof(str), upnp_current_track_duration);
  htsmsg_add_str(out, "MediaDuration", str);
  htsmsg_add_str(out, "CurrentURI", upnp_current_url ?: "");

  char *meta = build_didl(myhost, myport);
  htsmsg_add_str(out, "CurrentURIMetaData", meta);
  free(meta);

  htsmsg_add_str(out, "PlayMedium", upnp_current_playstatus == NULL ?
		 "NONE" : "NETWORK");
  hts_mutex_unlock(&upnp_lock);
  return out;
}


/**
 *
 */
static htsmsg_t *
avt_GetMediaInfo_Ext(http_connection_t *hc, htsmsg_t *args,
		     const char *myhost, int myport)
{
  return avt_GetMediaInfo_common(hc, args, myhost, myport, 1);
}


/**
 *
 */
static htsmsg_t *
avt_GetMediaInfo(http_connection_t *hc, htsmsg_t *args,
		     const char *myhost, int myport)
{
  return avt_GetMediaInfo_common(hc, args, myhost, myport, 0);
}

/**
 *
 */
static htsmsg_t *
avt_GetDeviceCapabilities(http_connection_t *hc, htsmsg_t *args,
		       const char *myhost, int myport)
{
  htsmsg_t *out = avt_GetMediaInfo(hc, args, myhost, myport);
  htsmsg_add_str(out, "PlayMedia", "NETWORK");
  return out;
}


/**
 *
 */
static htsmsg_t *
avt_GetCurrentTransportActions(http_connection_t *hc, htsmsg_t *args,
			       const char *myhost, int myport)
{
  htsmsg_t *out = avt_GetMediaInfo(hc, args, myhost, myport);
  char str[256];
  current_transportActions(str, sizeof(str));
  htsmsg_add_str(out, "Actions", str);
  return out;
}


/**
 *
 */
static htsmsg_t *
avt_generate_props(upnp_local_service_t *uls, const char *myhost, int myport)
{
  char *event;
  htsbuf_queue_t xml;
  char str[256];
  const char *s;
  
  htsbuf_queue_init(&xml, 0);

  htsbuf_qprintf(&xml,
		 "<Event xmlns=\"urn:schemas-upnp-org:metadata-1-0/RCS/\">"
		 "<InstanceID val=\"0\">");


  upnp_event_encode_str(&xml, "TransportState", current_playstate());

  upnp_event_encode_str(&xml, "CurrentMediaCategory", current_mediaCategory());

  // PlaybackStorageMedium

  if(upnp_current_playstatus == NULL)
    s = "NONE";
  else
    s = "NETWORK";
  upnp_event_encode_str(&xml, "PlaybackStorageMedium", s);

  upnp_event_encode_str(&xml, "CurrentPlayMode", current_playMode());

  current_transportActions(str, sizeof(str));
  upnp_event_encode_str(&xml, "CurrentTransportActions", str);

  upnp_event_encode_int(&xml, "NumberOfTracks", upnp_current_total_tracks);
  upnp_event_encode_int(&xml, "CurrentTrack", upnp_current_track);
  upnp_event_encode_str(&xml, "AVTransportURI", upnp_current_url ?: "");
  upnp_event_encode_int(&xml, "TransportPlaySpeed", 1);

  // Metadata

  char *meta = build_didl(myhost, myport);
  upnp_event_encode_str(&xml, "AVTransportURIMetaData", meta);
  upnp_event_encode_str(&xml, "CurrentTrackMetaData", meta);
  free(meta);

  fmttime(str, sizeof(str), upnp_current_track_duration);
  upnp_event_encode_str(&xml, "CurrentTrackDuration", str);
  upnp_event_encode_str(&xml, "CurrentMediaDuration", str);

  upnp_event_encode_str(&xml, "PossibleRecordQualityModes", NULL);
  upnp_event_encode_str(&xml, "TransportStatus", "OK");
  upnp_event_encode_str(&xml, "DRMState", "UNKNOWN");
  upnp_event_encode_str(&xml, "RecordMediumWriteStatus", NULL);
  upnp_event_encode_str(&xml, "RecordStorageMedium", NULL);
  upnp_event_encode_str(&xml, "PossibleRecordStorageMedia", NULL);
  upnp_event_encode_str(&xml, "NextAVTransportURI", "");
  upnp_event_encode_str(&xml, "NextAVTransportURIMetaData", NULL);
  upnp_event_encode_str(&xml, "CurrentRecordQualityMode", NULL);
  upnp_event_encode_str(&xml, "PossiblePlaybackStorageMedia", "NETWORK");



  htsbuf_qprintf(&xml, "</InstanceID></Event>");

  event = htsbuf_to_string(&xml);

  htsmsg_t *r = htsmsg_create_map();
  htsmsg_add_str(r, "LastChange", event);
  free(event);
  return r;
}






/**
 *
 */
upnp_local_service_t upnp_AVTransport_2 = {
  .uls_name = "AVTransport",
  .uls_version = 2,
  .uls_generate_props = avt_generate_props,
  .uls_methods = {
    { "Stop", avt_Stop },
    { "SetAVTransportURI", avt_SetAVTransportURI },
    { "Play", avt_Play },
    { "Pause", avt_Pause },
    { "Seek", avt_Seek },
    { "Next", avt_Next },
    { "Previous", avt_Previous },
    { "GetPositionInfo", avt_GetPositionInfo },
    { "GetTransportInfo", avt_GetTransportInfo },
    { "GetTransportSettings", avt_GetTransportSettings },
    { "GetMediaInfo", avt_GetMediaInfo },
    { "GetMediaInfo_Ext", avt_GetMediaInfo_Ext },
    { "GetDeviceCapabilities", avt_GetDeviceCapabilities },
    { "GetCurrentTransportActions", avt_GetCurrentTransportActions },
    { NULL, NULL},
  }
};


/**
 *
 */
static void
set_current_str(void *opaque, const char *str)
{
  upnp_local_service_t *uls = &upnp_AVTransport_2;
  mystrset((char **)opaque, str);
  upnp_schedule_notify(uls);
}


/**
 *
 */
static void
set_current_int(void *opaque, int v)
{
  upnp_local_service_t *uls = &upnp_AVTransport_2;
  *(int *)opaque = v;
  upnp_schedule_notify(uls);
}


/**
 *
 */
static void
set_current_int_passive(void *opaque, int v)
{
  *(int *)opaque = v;
}




/**
 *
 */
void
upnp_avtransport_init(void)
{
  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "url"),
		 PROP_TAG_CALLBACK_STRING, set_current_str, &upnp_current_url,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "playstatus"),
		 PROP_TAG_CALLBACK_STRING, set_current_str, &upnp_current_playstatus,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "type"),
		 PROP_TAG_CALLBACK_STRING, set_current_str, &upnp_current_type,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", 
			       "metadata", "title"),
		 PROP_TAG_CALLBACK_STRING, set_current_str, &upnp_current_title,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", 
			       "metadata", "album"),
		 PROP_TAG_CALLBACK_STRING, set_current_str, &upnp_current_album,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", 
			       "metadata", "album_art"),
		 PROP_TAG_CALLBACK_STRING,
		 set_current_str, &upnp_current_album_art,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", 
			       "metadata", "artist"),
		 PROP_TAG_CALLBACK_STRING,
		 set_current_str, &upnp_current_artist,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", 
			       "metadata", "duration"),
		 PROP_TAG_CALLBACK_INT, set_current_int, &upnp_current_track_duration,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "currentTrack"),
		 PROP_TAG_CALLBACK_INT, set_current_int, &upnp_current_track,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "totalTracks"),
		 PROP_TAG_CALLBACK_INT, set_current_int, &upnp_current_total_tracks,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "currenttime"),
		 PROP_TAG_CALLBACK_INT, set_current_int_passive, &upnp_current_track_time,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "shuffle"),
		 PROP_TAG_CALLBACK_INT, set_current_int, &upnp_current_shuffle,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "repeat"),
		 PROP_TAG_CALLBACK_INT, set_current_int, &upnp_current_repeat,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "canSkipBackward"),
		 PROP_TAG_CALLBACK_INT, set_current_int, &upnp_current_canSkipBackward,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "canSkipForward"),
		 PROP_TAG_CALLBACK_INT, set_current_int, &upnp_current_canSkipForward,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "canSeek"),
		 PROP_TAG_CALLBACK_INT, set_current_int, &upnp_current_canSeek,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "canPause"),
		 PROP_TAG_CALLBACK_INT, set_current_int, &upnp_current_canPause,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "canStop"),
		 PROP_TAG_CALLBACK_INT, set_current_int, &upnp_current_canStop,
		 PROP_TAG_MUTEX, &upnp_lock,
		 NULL);

}
