/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>

#include "text.h"

static FcConfig *fc_config;
static int fc_initialized;


/**
 *
 */
static void
fontconfig_init(void)
{
  FcConfig *conf = FcInitLoadConfig();
  FcConfigBuildFonts(conf);
  fc_config = conf;
  fc_initialized = 1;
}


/**
 *
 */
int
fontconfig_resolve(int uc, uint8_t style, const char *family,
		   char *urlbuf, size_t urllen)
{
  int rval = 1;
  FcPattern *pat = NULL;
  FcFontSet *sorted = NULL;
  FcResult result;
  FcCharSet *r_charset;

  if(!fc_initialized)
    fontconfig_init();

  if(fc_config == NULL)
    return 1;

  if(family == NULL)
    family = "Arial";

  pat = FcPatternCreate();

  FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *) family);
  FcPatternAddBool(pat, FC_OUTLINE, FcTrue);
  FcPatternAddInteger(pat, FC_WEIGHT, style & TR_STYLE_BOLD ? 200 : 80);
  FcPatternAddInteger(pat, FC_SLANT, style & TR_STYLE_ITALIC ? 110 : 0);

  FcDefaultSubstitute(pat);

  if(!FcConfigSubstitute(fc_config, pat, FcMatchPattern))
    goto done;

  sorted = FcFontSort(fc_config, pat, FcTrue, NULL, &result);

  int i;
  FcPattern *fp = NULL;
  for(i = 0; i < sorted->nfont; i++) {
    fp = sorted->fonts[i];

    if(FcPatternGetCharSet(fp, FC_CHARSET, 0, &r_charset) != FcResultMatch)
      continue;
    if(FcCharSetHasChar(r_charset, uc))
      break;
  }

  if(i == sorted->nfont)
    goto done;

  FcChar8 *fname;
  result = FcPatternGetString(fp, FC_FILE, 0, &fname);
  if(result == FcResultMatch) {
    snprintf(urlbuf, urllen, "file://%s", fname);
    rval = 0;
#if 0
    *actualstylep = 0;
    int ival;
    if(FcPatternGetInteger(fp, FC_SLANT, 0, &ival) == FcResultMatch)
      if(ival > 50)
	*actualstylep |= TR_STYLE_ITALIC;
    
    if(FcPatternGetInteger(fp, FC_WEIGHT, 0, &ival) == FcResultMatch) 
      if(ival > 150)
	*actualstylep |= TR_STYLE_BOLD;
#endif
  }

 done:
  if(pat != NULL)
    FcPatternDestroy(pat);
  if(sorted != NULL)
    FcFontSetDestroy(sorted);
  return rval;
}
