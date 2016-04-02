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
#include <stdlib.h>

#include "main.h"
#include "misc/regex.h"
#include "misc/str.h"

#include "metadata_str.h"

#define isnum(a) ((a) >= '0' && (a) <= '9')

/**
 *
 */
static const char *stopstrings[] = {
  "1080",
  "1080P",
  "3D",
  "720",
  "720P",
  "AC3",
  "AE",
  "AHDTV",
  "ANALOG",
  "AUDIO",
  "BDRIP",
  "CAM",
  "CD",
  "CD1",
  "CD2",
  "CD3",
  "CHRONO",
  "COLORIZED",
  "COMPLETE",
  "CONVERT",
  "CUSTOM",
  "DC",
  "DDC",
  "DIRFIX",
  "DISC",
  "DISC1",
  "DISC2",
  "DISC3",
  "DIVX",
  "DOLBY",
  "DSR",
  "DTS",
  "DTV",
  "DUAL",
  "DUBBED",
  "DVBRIP",
  "DVDRIP",
  "DVDSCR",
  "DVDSCREENER",
  "EXTENDED",
  "FINAL",
  "FS",
  "HARDCODED",
  "HARDSUB",
  "HARDSUBBED",
  "HD",
  "HDDVDRIP",
  "HDRIP",
  "HDTV",
  "HR",
  "INT",
  "INTERNAL",
  "LASERDISC",
  "LIMITED",
  "LINE",
  "LIVE.AUDIO",
  "MP3",
  "MULTI",
  "NATIVE",
  "NFOFIX",
  "NTSC",
  "OAR",
  "P2P",
  "PAL",
  "PDTV",
  "PPV",
  "PREAIR",
  "PROOFFIX",
  "PROPER",
  "PT",
  "R1",
  "R2",
  "R3",
  "R4",
  "R5",
  "R6",
  "RATED",
  "RC",
  "READ.NFO",
  "READNFO",
  "REMASTERED",
  "REPACK",
  "RERIP",
  "RETAIL",
  "SAMPLEFIX",
  "SATRIP",
  "SCR",
  "SCREENER",
  "SE",
  "STV",
  "SUBBED",
  "SUBFORCED",
  "SUBS",
  "SVCD",
  "SYNCFIX",
  "TC",
  "TELECINE",
  "TELESYNC",
  "THEATRICAL",
  "TS",
  "TVRIP",
  "UNCUT",
  "UNRATED",
  "UNSUBBED",
  "VCDRIP",
  "VHSRIP",
  "WATERMARKED",
  "WORKPRINT",
  "WP",
  "WS",
  "X264",
  "XVID",
  NULL,
};

/**
 *
 */
void
metadata_filename_to_title(const char *filename, int *yearp, rstr_t **titlep)
{
  int year = 0;

  char *s = mystrdupa(filename);

  url_deescape(s);

  int i = strlen(s);

  while(i > 0) {

    if(i > 5 && s[i-5] == '.' &&
       isnum(s[i-4]) && isnum(s[i-3]) && isnum(s[i-2]) && isnum(s[i-1])) {
      year = atoi(s + i - 4);
      i -= 5;
      s[i] = 0;
      continue;
    }

    if(i > 7 && s[i-7] == ' ' && s[i-6] == '(' &&
       isnum(s[i-5]) && isnum(s[i-4]) && isnum(s[i-3]) && isnum(s[i-2]) &&
       s[i-1] == ')') {
      year = atoi(s + i - 5);
      i -= 7;
      s[i] = 0;
      continue;
    }

    int j;
    for(j = 0; stopstrings[j] != NULL; j++) {
      int len = strlen(stopstrings[j]);
      if(i > len+1 && (s[i-len-1] == '.' || s[i-len-1] == ' ') &&
	 !strncasecmp(s+i-len, stopstrings[j], len) &&
	 (s[i] == '.' || s[i] == ' ' || s[i] == '-' || s[i] == 0)) {
	i -= len+1;
	s[i] = 0;
	break;
      }
    }

    if(stopstrings[j] != NULL)
      continue;

    i--;
  }

  char *lastword = strrchr(s, ' ');
  if(lastword && lastword > s) {
    int y = atoi(lastword + 1);
    if(y > 1900 && y < 2040) {
      year = y;
      *lastword = 0;
    }
  }




  for(i = 0; s[i]; i++) {
    if(s[i] == '.') {
      s[i] = ' ';
    }
  }

  if(yearp != NULL)
    *yearp = year;

  if(titlep != NULL)
    *titlep = rstr_alloc(s);
}


/**
 *
 */
int
metadata_filename_to_episode(const char *s,
			     int *seasonp, int *episodep,
			     rstr_t **titlep)
{
  int i, j;
  int len = strlen(s);
  int season = -1;
  int episode = -1;
  for(i = 0; i < len; i++) {
    if((s[i] == 's' || s[i] == 'S') && isnum(s[i+1]) && isnum(s[i+2])) {
      int o = 3+i;
      if(s[o] == '.')
	o++;

      if((s[o] == 'e' || s[o] == 'E') && isnum(s[o+1]) && isnum(s[o+2])) {
	season = atoi(s+i+1);
	episode = atoi(s+o+1);
	break;
      }
    }
  }


  if(season == -1 || episode == -1)
    return -1;

  *seasonp = season;
  *episodep = episode;

  char *t = mystrdupa(s);
  url_deescape(t);

  for(j= 0; j < i; j++) {
    if(t[j] == '.') {
      t[j] = ' ';
    }
  }
  t[j] = 0;

  if(titlep != NULL) {
    if(j)
      *titlep = rstr_alloc(t);
    else
      *titlep = NULL;
  }
  return 0;
}


/**
 *
 */
const char *folder_to_season[] = {
  "(.*)[ .]Season[ .]([0-9]+)",
  NULL
};


/**
 *
 */
int
metadata_folder_to_season(const char *s,
			  int *seasonp, rstr_t **titlep)
{
  int i;
  for(i = 0; folder_to_season[i] != NULL; i++) {
    hts_regex_t re;
    if(!hts_regcomp(&re, folder_to_season[i])) {
      hts_regmatch_t matches[8];
      if(!hts_regexec(&re, s, 8, matches, 0)) {
	hts_regfree(&re);
	if(seasonp != NULL)
	  *seasonp = atoi(s + matches[2].rm_so);
	if(titlep != NULL) {
	  int l = matches[1].rm_eo - matches[1].rm_so;
	  if(l > 0)
	    *titlep = rstr_allocl(s + matches[i].rm_so, l);
	  else
	    *titlep = NULL;
	}
	return 0;
      }
      hts_regfree(&re);
    }
  }
  return -1;
}


/**
 *
 */
int
is_reasonable_movie_name(const char *s)
{
  int n = 0;
  for(;*s; s++) {
    if(*s >= 0x30)
      n++;
  }
  return n >= 3;
}




/**
 *
 */
rstr_t *
metadata_remove_postfix_rstr(rstr_t *name)
{
  if(!gconf.show_filename_extensions) {
    const char *str = rstr_get(name);
    int len = strlen(str);
    if(len > 4 && str[len - 4] == '.')
      return rstr_allocl(str, len - 4);

    if(len > 5 && !strcasecmp(str + len - 5, ".m2ts"))
      return rstr_allocl(str, len - 5);
  }
  return rstr_dup(name);
}


/**
 *
 */
rstr_t *
metadata_remove_postfix(const char *str)
{
  int len = strlen(str);
  if(len > 4 && str[len - 4] == '.') {
    return rstr_allocl(str, len - 4);
  }
  return rstr_allocl(str, len);
}

