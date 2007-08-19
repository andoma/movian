/*
 *  Subtitling
 *  Copyright (C) 2007 Andreas Öman
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

#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "showtime.h"
#include "media.h"
#include "subtitles.h"

/*
 *
 */
subtitle_format_t
subtitle_probe_file(const char *filename)
{
  char buf[50];
  subtitle_format_t t = SUBTITLE_FORMAT_UNKNOWN;
  int fd, flen = strlen(filename);

  fd = open(filename, O_RDONLY);
  if(fd == -1)
    return SUBTITLE_FORMAT_UNKNOWN;

  if(read(fd, buf, sizeof(buf)) != sizeof(buf)) {
    close(fd);
    return SUBTITLE_FORMAT_UNKNOWN;
  }

  buf[sizeof(buf) - 1] = 0;
  
  if(flen > 5 && !strcasecmp(filename + flen - 4, ".srt") &&
     isdigit(buf[0]) && strstr(buf, "-->"))
    t = SUBTITLE_FORMAT_SRT;
  else if(flen > 5 && !strcasecmp(filename + flen - 4, ".sub") &&
	  !strncmp(buf, "{1}{1}", 6))
    t = SUBTITLE_FORMAT_SUB;

  close(fd);
  return t;
}

/*
 *
 */
void
subtitles_free(subtitles_t *sub)
{
  subtitle_entry_t *se;

  while((se = TAILQ_FIRST(&sub->sub_entries)) != NULL) {
    TAILQ_REMOVE(&sub->sub_entries, se, se_link);
    free(se);
  }

  free(sub->sub_vec);
  free(sub);
}

/*
 *
 */
subtitles_t *
subtitles_load_finale(subtitles_t *sub, int nentries)
{
  subtitle_entry_t *se;
  int c;

  sub->sub_nentries = nentries;
  sub->sub_hint = 0;

  sub->sub_vec = malloc(sizeof(void *) * nentries);
  c = 0;
  TAILQ_FOREACH(se, &sub->sub_entries, se_link)
    sub->sub_vec[c++] = se;
  return sub;
}



/*
 *
 */
subtitles_t *
subtitles_load_srt(const char *filename)
{
  FILE *fp;
  subtitles_t *sub;
  subtitle_entry_t *se;
  int p, c;
  int ah, am, as, au, bh, bm, bs, bu;
  int64_t start, stop;
  char txt[400];
  int nentries = 0;

  fp = fopen(filename, "r");
  if(fp == NULL)
    return NULL;

  sub = malloc(sizeof(subtitles_t));
  TAILQ_INIT(&sub->sub_entries);

  while(!feof(fp)) {
    if(fscanf(fp, "%d\n", &c) != 1)
      break;

    if(fscanf(fp, "%02d:%02d:%2d,%03d --> %02d:%02d:%2d,%03d\n",
	      &ah, &am, &as, &au, &bh, &bm, &bs, &bu) != 8)
      break;

    start = 
      (int64_t) ah * 3600000000ULL +
      (int64_t) am *   60000000ULL +
      (int64_t) as *    1000000ULL +
      (int64_t) au *       1000ULL;

    stop = 
      (int64_t) bh * 3600000000ULL +
      (int64_t) bm *   60000000ULL +
      (int64_t) bs *    1000000ULL +
      (int64_t) bu *       1000ULL;

    p = 0;
    txt[0] = 0;

    while(1) {
      if(fgets(txt + p, sizeof(txt) - p - 1, fp) == NULL)
	break;
      
      if(txt[p] < 32)
	break;

      p += strlen(txt + p);
    }
    txt[p] = 0;
    
    se = malloc(sizeof(subtitle_entry_t));
    se->se_start = start;
    se->se_stop = stop;
    se->se_text = strdup(txt);
    TAILQ_INSERT_TAIL(&sub->sub_entries, se, se_link);
    nentries++;
  }

  fclose(fp);
  return subtitles_load_finale(sub, nentries);
}


/*
 *
 */
subtitles_t *
subtitles_load_sub(const char *filename)
{
  FILE *fp;
  subtitles_t *sub;
  subtitle_entry_t *se;
  float framerate, m, v;
  int64_t start, stop;
  char buf[1000];
  char *s;
  int i, l, nentries = 0;

  fp = fopen(filename, "r");
  if(fp == NULL)
    return NULL;

  if(fscanf(fp, "{1}{1}%f\n", &framerate) != 1)
    return NULL;

  if(framerate < 0 || framerate > 100)
    return NULL;

  m = 1000000 / framerate;


  sub = malloc(sizeof(subtitles_t));
  TAILQ_INIT(&sub->sub_entries);

  while(!feof(fp)) {
    if(fgets(buf, sizeof(buf), fp) == NULL)
      break;

    s = buf;
    if(s[0] != '{')
      continue;
    s++;

    v = strtol(s, &s, 10);
    start = v * m;

    if(s[0] != '}' || s[1] != '{')
      continue;
    s+= 2;
    v = strtol(s, &s, 10);
    stop = v * m;

    if(s[0] != '}')
      continue;

    s++;

    l = strlen(s);
    for(i = 0; i < l; i++) {
      if(s[i] == '|')
	s[i] = '\n';
      if(s[i] == 10 || s[i] == 13)
	s[i] = 0;
    }
    
    se = malloc(sizeof(subtitle_entry_t));
    se->se_start = start;
    se->se_stop = stop;
    se->se_text = strdup(s);
    TAILQ_INSERT_TAIL(&sub->sub_entries, se, se_link);
    nentries++;
  }
  fclose(fp);
  return subtitles_load_finale(sub, nentries);
}




/*
 *
 */
subtitles_t *
subtitles_load(const char *filename)
{
  switch(subtitle_probe_file(filename)) {
  case SUBTITLE_FORMAT_UNKNOWN:
  default:
    return NULL;
  case SUBTITLE_FORMAT_SRT:
    return subtitles_load_srt(filename);
  case SUBTITLE_FORMAT_SUB:
    return subtitles_load_sub(filename);
  }
}








/*
 *
 */
int
subtitles_index_by_pts(subtitles_t *sub, int64_t pts)
{
  int n, p, h = sub->sub_hint;
  subtitle_entry_t *se, *prev, *next;

  while(1) {
    se = sub->sub_vec[h];

    n = h + 1;
    p = h - 1;

    prev = p >= 0                ? sub->sub_vec[p] : NULL;
    next = n < sub->sub_nentries ? sub->sub_vec[n] : NULL;

    if(pts < se->se_start) {
      if(prev == NULL || pts > prev->se_stop)
	return -1;
      h = p;
      continue;
    }
    
    if(pts > se->se_stop) {
      if(next == NULL || pts < next->se_start)
	return -1;
      h = n;
      continue;
    }
    sub->sub_hint = h;
    return h;
  }
}

/*
 *
 */
glw_t *
subtitles_make_widget(subtitles_t *sub, int index)
{
  subtitle_entry_t *se;
  glw_t *y, *x;


  if(index < 0 || index >= sub->sub_nentries)
    return NULL;
  
  se = sub->sub_vec[index];

  y = glw_create(GLW_CONTAINER_Y,
		 NULL);
  
  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 4.0f,
	     NULL);

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, y,
		 NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, 0.1f,
	     NULL);

  glw_text_multiline(x, GLW_TEXT_BITMAP, se->se_text,
		     300, 3, 0, 27.0);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, 0.1f,
	     NULL);
  return y;
}
