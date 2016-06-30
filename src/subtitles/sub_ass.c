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

#include "main.h"
#include "video_overlay.h"
#include "text/text.h"
#include "image/pixmap.h"
#include "misc/str.h"
#include "sub.h"
#include "video/video_settings.h"
#include "ext_subtitles.h"
#include "subtitles.h"
#include "misc/charset_detector.h"
#include "misc/minmax.h"

// #define ASS_DEBUG

/**
 *
 */
static int64_t
ass_get_ts(const char *buf)
{
  if(strlen(buf) != 10)
    return PTS_UNSET;

  return 1000LL * (
    (buf[ 0] - '0') *  3600000LL +
    (buf[ 2] - '0') *   600000LL +
    (buf[ 3] - '0') *    60000LL + 
    (buf[ 5] - '0') *    10000LL + 
    (buf[ 6] - '0') *     1000LL +
    (buf[ 8] - '0') *      100LL +
    (buf[ 9] - '0') *       10LL);

}



/**
 *
 */
#define TEXT_APPEND_STEP 16

static uint32_t *
text_append(uint32_t *ptr, int *lenp, uint32_t uc)
{
  int len = *lenp;

  if((len & (TEXT_APPEND_STEP-1)) == 0)
    ptr = realloc(ptr, (len + TEXT_APPEND_STEP)  * sizeof(uint32_t));

  ptr[len] = uc;
  *lenp = len + 1;
  return ptr;
}


/**
 *
 */
typedef struct ass_style {
  LIST_ENTRY(ass_style) as_link;

  char *as_name;
  char *as_fontname;
  uint32_t as_primary_color;
  uint32_t as_secondary_color;
  uint32_t as_outline_color;
  uint32_t as_back_color;

  int as_fontsize;
  int as_bold;
  int as_italic;
  int as_underline;
  int as_strikeout;
  int as_scalex;
  int as_scaley;
  int as_spacing;
  int as_angle;
  unsigned int as_borderstyle;
  unsigned int as_outline;
  unsigned int as_shadow;
  unsigned int as_alignment;

  int as_margin_left;
  int as_margin_right;
  int as_margin_vertical;

  int as_encoding;

} ass_style_t;

static const ass_style_t ass_style_default = {
  .as_primary_color = 0xffffff,
  .as_outline_color = 0x000000,
  .as_shadow = 1,
  .as_outline = 1,
  .as_bold = 1,
  .as_alignment = 1,
  .as_margin_left = 20,
  .as_margin_right = 20,
  .as_margin_vertical = 20,
  .as_fontsize = 48,
};

typedef struct ass_decoder_ctx {
  enum {
    ADC_SECTION_NONE,
    ADC_SECTION_SCRIPT_INFO,
    ADC_SECTION_V4_STYLES,
    ADC_SECTION_EVENTS,
  } adc_section;

  LIST_HEAD(, ass_style) adc_styles;
  const char *adc_style_format;
  char *adc_event_format;
  
  int adc_resx;
  int adc_resy;

  void *adc_opaque;
  void (*adc_dialogue_handler)(struct ass_decoder_ctx *adc, const char *s);

  int adc_shadow;
  int adc_outline;

} ass_decoder_ctx_t;


/**
 *
 */
static void
adc_init(ass_decoder_ctx_t *adc)
{
  memset(adc, 0, sizeof(ass_decoder_ctx_t));
  adc->adc_shadow = TR_CODE_SHADOW_US;
  adc->adc_outline = TR_CODE_OUTLINE_US;
}




/**
 *
 */
static void
adc_cleanup(ass_decoder_ctx_t *adc)
{
  ass_style_t *as;
  while((as = LIST_FIRST(&adc->adc_styles)) != NULL) {
    LIST_REMOVE(as, as_link);
    free(as->as_name);
    free(as->as_fontname);
    free(as);
  }
  free(adc->adc_event_format);
}



/**
 *
 */
static void
gettoken(char *buf, size_t bufsize, const char **src)
{
  char *start = buf;
  char *end = buf + bufsize;
  const char *s = *src;

  while(*s == 32)
    s++;

  while(*s != 0 && *s != ',' && buf < end - 1)
    *buf++ = *s++;
  if(*s == ',')
    s++;
  *buf = 0;

  while(buf > start && buf[-1] == ' ')
    *--buf = 0;
  *src = s;
}




/**
 *
 */
