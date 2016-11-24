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
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "main.h"
#include "fileaccess.h"
#include "fa_probe.h"
#include "navigator.h"
#include "media/media.h"
#include "misc/str.h"
#include "misc/isolang.h"
#include "image/jpeg.h"
#include "htsmsg/htsmsg_json.h"

#if ENABLE_LIBAV
#include <libavutil/avstring.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include "fa_libav.h"
#include "libav.h"
#endif

#if ENABLE_VMIR
#include "np/np.h"
#endif

#if ENABLE_PLUGINS
#include "plugins.h"
#endif

/**
 *
 */
static const char *
codecname(enum AVCodecID id)
{
  AVCodec *c;

  switch(id) {
  case AV_CODEC_ID_AC3:
    return "AC3";
  case AV_CODEC_ID_EAC3:
    return "EAC3";
  case AV_CODEC_ID_DTS:
    return "DTS";
  case AV_CODEC_ID_TEXT:
  case AV_CODEC_ID_MOV_TEXT:
    return "Text";
  case AV_CODEC_ID_SSA:
    return "SSA";

  default:
    c = avcodec_find_decoder(id);
    if(c)
      return c->name;
    return "Unsupported Codec";
  }
}






static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
static const uint8_t isosig[8] = {0x1, 0x43, 0x44, 0x30, 0x30, 0x31, 0x1, 0x0};
static const uint8_t gifsig[6] = {'G', 'I', 'F', '8', '9', 'a'};
static const uint8_t ttfsig[5] = {0,1,0,0,0};
static const uint8_t otfsig[4] = {'O', 'T', 'T', 'O'};
static const uint8_t pdfsig[] = {'%', 'P', 'D', 'F', '-'};
static const uint8_t offsig[8] ={0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1};

/**
 *
 */
static rstr_t *
libav_metadata_rstr(AVDictionary *m, const char *key)
{
  AVDictionaryEntry *tag;
  int len;
  rstr_t *ret;
  const char *str;
  char *d;

  if((tag = av_dict_get(m, key, NULL, 0)) == NULL) {
    if((tag = av_dict_get(m, key, NULL, AV_DICT_IGNORE_SUFFIX)) == NULL) {
      return NULL;
    }
  }

  if(!utf8_verify(tag->value))
    return NULL;

  str = tag->value;
  len = strlen(str);
  ret = rstr_allocl(str, len);
  d = rstr_data(ret);

  while(len > 0) {
    len--;
    if(d[len] <= ' ' || d[len] == '-')
      d[len] = 0;
    else
      break;
  }
  if(*d == 0 || !strncasecmp(d, "http://", 7)) {
    rstr_release(ret);
    return NULL;
  }
  return ret;
}


/**
 *
 */
static int
libav_metadata_int(AVDictionary *m, const char *key, int def)
{
  AVDictionaryEntry *tag;

  if((tag = av_dict_get(m, key, NULL, AV_DICT_IGNORE_SUFFIX)) == NULL)
    return def;

  return tag->value && tag->value[0] >= '0' && tag->value[0] <= '9' ?
    atoi(tag->value) : def;
}


#if 0
/**
 * Obtain details from playlist
 */
static void
fa_probe_playlist(metadata_t *md, const char *url, uint8_t *pb, size_t pbsize)
{
  const char *t;
  char tmp1[300];
  int i;

  t = strrchr(url, '/');
  t = t ? t + 1 : url;

  i = 0;
  while(*t && *t != '.')
    tmp1[i++] = *t++;
  tmp1[i] = 0;

  md->md_title = rstr_alloc(tmp1);

  t = strstr((char *)pb, "NumberOfEntries=");

  if(t != NULL)
    md->md_tracks = atoi(t + 16);
}
#endif


/**
 *
 */
static void
metdata_set_redirect(metadata_t *md, const char *fmt, ...)
{
  char buf[URL_MAX];
  va_list ap;
  va_start(ap, fmt);

  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  md->md_redirect = strdup(buf);
}


/**
 *
 */
static int
jpeginfo_reader(void *handle, void *buf, int64_t offset, size_t size)
{
  if(fa_seek(handle, offset, SEEK_SET) != offset)
    return -1;
  return fa_read(handle, buf, size);
}


