/*
 *  Functions for probing file contents
 *  Copyright (C) 2008 Andreas Öman
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

#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <libavformat/avformat.h>
#include <libavutil/avstring.h>

#ifdef HAVE_LIBEXIF
#include <libexif/exif-data.h>
#include <libexif/exif-utils.h>
#include <libexif/exif-loader.h>
#endif

#include "showtime.h"
#include "fa_probe.h"
#include "fa_tags.h"


/**
 *
 */
static void
lavf_build_string_and_trim(struct filetag_list *list, ftag_t tag, 
			   const char *str)
{
  int len = strlen(str);
  char *ret;

  if(len == 0)
    return;

  ret = alloca(len + 1);

  memcpy(ret, str, len);
  ret[len] = 0;

  while(len > 0) {
    len--;
    if(ret[len] <= ' ' || ret[len] == '-')
      ret[len] = 0;
    else
      break;
  }
  if(*ret == 0)
    return;

  filetag_set_str(list, tag, ret);
}



/*
 *
 */

static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

int
fa_probe(struct filetag_list *list, const char *filename)
{
  int i, fd;
  AVFormatContext *fctx;
  AVCodecContext *avctx;
  AVCodec *codec;
  const char *t;
  char probebuf[128];
  char tmp1[300];
  char *p;
  struct stat st;
  int has_video = 0;
  int has_audio = 0;
  const char *codectype;

#ifdef HAVE_LIBEXIF
  ExifLoader *l;
  ExifData *ed;
#endif

  fd = open(filename, O_RDONLY);
  if(fd == -1)
    return 1;

  if(fstat(fd, &st) == 0) {
    filetag_set_int(list, FTAG_FILESIZE, st.st_size);
  }

  i = read(fd, probebuf, sizeof(probebuf));

  if(i == sizeof(probebuf)) {
    if(!strncasecmp(probebuf, "[playlist]", 10)) {

      probebuf[sizeof(probebuf) - 1] = 0;

      filetag_set_int(list, FTAG_FILETYPE, FILETYPE_PLAYLIST_PLS);

      t = strrchr(filename, '/');
      t = t ? t + 1 : filename;

      i = 0;
      while(*t && *t != '.')
	tmp1[i++] = *t++;
      tmp1[i] = 0;

      filetag_set_str(list, FTAG_TITLE, tmp1);

      t = strstr(probebuf, "NumberOfEntries=");

      if(t != NULL)
	filetag_set_int(list, FTAG_NTRACKS, atoi(t + 16));

      close(fd);
      return 0;
    }

#ifdef HAVE_LIBEXIF

    l = exif_loader_new();
    exif_loader_write_file(l, filename);

    ed = exif_loader_get_data(l);
    exif_loader_unref (l);
    if(ed != NULL) {
      ExifEntry *e;
      
      e = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF],
				 EXIF_TAG_DATE_TIME_ORIGINAL);
      if(e != NULL) {
	char tid[100];
	struct tm tm;
	time_t t;

	exif_entry_get_value(e, tid, sizeof(tid));

	if(sscanf(tid, "%04d:%02d:%02d %02d:%02d:%02d",
		  &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
		  &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
	  tm.tm_year -= 1900;
	  tm.tm_mon--;
	  t = mktime(&tm);
	  if(t != (time_t)-1) {
	    filetag_set_int(list, FTAG_ORIGINAL_DATE, t);
	  }
	}
      }
      exif_data_unref(ed);
    }
#endif

    if(
       (probebuf[6] == 'J' && probebuf[7] == 'F' &&
	probebuf[8] == 'I' && probebuf[9] == 'F') ||
       (probebuf[6] == 'E' && probebuf[7] == 'x' &&
	probebuf[8] == 'i' && probebuf[9] == 'f') ||
       !memcmp(probebuf, pngsig, 8)) {

      filetag_set_int(list, FTAG_FILETYPE, FILETYPE_IMAGE);
      close(fd);
      return 0;
    }
  }
#if 0
  if(fast) {
    close(fd);
    return 1;
  }
