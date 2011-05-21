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
  .as_fontsize = 20,
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


static int
hexnibble(char c)
{
  switch(c) {
  case '0' ... '9':    return c - '0';
  case 'a' ... 'f':    return c - 'a' + 10;
  case 'A' ... 'F':    return c - 'A' + 10;
  default:
    return -1;
  }
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
  case 6: rgba |= hexnibble(str[l-6]) << 4;
  case 5: rgba |= hexnibble(str[l-5]) << 0;
  case 4: rgba |= hexnibble(str[l-4]) << 12;
  case 3: rgba |= hexnibble(str[l-3]) << 8;
  case 2: rgba |= hexnibble(str[l-2]) << 20;
  case 1: rgba |= hexnibble(str[l-1]) << 16;
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
      as->as_outline = !!atoi(val);
    else if(!strcasecmp(key, "shadow"))
      as->as_shadow = MIN((unsigned int)atoi(val), 4);
    else if(!strcasecmp(key, "fontsize"))
      as->as_fontsize = atoi(val);
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
  int i;
  int64_t start, end;
  const char *str;
  int c;
  const ass_style_t *as;
  int vwidth  = vd->vd_mp->mp_video_width;
  int vheight = vd->vd_mp->mp_video_height;

  ad->ad_buf[strcspn(ad->ad_buf, "\n\r")] = 0;

  tokens[0] = ad->ad_buf;
  for(i = 1; i < 10; i++) {
    s = strchr(tokens[i-1], ',');
    if(s == NULL)
      return;
    *s++ = 0;
    tokens[i] = s;
  }

  start = ass_get_ts(tokens[1]);
  end   = ass_get_ts(tokens[2]);


  if(start == AV_NOPTS_VALUE || end == AV_NOPTS_VALUE)
    return;

  as = adc_find_style(&ad->ad_adc, tokens[3]);

  if(as->as_bold)
    ad_txt_append(ad, TR_CODE_BOLD_ON);
  if(as->as_italic)
    ad_txt_append(ad, TR_CODE_ITALIC_ON);


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

  int maxwidth = vwidth - as->as_margin_left - as->as_margin_right;
  if(maxwidth < 10)
    return;

  uint8_t red, green, blue, alpha;

  int fontsize = as->as_fontsize * subtitle_setting_scaling / 100;
  int alignment = (const int[]){TR_ALIGN_LEFT, TR_ALIGN_CENTER,
				TR_ALIGN_RIGHT}[(as->as_alignment - 1) % 3];

  pixmap_t *pm = text_render(ad->ad_text, ad->ad_textlen,
			     0, fontsize, alignment, maxwidth, 10, NULL);

  if(pm == NULL)
    return;

  pixmap_t *mask = pixmap_extract_channel(pm, 3);

  pixmap_t *out = pixmap_create(pm->pm_width  + as->as_shadow,
				pm->pm_height + as->as_shadow,
				PIX_FMT_BGR32);

  if(as->as_shadow) {
    pixmap_t *blur = pixmap_convolution_filter(mask, PIXMAP_BLUR);
    pixmap_composite(out, blur, as->as_shadow, as->as_shadow, 0, 0, 0, 128);
    pixmap_release(blur);
  }

  if(as->as_outline) {
    pixmap_t *edges = pixmap_convolution_filter(mask, PIXMAP_EDGE_DETECT);

    blue  = as->as_outline_color;
    green = as->as_outline_color >> 8;
    red   = as->as_outline_color >> 16;
    alpha = as->as_outline_color >> 24;
    pixmap_composite(out, edges, 0, 0, red, green, blue, alpha);
    pixmap_release(edges);
  }

  pixmap_release(mask);

  blue  = as->as_primary_color;
  green = as->as_primary_color >> 8;
  red   = as->as_primary_color >> 16;
  alpha = as->as_primary_color >> 24;
  pixmap_composite(out, pm, 0, 0, red, green, blue, alpha);

  video_overlay_t *vo = video_overlay_from_pixmap(out);
  pixmap_release(out);

  vo->vo_start = start;
  vo->vo_stop = end;
  vo->vo_fadein = ad->ad_fadein;
  vo->vo_fadeout = ad->ad_fadeout;

  switch(as->as_alignment) {
  case 1 ... 3:
    vo->vo_y = vheight - pm->pm_height - as->as_margin_vertical;
    break;
    
  case 4 ... 6:
    vo->vo_y = vheight / 2 - pm->pm_height / 2;
    break;

  case 7 ... 9:
    vo->vo_y = as->as_margin_vertical;
    break;
  }

  
  switch((as->as_alignment - 1) % 3) {
  case 0:
    vo->vo_x = as->as_margin_left;
    break;
    
  case 1:
    vo->vo_x = vwidth / 2 - pm->pm_width / 2;
    break;
    
  case 2:
    vo->vo_x = vwidth - as->as_margin_right - pm->pm_width;
    break;
  }

  pixmap_release(pm);
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

  if(vd->vd_mp->mp_video_width < 10 ||  vd->vd_mp->mp_video_height < 10)
    return;

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