static void
fa_probe_exif(metadata_t *md, const char *url, const uint8_t *pb,
              fa_handle_t *fh, int buflen)
{
  jpeginfo_t ji;

  if(jpeg_info(&ji, jpeginfo_reader, fh,
	       JPEG_INFO_DIMENSIONS | JPEG_INFO_ORIENTATION |
	       JPEG_INFO_METADATA,
	       pb, buflen, NULL, 0))
    return;

  md->md_time = ji.ji_time;
  md->md_manufacturer = rstr_dup(ji.ji_manufacturer);
  md->md_equipment    = rstr_dup(ji.ji_equipment);
  jpeg_info_clear(&ji);
}


/**
 * Probe file by checking its header
 */
static int
fa_probe_header(metadata_t *md, const char *url, fa_handle_t *fh,
		const char *filename, const uint8_t *buf, size_t l)
{
  uint16_t flags;

  if(l >= 256 && (!memcmp(buf, "d8:announce", 11))) {
    md->md_contenttype = CONTENT_ARCHIVE;
    metdata_set_redirect(md, "torrentfile://%s/", url);
    return 1;
  }

  if(l >= 256 && (!memcmp(buf, "d13:announce-list", 17))) {
    md->md_contenttype = CONTENT_ARCHIVE;
    metdata_set_redirect(md, "torrentfile://%s/", url);
    return 1;
  }

  if(gconf.fa_browse_archives && l >= 16 &&
     buf[0] == 'R'  && buf[1] == 'a'  && buf[2] == 'r' && buf[3] == '!' &&
     buf[4] == 0x1a && buf[5] == 0x07 && buf[6] == 0x0 && buf[9] == 0x73) {

    flags = buf[10] | buf[11] << 8;
    if((flags & 0x101) == 1) {
      /* Don't include slave volumes */
      md->md_contenttype = CONTENT_UNKNOWN;
      return 1;
    }

    metdata_set_redirect(md, "rar://%s", url);
    md->md_contenttype = CONTENT_ARCHIVE;
    return 1;
  }

  if(gconf.fa_browse_archives && l > 4 &&
     buf[0] == 0x50 && buf[1] == 0x4b && buf[2] == 0x03 && buf[3] == 0x04) {

    char path[256];
    buf_t *buf;

    snprintf(path, sizeof(path), "zip://%s/plugin.json", url);
    buf = fa_load(path, NULL);
    if(buf != NULL) {
      htsmsg_t *json = htsmsg_json_deserialize(buf_cstr(buf));
      buf_release(buf);

      if(json != NULL) {
	const char *title = htsmsg_get_str(json, "title");
	if(title != NULL && htsmsg_get_str(json, "id") != NULL &&
	   htsmsg_get_str(json, "type") != NULL) {
	  md->md_title = rstr_alloc(title);
	  md->md_contenttype = CONTENT_PLUGIN;
	  htsmsg_release(json);
	  return 1;
	}
	htsmsg_release(json);
      }
    }
    metdata_set_redirect(md, "zip://%s", url);
    md->md_contenttype = CONTENT_ARCHIVE;
    return 1;
  }

#if 0
  if(!strncasecmp((char *)buf, "[playlist]", 10)) {
    /* Playlist */
    fa_probe_playlist(md, url, buf, sizeof(buf));
    md->md_contenttype = CONTENT_PLAYLIST;
    return 1;
  }
#endif

  if(l > 16 && buf[0] == 0xff && buf[1] == 0xd8 && buf[2] == 0xff) {
    /* JPEG image */
    md->md_contenttype = CONTENT_IMAGE;
    fa_probe_exif(md, url, buf, fh, l); // Try to get more info
    return 1;
  }

  if(!memcmp(buf, pngsig, 8)) {
    /* PNG */
    md->md_contenttype = CONTENT_IMAGE;
    return 1;
  }

  if(!memcmp(buf, pdfsig, sizeof(pdfsig))) {
    /* PDF */
    md->md_contenttype = CONTENT_DOCUMENT;
    return 1;
  }

  if(!memcmp(buf, offsig, sizeof(offsig))) {
    /* MS OFFICE */
    md->md_contenttype = CONTENT_DOCUMENT;
    return 1;
  }

  if(buf[0] == 'B' && buf[1] == 'M') {
    /* BMP */
    uint32_t siz = buf[2] | (buf[3] << 8) | (buf[4] << 16) | (buf[5] << 24);
    if(siz == fa_fsize(fh)) {
      md->md_contenttype = CONTENT_IMAGE;
      return 1;
    }
  }

  if(!memcmp(buf, gifsig, sizeof(gifsig))) {
    /* GIF */
    md->md_contenttype = CONTENT_IMAGE;
    return 1;
  }

  if(!memcmp(buf, "<?xml", 5) && find_str((char *)buf, l, "<svg")) {
    /* SVG */
    md->md_contenttype = CONTENT_IMAGE;
    return 1;
  }

  if(buf[0] == '%' && buf[1] == 'P' && buf[2] == 'D' && buf[3] == 'F') {
    md->md_contenttype = CONTENT_UNKNOWN;
    return 1;
  }

  if(!memcmp(buf, ttfsig, sizeof(ttfsig)) ||
     !memcmp(buf, otfsig, sizeof(otfsig))) {
    /* TTF or OTF */
    md->md_contenttype = CONTENT_FONT;
    return 1;
  }

  if(l > 16 && mystrbegins((const char *)buf, "#EXTM3U")) {



    if(strstr((const char *)buf, "#EXT-X-STREAM-INF:") ||
       strstr((const char *)buf, "#EXT-X-TARGETDURATION:") ||
       strstr((const char *)buf, "#EXT-X-MEDIA-SEQUENCE:")) {
      // Top level HLS playlist
      md->md_contenttype = CONTENT_VIDEO;
      return 1;
    }

    metdata_set_redirect(md, "playlist:%s", url);
    md->md_contenttype = CONTENT_PLAYLIST;
    return 1;
  }
  return 0;
}