#endif

  if(lseek(fd, 0x8000, SEEK_SET) == 0x8000) {
    i = read(fd, probebuf, sizeof(probebuf));
    if(i == sizeof(probebuf)) {
      if(probebuf[0] == 0x01 &&
	 probebuf[1] == 0x43 &&
	 probebuf[2] == 0x44 &&
	 probebuf[3] == 0x30 &&
	 probebuf[4] == 0x30 &&
	 probebuf[5] == 0x31 &&
	 probebuf[6] == 0x01 &&
	 probebuf[7] == 0x00) {

	p = &probebuf[40];
	while(*p > 32 && p != &probebuf[72]) {
	  p++;
	}
	*p = 0;

	filetag_set_int(list, FTAG_FILETYPE, FILETYPE_ISO);
	filetag_set_str(list, FTAG_TITLE,    &probebuf[40]);
	close(fd);
	return 0;
      }
    }
  }
  


  close(fd);

  /* Okay, see if lavc can find out anything about the file */

  fflock();
  
  if(av_open_input_file(&fctx, filename, NULL, 0, NULL) != 0) {
    ffunlock();
    return 1;
  }

  
  dump_format(fctx, 0, filename, 0);

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    ffunlock();
    return 1;
  }

  /* Format meta info */

  if(fctx->title[0] == 0) {
    t = strrchr(filename, '/');
    t = t ? t + 1 : filename;
    i = strlen(t);
    p = alloca(i + 1);
    memcpy(p, t, i + 1);
    
    if(i > 4 && p[i - 4] == '.')
      p[i - 4] = 0;
    filetag_set_str(list, FTAG_TITLE, p);
  } else {
    lavf_build_string_and_trim(list, FTAG_TITLE, fctx->title);
  }

  lavf_build_string_and_trim(list, FTAG_AUTHOR, fctx->author);
  lavf_build_string_and_trim(list, FTAG_ALBUM, fctx->album);

  if(fctx->track != 0)
    filetag_set_int(list, FTAG_TRACK, fctx->track);

  filetag_set_str(list, FTAG_MEDIAFORMAT, fctx->iformat->long_name);

  /* Check each stream */

  for(i = 0; i < fctx->nb_streams; i++) {
    avctx = fctx->streams[i]->codec;
    codec = avcodec_find_decoder(avctx->codec_id);

    switch(avctx->codec_type) {
    case CODEC_TYPE_VIDEO:
      codectype = "Video";
      has_video = !!codec;
      break;
    case CODEC_TYPE_AUDIO:
      codectype = "Audio";
      has_audio = !!codec;
      break;
      
    default:
      continue;
    }

    if(codec == NULL) {
      snprintf(tmp1, sizeof(tmp1), "%s: Unsupported codec", codectype);
    } else {
      snprintf(tmp1, sizeof(tmp1), "%s: %s",  codectype, codec->name);

      if(avctx->codec_type == CODEC_TYPE_AUDIO) {
	snprintf(tmp1 + strlen(tmp1), sizeof(tmp1) - strlen(tmp1),
		 ", %d Hz, %d chanels", avctx->sample_rate, avctx->channels);
      }

      if(avctx->width)
	snprintf(tmp1 + strlen(tmp1), sizeof(tmp1) - strlen(tmp1),
		 ", %dx%d",
		 avctx->width, avctx->height);
      
      if(avctx->bit_rate)
	snprintf(tmp1 + strlen(tmp1), sizeof(tmp1) - strlen(tmp1),
		 ", %d kb/s", avctx->bit_rate / 1000);
    }

    switch(avctx->codec_type) {
    case CODEC_TYPE_VIDEO:
      p = strncmp(tmp1, "Video: ", 7) ? tmp1 : tmp1 + 7;
      filetag_set_str(list, FTAG_VIDEOINFO, p);
      break;
    case CODEC_TYPE_AUDIO:
      p = strncmp(tmp1, "Audio: ", 7) ? tmp1 : tmp1 + 7;
      filetag_set_str(list, FTAG_AUDIOINFO, p);
      break;
      
    default:
      continue;
    }
  }

  if(has_video)
    filetag_set_int(list, FTAG_FILETYPE, FILETYPE_VIDEO);
  else if(has_audio)
    filetag_set_int(list, FTAG_FILETYPE, FILETYPE_AUDIO);

  if(fctx->duration != AV_NOPTS_VALUE)
    filetag_set_int(list, FTAG_DURATION, fctx->duration / AV_TIME_BASE);

  av_close_input_file(fctx);  
  ffunlock();

#if 0
  /* Set icon, if not supplied, we try to make a guess */

  if(icon != NULL) {
    mi->mi_icon = strdup(icon);
  } else {
    av_strlcpy(tmp1, filename, sizeof(tmp1));
    p = strrchr(tmp1, '/');
    if(p != NULL) {

      static const char *foldericons[] = {
	"Folder.jpg",
	"folder.jpg",
	"AlbumArtSmall.jpg",
	NULL};

      p++;
      i = 0;
      while(foldericons[i]) {
	av_strlcpy(p, foldericons[i], sizeof(tmp1) - (p - tmp1) - 1);
	if(stat(tmp1, &st) == 0) {
	  break;
	}
	i++;
      }
      if(foldericons[i]) {
	mi->mi_icon = strdup(filename);
	printf("Icon %s\n", mi->mi_icon);
      }
    }
  }
#endif

  return 0;
}
