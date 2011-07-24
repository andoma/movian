/*
 *  Subtitles decoder / rendering
 *  Copyright (C) 2007 - 2010 Andreas Öman
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
#include "showtime.h"
#include "video_decoder.h"
#include "video_overlay.h"
#include "text/text.h"
#include "misc/pixmap.h"
#include "misc/string.h"
#include "sub.h"
#include "video_settings.h"

/**
 *
 */
static int64_t
ass_get_ts(const char *buf)
{
  if(strlen(buf) != 10)
    return AV_NOPTS_VALUE;

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

} ass_style_t;

static const ass_style_t ass_style_default = {
  .as_primary_color = 0xffffffff,
  .as_outline_color = 0xff000000,
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
  const char *adc_event_format;

  int adc_canvas_width;
  int adc_canvas_height;

} ass_decoder_ctx_t;


/**
 *
 */
static void
adc_release_styles(ass_decoder_ctx_t *adc)
{
  ass_style_t *as;
  while((as = LIST_FIRST(&adc->adc_styles)) != NULL) {
    LIST_REMOVE(as, as_link);
    free(as->as_name);
    free(as);
  }
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
  if((rgba & 0xff000000) == 0)
    rgba |= 0xff000000;

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
  as->as_primary_color = 0xffffffff;
  as->as_outline_color = 0xff000000;

  while(*fmt && *str) {
    gettoken(key, sizeof(key), &fmt);
    gettoken(val, sizeof(val), &str);

    if(!strcasecmp(key, "name"))
      mystrset(&as->as_name, val);
    else if(!strcasecmp(key, "alignment"))
      as->as_alignment = atoi(val);
    else if(!strcasecmp(key, "marginl"))
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
  }
  LIST_INSERT_HEAD(&adc->adc_styles, as, as_link);
}


/**
 *
 */
static void
ass_decode_line(ass_decoder_ctx_t *adc, const char *str)
{
  const char *s;
  if(!strcmp(str, "[Script Info]")) {
    adc->adc_section = ADC_SECTION_SCRIPT_INFO;
    return;
  }

  if(!strcmp(str, "[V4+ Styles]")) {
    adc->adc_section = ADC_SECTION_V4_STYLES;
    return;
  }

  if(!strcmp(str, "[Events]")) {
    adc->adc_section = ADC_SECTION_EVENTS;
    return;
  }

  if(*str == '[') {
    // Unknown section, better stay out of it
    adc->adc_section = ADC_SECTION_NONE;
    return;
  }

  switch(adc->adc_section) {
  default:
    break;
  case ADC_SECTION_SCRIPT_INFO:
    s = mystrbegins(str, "PlayResX:");
    if(s != NULL) {
      adc->adc_canvas_width = atoi(s);
      break;
    }

    s = mystrbegins(str, "PlayResY:");
    if(s != NULL) {
      adc->adc_canvas_height = atoi(s);
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
    if(s != NULL)
      adc->adc_event_format = s;

    break;
  }
}


/**
 *
 */
static void
ass_decode_header(ass_decoder_ctx_t *adc, char *s)
{
  int l;
  //  printf("%s\n", s);
  for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
    s[l] = 0;
    ass_decode_line(adc, s);
  }
}


/**
 *
 */
static const ass_style_t *
adc_find_style(ass_decoder_ctx_t *adc, const char *name)
{
  ass_style_t *as;
  LIST_FOREACH(as, &adc->adc_styles, as_link)
    if(!strcasecmp(as->as_name, name))
      return as;
  return &ass_style_default;
}


/**
 * Context to decode a dialogue line
 */
typedef struct ass_dialogue {
  
  ass_decoder_ctx_t ad_adc;

  uint32_t *ad_text;
  int ad_textlen;

  char *ad_hdr;
  char *ad_buf;

  int ad_fadein;
  int ad_fadeout;

} ass_dialoge_t;


static void
ad_txt_append(ass_dialoge_t *ad, int v)
{
  ad->ad_text = text_append(ad->ad_text, &ad->ad_textlen, v);
}


/**
 *
 */
static void
ass_handle_override(ass_dialoge_t *ad, const char *src, int len)
{
  char *str, *cmd;
  int v1, v2;
  if(len > 1000)
    return;

  str = alloca(len + 1);
  memcpy(str, src, len);
  str[len] = 0;
  
  while((cmd = strchr(str, '\\')) != NULL) {
    str = ++cmd;
    if(str[0] == 'i')
      ad_txt_append(ad, str[1] == '1' ? TR_CODE_ITALIC_ON : TR_CODE_ITALIC_OFF);
    else if(str[0] == 'b')
      ad_txt_append(ad, str[1] == '1' ? TR_CODE_BOLD_ON : TR_CODE_BOLD_OFF);
    else if(sscanf(str, "fad(%d,%d)", &v1, &v2) == 2) {
      ad->ad_fadein = v1 * 1000;
      ad->ad_fadeout = v2 * 1000;
    } else
      TRACE(TRACE_DEBUG, "ASS", "Can't handle override: %s", str);
  }
}

/**
 *
 */