/**
 * Check if file is an iso image
 * pb is guaranteed to point at 128 byts
 * of data starting 0x8000 of start of file
 */
static int
fa_probe_iso0(metadata_t *md, uint8_t *pb)
{
  uint8_t *p;

  if(memcmp(pb, isosig, 8))
    return -1;

  p = &pb[40];
  while(*p > 32 && p != &pb[72])
    p++;

  *p = 0;

  if(md != NULL) {
    md->md_title = rstr_alloc((const char *)pb + 40);
    md->md_contenttype = CONTENT_DVD;
  }
  return 0;
}



/**
 * Check if file is an iso image
 * pb is guaranteed to point at 64k of data
 */
int
fa_probe_iso(metadata_t *md, fa_handle_t *fh)
{
  uint8_t pb[128];

  if(fa_seek_lazy(fh, 0x8000, SEEK_SET) != 0x8000)
    return -1;

  if(fa_read(fh, pb, sizeof(pb)) != sizeof(pb))
    return -1;
  return fa_probe_iso0(md, pb);
}

/**
 *
 */
static void
fa_lavf_load_meta(metadata_t *md, AVFormatContext *fctx,
		  const char *filename)
{
  int i;
  char tmp1[1024];
  int has_video = 0;
  int has_audio = 0;

  md->md_artist = libav_metadata_rstr(fctx->metadata, "artist") ?:
    libav_metadata_rstr(fctx->metadata, "author");

  md->md_album = libav_metadata_rstr(fctx->metadata, "album");

  md->md_format = rstr_alloc(fctx->iformat->long_name);

  if(fctx->duration != AV_NOPTS_VALUE)
    md->md_duration = (float)fctx->duration / 1000000;

  for(i = 0; i < fctx->nb_streams; i++) {
    AVStream *stream = fctx->streams[i];
    AVCodecContext *avctx = stream->codec;

    if(avctx->codec_type == AVMEDIA_TYPE_AUDIO)
      has_audio = 1;

    if(avctx->codec_type == AVMEDIA_TYPE_VIDEO &&
       !(stream->disposition & AV_DISPOSITION_ATTACHED_PIC))
      has_video = 1;
  }

  if(has_audio && !has_video) {
    md->md_contenttype = CONTENT_AUDIO;

    md->md_title = libav_metadata_rstr(fctx->metadata, "title");
    md->md_track = libav_metadata_int(fctx->metadata, "track",
                                      filename ? atoi(filename) : 0);

    return;
  }

  has_audio = 0;
  has_video = 0;

  if(1) {

    int atrack = 0;
    int strack = 0;
    int vtrack = 0;

    /* Check each stream */

    for(i = 0; i < fctx->nb_streams; i++) {
      AVStream *stream = fctx->streams[i];
      AVCodecContext *avctx = stream->codec;
      AVCodec *codec = avcodec_find_decoder(avctx->codec_id);
      AVDictionaryEntry *lang, *title;
      int tn;
      char str[256];

      avcodec_string(str, sizeof(str), avctx, 0);
      TRACE(TRACE_DEBUG, "Probe", " Stream #%d: %s", i, str);

      switch(avctx->codec_type) {
      case AVMEDIA_TYPE_VIDEO:
	has_video = !!codec;
	tn = ++vtrack;
	break;
      case AVMEDIA_TYPE_AUDIO:
	has_audio = !!codec;
	tn = ++atrack;
	break;
      case AVMEDIA_TYPE_SUBTITLE:
	tn = ++strack;
	break;

      default:
	continue;
      }

      if(codec == NULL) {
	snprintf(tmp1, sizeof(tmp1), "%s", codecname(avctx->codec_id));
      } else {
	metadata_from_libav(tmp1, sizeof(tmp1), codec, avctx);
      }

      lang = av_dict_get(stream->metadata, "language", NULL,
                         AV_DICT_IGNORE_SUFFIX);

      title = av_dict_get(stream->metadata, "title", NULL,
                          AV_DICT_IGNORE_SUFFIX);

      metadata_add_stream(md, codecname(avctx->codec_id),
			  avctx->codec_type, i,
			  title ? title->value : NULL,
			  tmp1,
			  lang ? lang->value : NULL,
			  stream->disposition,
			  tn, avctx->channels);
    }

    md->md_contenttype = CONTENT_FILE;
    if(has_video) {
      md->md_contenttype = CONTENT_VIDEO;
    } else if(has_audio) {
      md->md_contenttype = CONTENT_AUDIO;
    }
  }
}