static uint32_t
ass_parse_color(const char *str)
{
  int l = 0;
  uint32_t rgba = 0;
  if(*str == '&')
    str++;

  if(*str == 'h' || *str == 'H')
    str++;
  
  while(hexnibble(str[l]) != -1)
    l++;

  if(l > 8)
    l = 8;

  switch(l) {
  case 8: rgba |= hexnibble(str[l-8]) << 28;
  case 7: rgba |= hexnibble(str[l-7]) << 24;
  case 6: rgba |= hexnibble(str[l-6]) << 20;
  case 5: rgba |= hexnibble(str[l-5]) << 16;
  case 4: rgba |= hexnibble(str[l-4]) << 12;
  case 3: rgba |= hexnibble(str[l-3]) << 8;
  case 2: rgba |= hexnibble(str[l-2]) << 4;
  case 1: rgba |= hexnibble(str[l-1]);
  }

  return rgba;
}


/**
 *
 */
static void
ass_parse_v4style(ass_decoder_ctx_t *adc, const char *str)
{
  char key[128];
  char val[128];
  const char *fmt = adc->adc_style_format;
  ass_style_t *as;

  if(fmt == NULL)
    return;

  as = calloc(1, sizeof(ass_style_t));
  as->as_primary_color = 0x00ffffff;
  as->as_outline_color = 0x00000000;

  while(*fmt && *str) {
    gettoken(key, sizeof(key), &fmt);
    gettoken(val, sizeof(val), &str);

    if(!strcasecmp(key, "name"))
      mystrset(&as->as_name, val);
    else if(!strcasecmp(key, "alignment")) {
      as->as_alignment = atoi(val);
      if(as->as_alignment < 1 || as->as_alignment > 9)
	as->as_alignment = 1;
    } else if(!strcasecmp(key, "marginl"))
      as->as_margin_left = atoi(val);
    else if(!strcasecmp(key, "marginr"))
      as->as_margin_right = atoi(val);
    else if(!strcasecmp(key, "marginv"))
      as->as_margin_vertical = atoi(val);
    else if(!strcasecmp(key, "bold"))
      as->as_bold = !!atoi(val);
    else if(!strcasecmp(key, "italic"))
      as->as_italic = !!atoi(val);
    else if(!strcasecmp(key, "primarycolour"))
      as->as_primary_color = ass_parse_color(val);
    else if(!strcasecmp(key, "secondarycolour"))
      as->as_secondary_color = ass_parse_color(val);
    else if(!strcasecmp(key, "outlinecolour"))
      as->as_outline_color = ass_parse_color(val);
    else if(!strcasecmp(key, "backcolour"))
      as->as_back_color = ass_parse_color(val);
    else if(!strcasecmp(key, "outline"))
      as->as_outline = MIN((unsigned int)atoi(val), 4);
    else if(!strcasecmp(key, "shadow"))
      as->as_shadow = MIN((unsigned int)atoi(val), 4);
    else if(!strcasecmp(key, "fontsize"))
      as->as_fontsize = atoi(val);
    else if(!strcasecmp(key, "fontname"))
      mystrset(&as->as_fontname, val);
    else if(!strcasecmp(key, "encoding"))
      as->as_encoding = atoi(val);
  }
  LIST_INSERT_HEAD(&adc->adc_styles, as, as_link);
}


/**
 *
 */
static int
ass_decode_line(ass_decoder_ctx_t *adc, const char *str)
{
  const char *s;
  if(!strcmp(str, "[Script Info]")) {
    adc->adc_section = ADC_SECTION_SCRIPT_INFO;
    return 0;
  }

  if(!strcmp(str, "[V4+ Styles]")) {
    adc->adc_section = ADC_SECTION_V4_STYLES;
    return 0;
  }

  if(!strcmp(str, "[Events]")) {
    adc->adc_section = ADC_SECTION_EVENTS;
    return 0;
  }

  if(*str == '[') {
    // Unknown section, better stay out of it
    adc->adc_section = ADC_SECTION_NONE;
    return 0;
  }

  switch(adc->adc_section) {
  default:
    break;
  case ADC_SECTION_SCRIPT_INFO:
    s = mystrbegins(str, "PlayResX:");
    if(s != NULL) {
      adc->adc_resx = atoi(s);
      break;
    }

    s = mystrbegins(str, "PlayResY:");
    if(s != NULL) {
      adc->adc_resy = atoi(s);
      break;
    }

    s = mystrbegins(str, "ScaledBorderAndShadow:");
    if(s != NULL) {
      if(atoi(s) > 0 || !strcasecmp(s, "yes")) {
	adc->adc_shadow = TR_CODE_SHADOW_US;
	adc->adc_shadow = TR_CODE_SHADOW_US;
      }
      break;
    }

    break;

  case ADC_SECTION_V4_STYLES:
    s = mystrbegins(str, "Format:");
    if(s != NULL) {
      adc->adc_style_format = s;
      break;
    }

    s = mystrbegins(str, "Style:");
    if(s != NULL) {
      ass_parse_v4style(adc, s);
      break;
    }
    break;

  case ADC_SECTION_EVENTS:
    s = mystrbegins(str, "Format:");
    if(s != NULL) {
      adc->adc_event_format = strdup(s);
      break;
    }

    s = mystrbegins(str, "Dialogue:");
    if(s != NULL) {
      if(adc->adc_dialogue_handler == NULL) 
	return 1;
      adc->adc_dialogue_handler(adc, s);
      break;
    }
    break;
  }

  return 0;
}