static void
ad_dialogue_decode(ass_dialoge_t *ad, video_decoder_t *vd)
{
  char *tokens[10], *s;
  int i, layer;
  int64_t start, end;
  const char *str;
  int c;
  const ass_style_t *as;

  ad->ad_buf[strcspn(ad->ad_buf, "\n\r")] = 0;
  tokens[0] = ad->ad_buf;
  for(i = 1; i < 10; i++) {
    s = strchr(tokens[i-1], ',');
    if(s == NULL)
      return;
    *s++ = 0;
    tokens[i] = s;
  }

  layer = atoi(tokens[0]);
  start = ass_get_ts(tokens[1]);
  end   = ass_get_ts(tokens[2]);


  if(start == AV_NOPTS_VALUE || end == AV_NOPTS_VALUE)
    return;

  as = adc_find_style(&ad->ad_adc, tokens[3]);

  if(as->as_bold)
    ad_txt_append(ad, TR_CODE_BOLD_ON);
  if(as->as_italic)
    ad_txt_append(ad, TR_CODE_ITALIC_ON);
  if(as->as_fontname)
    ad_txt_append(ad, TR_CODE_FONT_FAMILY |
		  freetype_family_id(as->as_fontname));

  ad_txt_append(ad, TR_CODE_SIZE_PX | as->as_fontsize);

  if(as == &ass_style_default || subtitle_settings.style_override) {

    ad_txt_append(ad, TR_CODE_COLOR | subtitle_settings.color);
    ad_txt_append(ad, TR_CODE_OUTLINE_COLOR | subtitle_settings.outline_color);
    ad_txt_append(ad, TR_CODE_SHADOW_COLOR | subtitle_settings.shadow_color);

    ad_txt_append(ad, TR_CODE_SHADOW | subtitle_settings.shadow_displacement);
    ad_txt_append(ad, TR_CODE_OUTLINE | subtitle_settings.outline_size);

  } else {

    ad_txt_append(ad, TR_CODE_COLOR | (as->as_primary_color & 0xffffff));
    ad_txt_append(ad, TR_CODE_ALPHA | (as->as_primary_color >> 24));
    ad_txt_append(ad, TR_CODE_OUTLINE_COLOR |(as->as_outline_color & 0xffffff));
    ad_txt_append(ad, TR_CODE_OUTLINE_ALPHA | (as->as_outline_color >> 24));
    ad_txt_append(ad, TR_CODE_SHADOW_COLOR | (as->as_back_color & 0xffffff));
    ad_txt_append(ad, TR_CODE_SHADOW_ALPHA | (as->as_back_color >> 24));

    if(as->as_shadow)
      ad_txt_append(ad, TR_CODE_SHADOW | (as->as_shadow & 0xff));

    if(as->as_outline)
      ad_txt_append(ad, TR_CODE_OUTLINE | (as->as_outline & 0xff));
  }

  str = tokens[9];
  while((c = utf8_get(&str)) != 0) {
    if(c == '\\' && (*str == 'n' || *str == 'N')) {
      str++;
      ad_txt_append(ad, '\n');
      continue;
    }

    if(c == '{') {
      const char *end = strchr(str, '}');
      if(end == NULL)
	break;

      ass_handle_override(ad, str, end - str);

      str = end + 1;
      continue;
    }

    ad_txt_append(ad, c);
  }

  video_overlay_t *vo = calloc(1, sizeof(video_overlay_t));
  vo->vo_type = VO_TEXT;

  vo->vo_text = malloc(ad->ad_textlen * sizeof(uint32_t));
  vo->vo_text_length = ad->ad_textlen;
  memcpy(vo->vo_text, ad->ad_text, ad->ad_textlen * sizeof(uint32_t));

  vo->vo_start = start;
  vo->vo_stop = end;
  vo->vo_fadein = ad->ad_fadein;
  vo->vo_fadeout = ad->ad_fadeout;
  vo->vo_alignment = as->as_alignment;

  vo->vo_padding_left  =  as->as_margin_left;
  vo->vo_padding_top    = as->as_margin_vertical;
  vo->vo_padding_right  = as->as_margin_right;
  vo->vo_padding_bottom = as->as_margin_vertical;

  vo->vo_canvas_width  = ad->ad_adc.adc_canvas_width ?: -1;
  vo->vo_canvas_height = ad->ad_adc.adc_canvas_height ?: -1;

  vo->vo_layer = layer;
  video_overlay_enqueue(vd, vo);
}


/**
 *
 */
void
sub_ass_render(video_decoder_t *vd, const char *src,
	       const uint8_t *header, int header_len)
{
  ass_dialoge_t ad;

  if(strncmp(src, "Dialogue:", strlen("Dialogue:")))
    return;
  src += strlen("Dialogue:");
  
  memset(&ad, 0, sizeof(ad));

  // Headers

  ad.ad_hdr = malloc(header_len + 1);
  memcpy(ad.ad_hdr, header, header_len);
  ad.ad_hdr[header_len] = 0;
  
  ad.ad_buf = strdup(src);

  ass_decode_header(&ad.ad_adc, ad.ad_hdr);

  // Dialogue

  ad_dialogue_decode(&ad, vd);
  adc_release_styles(&ad.ad_adc);
  free(ad.ad_text);
  free(ad.ad_buf);
  free(ad.ad_hdr);
}