/**
 *
 */
metadata_t *
fa_probe_metadata(const char *url, char *errbuf, size_t errsize,
		  const char *filename, prop_t *stats)
{
  const char *postfix = strrchr(url, '.');
  if(postfix != NULL) {
    // Some files can just be figured out by the file ending
    if(!strcmp(postfix, ".m3u")) {
      metadata_t *md = metadata_create();
      metdata_set_redirect(md, "playlist:%s", url);
      md->md_contenttype = CONTENT_PLAYLIST;
      return md;
    }
  }


  AVFormatContext *fctx;
  int park = 1;
  fa_open_extra_t foe = {
    .foe_stats = stats
  };

  fa_handle_t *fh = fa_open_ex(url, errbuf, errsize,
                               FA_BUFFERED_SMALL, &foe);

  if(fh == NULL)
    return NULL;

  metadata_t *md = metadata_create();

  uint8_t buf[4097];
  int l = fa_read(fh, buf, sizeof(buf) - 1);
  if(l > 0) {
    buf[l] = 0;

#if ENABLE_PLUGINS
    plugin_probe_for_autoinstall(fh, buf, l, url);
#endif

#if ENABLE_VMIR
    if(np_fa_probe(fh, buf, l, md, url) == 0) {
      fa_close_with_park(fh, park);
      return md;
    }
#endif
    if(fa_probe_header(md, url, fh, filename, buf, l)) {
      fa_close_with_park(fh, park);
      return md;
    }
  }
  fa_seek(fh, 0, SEEK_SET);

  if(!fa_probe_iso(md, fh)) {
    fa_close_with_park(fh, park);
    return md;
  }

  int strategy = fa_libav_get_strategy_for_file(fh);

  AVIOContext *avio = fa_libav_reopen(fh, 0);

  if((fctx = fa_libav_open_format(avio, url, errbuf, errsize,
                                  NULL, strategy)) == NULL) {
    fa_libav_close(avio);
    metadata_destroy(md);
    return NULL;
  }

  fa_lavf_load_meta(md, fctx, filename);
  fa_libav_close_format(fctx, park);
  return md;
}


/**
 *
 */
metadata_t *
fa_metadata_from_fctx(AVFormatContext *fctx)
{
  metadata_t *md = metadata_create();

  fa_lavf_load_meta(md, fctx, NULL);
  return md;
}


/**
 * Probe a directory
 */
metadata_t *
fa_probe_dir(const char *url)
{
  metadata_t *md = metadata_create();
  char path[URL_MAX];
  struct fa_stat fs;

  md->md_contenttype = CONTENT_DIR;

  fa_pathjoin(path, sizeof(path), url, "VIDEO_TS");
  if(fa_stat(path, &fs, NULL, 0) == 0 && fs.fs_type == CONTENT_DIR) {
    md->md_contenttype = CONTENT_DVD;
    return md;
  }

  fa_pathjoin(path, sizeof(path), url, "video_ts");
  if(fa_stat(path, &fs, NULL, 0) == 0 && fs.fs_type == CONTENT_DIR) {
    md->md_contenttype = CONTENT_DVD;
    return md;
  }

  return md;
}