/**
 *
 */
static void
ass_decode_lines(ass_decoder_ctx_t *adc, char *s)
{
  int l;

  for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
    s[l] = 0;
#ifdef ASS_DEBUG
    printf("ass/line: %s\n", s);
#endif
    if(ass_decode_line(adc, s))
      break;
  }
}


/**
 *
 */
static const ass_style_t *
adc_find_style(const ass_decoder_ctx_t *adc, const char *name)
{
  ass_style_t *as;
  if(*name == '*')
    name++;
  LIST_FOREACH(as, &adc->adc_styles, as_link)
    if(!strcasecmp(as->as_name, name))
      return as;
  return &ass_style_default;
}


/**
 * Context to decode a dialogue line
 */
typedef struct ass_dialogue {
  uint32_t *ad_text;
  int ad_textlen;

  int ad_fadein;
  int ad_fadeout;
  
  int16_t ad_x;
  int16_t ad_y;

  int8_t ad_alignment;
  int8_t ad_absolute_pos;
  
  char ad_not_supported;

} ass_dialoge_t;


static void
ad_txt_append(ass_dialoge_t *ad, int v)
{
  ad->ad_text = text_append(ad->ad_text, &ad->ad_textlen, v);
}


static __inline int isd(char c)
{
  return c >= '0' && c <= '9';
}

/**
 *
 */
static void
ass_handle_override(ass_dialoge_t *ad, const char *src, int len,
		    int fontdomain)
{
  char *str, *cmd;
  int v1, v2;
  if(len > 1000)
    return;

  str = alloca(len + 1);
  memcpy(str, src, len);
  str[len] = 0;
  
  while((cmd = strchr(str, '\\')) != NULL) {
  next:
    str = ++cmd;
    if(str[0] == 'i' && isd(str[1])) {
      ad_txt_append(ad, str[1] == '1' ? TR_CODE_ITALIC_ON : TR_CODE_ITALIC_OFF);
    } else if(str[0] == 'b' && isd(str[1])) {
      ad_txt_append(ad, str[1] == '1' ? TR_CODE_BOLD_ON : TR_CODE_BOLD_OFF);
    } else if(sscanf(str, "fad(%d,%d)", &v1, &v2) == 2) {
      ad->ad_fadein = v1 * 1000;
      ad->ad_fadeout = v2 * 1000;
    } else if(sscanf(str, "pos(%d,%d)", &v1, &v2) == 2) {
      ad->ad_x = v1;
      ad->ad_y = v2;
      ad->ad_absolute_pos = 1;
    } else if(!memcmp(str, "fscx", 4)||!memcmp(str, "fscy", 4)) {
      v1 = atoi(str + 4);
      if(v1 > 0)
	ad_txt_append(ad, TR_CODE_SIZE_PX + (v1 & 0xff));
    } else if(str[0] == 'f' && str[1] == 's' && isd(str[2])) {
      v1 = atoi(str + 2);
      if(v1 > 3)
	ad_txt_append(ad, TR_CODE_SIZE_PX + (v1 & 0xff));

    } else if((str[0] == 'c' && str[1] == '&') || (str[0] == '1' && str[1] == 'c')) {
       ad_txt_append(ad, TR_CODE_COLOR | ass_parse_color(str+2));
    } else if((str[0] == '3' && str[1] == 'c')) {
       ad_txt_append(ad, TR_CODE_OUTLINE_COLOR | ass_parse_color(str+2));
    } else if((str[0] == '4' && str[1] == 'c')) {
       ad_txt_append(ad, TR_CODE_SHADOW_COLOR | ass_parse_color(str+2));
    } else if(str[0] == 'f' && str[1] == 'n') {
      str += 2;
      cmd = strchr(str, '\\');
      if(cmd != NULL)
        *cmd = 0;

      ad_txt_append(ad, TR_CODE_FONT_FAMILY |
		    freetype_family_id(str, fontdomain));

      if(cmd == NULL)
	break;

      goto next;

    } else if(str[0] == 'a' && str[1] == 'n') {
      // Alignment
      ad->ad_alignment = atoi(str+2);
    } else if(str[0] == 't' && str[1] == '(') {
        // ignore Animated transform
      str = strchr(str, ')');
    } else if(str[0] == 'p' && isd(str[1])) {
        // ignore Drawing tags
        ad->ad_not_supported=1;
        return;
    } else {
#ifdef ASS_DEBUG
      printf("ass: Can't handle override: %s\n", str);
#endif
    }
  }
}

extern char font_subs[];

/**
 *
 */
static video_overlay_t *
ad_dialogue_decode(const ass_decoder_ctx_t *adc, const char *line,
		   int fontdomain)
{
  char key[128];
  char val[128];
  const char *fmt = adc->adc_event_format;
  const ass_style_t *as = &ass_style_default;
  int layer = 0;
  int64_t start = PTS_UNSET;
  int64_t end = PTS_UNSET;
  const char *str = NULL;
  ass_dialoge_t ad;

  memset(&ad, 0, sizeof(ad));

  if(fmt == NULL)
    return NULL;

#ifdef ASS_DEBUG
  printf("ass/dialogue: %s\n", line);
#endif

  while(*fmt && *line && *line != '\n' && *line != '\r') {
    gettoken(key, sizeof(key), &fmt);
    if(!strcasecmp(key, "text")) {
      char *d = mystrdupa(line);
      d[strcspn(d, "\n\r")] = 0;
      str = d;
      break;
    }

    gettoken(val, sizeof(val), &line);

    if(!strcasecmp(key, "layer"))
      layer = atoi(val);
    else if(!strcasecmp(key, "start"))
      start = ass_get_ts(val);
    else if(!strcasecmp(key, "end"))
      end = ass_get_ts(val);
    else if(!strcasecmp(key, "style"))
      as = adc_find_style(adc, val);
  }

  if(start == PTS_UNSET || end == PTS_UNSET || str == NULL)
    return NULL;

  if(as->as_bold)
    ad_txt_append(&ad, TR_CODE_BOLD_ON);
  if(as->as_italic)
    ad_txt_append(&ad, TR_CODE_ITALIC_ON);

  if(font_subs[0])
    ad_txt_append(&ad, TR_CODE_FONT_FAMILY |
		  freetype_family_id(font_subs, fontdomain));
  
  else if(as->as_fontname)
    ad_txt_append(&ad, TR_CODE_FONT_FAMILY |
		  freetype_family_id(as->as_fontname, fontdomain));

  if(as == &ass_style_default || subtitle_settings.style_override) {

    ad_txt_append(&ad, TR_CODE_COLOR | subtitle_settings.color);
    ad_txt_append(&ad, TR_CODE_OUTLINE_COLOR | subtitle_settings.outline_color);
    ad_txt_append(&ad, TR_CODE_SHADOW_COLOR | subtitle_settings.shadow_color);

    ad_txt_append(&ad, adc->adc_shadow | subtitle_settings.shadow_displacement);
    ad_txt_append(&ad, adc->adc_outline | subtitle_settings.outline_size);

  } else {
    int alpha;
    alpha = 255 - (as->as_primary_color >> 24);

    ad_txt_append(&ad, TR_CODE_SIZE_PX | as->as_fontsize);

    ad_txt_append(&ad, TR_CODE_COLOR | (as->as_primary_color & 0xffffff));
    ad_txt_append(&ad, TR_CODE_ALPHA | alpha);

    alpha = 255 - (as->as_outline_color >> 24);
    ad_txt_append(&ad, TR_CODE_OUTLINE_COLOR|(as->as_outline_color & 0xffffff));
    ad_txt_append(&ad, TR_CODE_OUTLINE_ALPHA | alpha);

    alpha = 255 - (as->as_back_color >> 24);
    ad_txt_append(&ad, TR_CODE_SHADOW_COLOR | (as->as_back_color & 0xffffff));
    ad_txt_append(&ad, TR_CODE_SHADOW_ALPHA | alpha);

    if(as->as_shadow)
      ad_txt_append(&ad, adc->adc_shadow | (as->as_shadow & 0xff));

    if(as->as_outline)
      ad_txt_append(&ad, adc->adc_outline | (as->as_outline & 0xff));
  }

  int c;
  while((c = utf8_get(&str)) != 0) {
    if(c == '\\' && (*str == 'n' || *str == 'N')) {
      str++;
      ad_txt_append(&ad, '\n');
      continue;
    }

    if(c == '\\' && *str == 'h') {
      // hard space
      str++;
      ad_txt_append(&ad, ' ');
      continue;
    }

    if(c == '{') {
      const char *end = strchr(str, '}');
      if(end == NULL)
        break;

      ass_handle_override(&ad, str, end - str, fontdomain);
      if(ad.ad_not_supported)
          return NULL;
      str = end + 1;
      continue;
    }

    ad_txt_append(&ad, c);
  }


  video_overlay_t *vo = calloc(1, sizeof(video_overlay_t));
  vo->vo_type = VO_TEXT;

  vo->vo_text = malloc(ad.ad_textlen * sizeof(uint32_t));
  vo->vo_text_length = ad.ad_textlen;
  memcpy(vo->vo_text, ad.ad_text, ad.ad_textlen * sizeof(uint32_t));

  free(ad.ad_text);

  vo->vo_start = start;
  vo->vo_stop = end;
  vo->vo_fadein = ad.ad_fadein;
  vo->vo_fadeout = ad.ad_fadeout;

  vo->vo_x = ad.ad_x;
  vo->vo_y = ad.ad_y;
  vo->vo_abspos = ad.ad_absolute_pos;

  vo->vo_alignment = ad.ad_alignment ?: as->as_alignment;

  vo->vo_padding_left  =  as->as_margin_left;
  vo->vo_padding_right  = as->as_margin_right;

  switch(vo->vo_alignment) {
  case LAYOUT_ALIGN_TOP:
  case LAYOUT_ALIGN_TOP_LEFT:
  case LAYOUT_ALIGN_TOP_RIGHT:
    vo->vo_padding_top    = as->as_margin_vertical;
    break;

  case LAYOUT_ALIGN_BOTTOM:
  case LAYOUT_ALIGN_BOTTOM_LEFT:
  case LAYOUT_ALIGN_BOTTOM_RIGHT:
    vo->vo_padding_bottom = as->as_margin_vertical;
    break;
  }

  if(adc->adc_resx == 0 && adc->adc_resy == 0) {
    vo->vo_canvas_width  = 384;
    vo->vo_canvas_height = 288;
  } else if((adc->adc_resx == 1280 && adc->adc_resy == 0) ||
	    (adc->adc_resx == 0 && adc->adc_resy == 1024)) {
    vo->vo_canvas_width  = 1280;
    vo->vo_canvas_height = 1024;
  } else if(adc->adc_resx && adc->adc_resy) {
    vo->vo_canvas_width  = adc->adc_resx;
    vo->vo_canvas_height = adc->adc_resy;
  } else if(adc->adc_resx) {
    vo->vo_canvas_width  = adc->adc_resx;
    vo->vo_canvas_height = adc->adc_resx * 3 / 4;
  } else if(adc->adc_resy) {
    vo->vo_canvas_width  = adc->adc_resy * 4 / 3;
    vo->vo_canvas_height = adc->adc_resy;
  }

  vo->vo_layer = layer;
  return vo;
}


/**
 *
 */
void
sub_ass_render(media_pipe_t *mp, const char *src,
	       const uint8_t *header, int header_len,
	       int fontdomain)
{
  ass_decoder_ctx_t adc;

  if(strncmp(src, "Dialogue:", strlen("Dialogue:")))
    return;
  src += strlen("Dialogue:");
  adc_init(&adc);

  // Headers

  char *hdr;
  hdr = malloc(header_len + 1);
  memcpy(hdr, header, header_len);
  hdr[header_len] = 0;
  ass_decode_lines(&adc, hdr);
  free(hdr);

  // Dialogue
  video_overlay_t *vo = ad_dialogue_decode(&adc, src, fontdomain);

  if(vo != NULL)
    video_overlay_enqueue(mp, vo);

  adc_cleanup(&adc);
}


/**
 *
 */
static void
load_ssa_dialogue(ass_decoder_ctx_t *adc, const char *str)
{
  video_overlay_t *vo = ad_dialogue_decode(adc, str, 0);
  if(vo == NULL)
    return;
  ext_subtitles_t *es = adc->adc_opaque;
  TAILQ_INSERT_TAIL(&es->es_entries, vo, vo_link);
}


/**
 *
 */
ext_subtitles_t *
load_ssa(const char *url, char *buf, size_t len)
{
  ext_subtitles_t *es = calloc(1, sizeof(ext_subtitles_t));
  ass_decoder_ctx_t adc;

  adc_init(&adc);
  TAILQ_INIT(&es->es_entries);

  adc.adc_dialogue_handler = load_ssa_dialogue;
  adc.adc_opaque = es;

  ass_decode_lines(&adc, buf);
  adc_cleanup(&adc);
  return es;
}
