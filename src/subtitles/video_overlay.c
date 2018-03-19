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
#include "main.h"
#include "media/media.h"
#include "text/text.h"
#include "image/pixmap.h"
#include "misc/str.h"
#include "video_overlay.h"
#include "dvdspu.h"
#include "sub.h"
#include "subtitles/subtitles.h"
#include "ext/telxcc/hamming.h"
#include "ext/telxcc/teletext.h"
#include "time.h"

void
video_overlay_enqueue(media_pipe_t *mp, video_overlay_t *vo)
{
  hts_mutex_lock(&mp->mp_overlay_mutex);
  TAILQ_INSERT_TAIL(&mp->mp_overlay_queue, vo, vo_link);
  hts_mutex_unlock(&mp->mp_overlay_mutex);
}

#if ENABLE_LIBAV

/**
 * Decode subtitles from LAVC
 */
static void
video_subtitles_lavc(media_pipe_t *mp, media_buf_t *mb,
		     AVCodecContext *ctx)
{
  AVSubtitle sub;
  int got_sub = 0, i, x, y;
  video_overlay_t *vo;

  AVPacket avpkt;
  av_init_packet(&avpkt);
  avpkt.data = mb->mb_data;
  avpkt.size = mb->mb_size;

  vo = calloc(1, sizeof(video_overlay_t));
  vo->vo_type = VO_TIMED_FLUSH;
  vo->vo_start = mb->mb_pts;
  video_overlay_enqueue(mp, vo);

  if(avcodec_decode_subtitle2(ctx, &sub, &got_sub, &avpkt) < 1 || !got_sub)
    return;

  if(sub.num_rects == 0)
  {
    // Flush screen
    vo = calloc(1, sizeof(video_overlay_t));
    vo->vo_type = VO_TIMED_FLUSH;
    vo->vo_start = mb->mb_pts + sub.start_display_time * 1000;
    video_overlay_enqueue(mp, vo);
  }
  else
  {

    for(i = 0; i < sub.num_rects; i++) {
      AVSubtitleRect *r = sub.rects[i];

      switch(r->type) {

      case SUBTITLE_BITMAP:
	vo = calloc(1, sizeof(video_overlay_t));

	vo->vo_start = mb->mb_pts + sub.start_display_time * 1000;
	vo->vo_stop  = mb->mb_pts + sub.end_display_time * 1000;

        vo->vo_canvas_width  = ctx->width ? ctx->width : 720;
        vo->vo_canvas_height = ctx->height ? ctx->height : 576;

	vo->vo_x = r->x;
	vo->vo_y = r->y;

	vo->vo_pixmap = pixmap_create(r->w, r->h, PIXMAP_BGR32, 0);

	if(vo->vo_pixmap == NULL) {
	  free(vo);
	  break;
	}

	const uint8_t *src = r->pict.data[0];
	const uint32_t *clut = (uint32_t *)r->pict.data[1];

	for(y = 0; y < r->h; y++) {
	  uint32_t *dst = (uint32_t *)(vo->vo_pixmap->pm_data +
				       y * vo->vo_pixmap->pm_linesize);
	  for(x = 0; x < r->w; x++)
	    *dst++ = clut[src[x]];

	  src += r->pict.linesize[0];
	}
	video_overlay_enqueue(mp, vo);
	break;

      case SUBTITLE_ASS:
	sub_ass_render(mp, r->ass,
		       ctx->subtitle_header, ctx->subtitle_header_size,
		       mb->mb_font_context);
	break;

      default:
	break;
      }
    }
  }
  avsubtitle_free(&sub);
}

#endif

extern char font_subs[];

/**
 *
 */
video_overlay_t *
video_overlay_render_cleartext(const char *txt, int64_t start, int64_t stop,
			       int tags, int fontdomain)
{
  uint32_t *uc;
  int len, txt_len;
  video_overlay_t *vo;

  txt_len = strlen(txt);

  if(txt_len == 0) {
    vo = calloc(1, sizeof(video_overlay_t));
  } else {

    uint32_t pfx[6];

    pfx[0] = TR_CODE_COLOR | subtitle_settings.color;
    pfx[1] = TR_CODE_SHADOW | subtitle_settings.shadow_displacement;
    pfx[2] = TR_CODE_SHADOW_COLOR | subtitle_settings.shadow_color;
    pfx[3] = TR_CODE_OUTLINE | subtitle_settings.outline_size;
    pfx[4] = TR_CODE_OUTLINE_COLOR | subtitle_settings.outline_color;
    int pfxlen = 5;

    if(font_subs[0])
      pfx[pfxlen++] = TR_CODE_FONT_FAMILY |
	freetype_family_id(font_subs, fontdomain);

    uc = text_parse(txt, &len, tags, pfx, pfxlen, fontdomain);
    if(uc == NULL)
      return NULL;

    vo = calloc(1, sizeof(video_overlay_t));
    vo->vo_type = VO_TEXT;
    vo->vo_text = uc;
    vo->vo_text_length = len;
    vo->vo_padding_left = -1;  // auto padding
  }

  if(stop == PTS_UNSET) {
    stop = start + calculate_subtitle_duration(txt_len) * 1000000;
    vo->vo_stop_estimated = 1;
  }
  vo->vo_start = start;
  vo->vo_stop = stop;
  return vo;
}

/**
 * Calculate the number of seconds a subtitle should be displayed.
 * Min 2 seconds, max 7 seconds.
 */
int
calculate_subtitle_duration(int txt_len)
{
  return 2 + (txt_len / 74.0F) * 5; //74 is the maximum amount of characters a subtitler may fit on 2 lines of text.
}

typedef enum {
  DATA_UNIT_EBU_TELETEXT_NONSUBTITLE = 0x02,
  DATA_UNIT_EBU_TELETEXT_SUBTITLE = 0x03,
  DATA_UNIT_EBU_TELETEXT_INVERTED = 0x0c,
  DATA_UNIT_VPS = 0xc3,
  DATA_UNIT_CLOSED_CAPTIONS = 0xc5
} data_unit_t;

typedef struct {
  uint8_t _clock_in; // clock run in
  uint8_t _framing_code; // framing code, not needed, ETSI 300 706: const 0xe4
  uint8_t address[2];
  uint8_t data[40];
} teletext_packet_payload_t;

// subtitle type pages bitmap, 2048 bits = 2048 possible pages in teletext (excl. subpages)
uint8_t cc_map[256] = { 0 };

// global TS PCR value
uint32_t global_timestamp = 0;

// last timestamp computed
uint64_t last_timestamp = 0;

typedef enum {
  NO = 0x00,
  YES = 0x01,
  UNDEF = 0xff
} bool_t;

int page = 0;

// application states -- flags for notices that should be printed only once
struct {
  uint8_t programme_info_processed;
  uint8_t pts_initialized;
} states = {
  .programme_info_processed = NO,
  .pts_initialized = NO
};

// extracts magazine number from teletext page
#define MAGAZINE(p) ((p >> 8) & 0xf)

// extracts page number from teletext page
#define PAGE(p) (p & 0xff)

// ETS 300 706, chapter 8.2
static uint8_t unham_8_4(uint8_t a) {
  uint8_t r = UNHAM_8_4[a];
  if (r == 0xff) {
    r = 0;
    if(gconf.enable_dvb_teletext_debug && states.programme_info_processed == YES)
      TRACE(TRACE_DEBUG, "DVB_Teletext", "! Unrecoverable data error; UNHAM8/4(%02x)", a);
  }
  return (r & 0x0f);
}

const char* TTXT_COLOURS[8] = {
  //black,     red,       green,     yellow,    blue,      magenta,   cyan,      white
  "#000000", "#ff0000", "#00ff00", "#ffff00", "#0000ff", "#ff00ff", "#00ffff", "#ffffff"
};

// helper, array length function
#define ARRAY_LENGTH(a) (sizeof(a)/sizeof(a[0]))

typedef enum {
  TRANSMISSION_MODE_PARALLEL = 0,
  TRANSMISSION_MODE_SERIAL = 1
} transmission_mode_t;

typedef struct {
  uint64_t show_timestamp; // show at timestamp (in ms)
  uint64_t hide_timestamp; // hide at timestamp (in ms)
  uint16_t text[25][40]; // 25 lines x 40 cols (1 screen/page) of wide chars
  uint8_t tainted; // 1 = text variable contains any data
} teletext_page_t;

// entities, used in colour mode, to replace unsafe HTML tag chars
struct {
  uint16_t character;
  const char *entity;
} const ENTITIES[] = {
  { .character = '<', .entity = "&lt;" },
  { .character = '>', .entity = "&gt;" },
  { .character = '&', .entity = "&amp;" }
};

// working teletext page buffer
teletext_page_t page_buffer = { 0 };

// teletext transmission mode
transmission_mode_t transmission_mode = TRANSMISSION_MODE_SERIAL;

// flag indicating if incoming data should be processed or ignored
uint8_t receiving_data = NO;

// current charset (charset can be -- and always is -- changed during transmission)
struct {
  uint8_t current;
  uint8_t g0_m29;
  uint8_t g0_x28;
} primary_charset = {
  .current = 0x00,
  .g0_m29 = UNDEF,
  .g0_x28 = UNDEF
};

// ETS 300 706, chapter 8.3
static uint32_t unham_24_18(uint32_t a) {
  uint8_t test = 0;

  // Tests A-F correspond to bits 0-6 respectively in 'test'.
  for (uint8_t i = 0; i < 23; i++) test ^= ((a >> i) & 0x01) * (i + 33);
  // Only parity bit is tested for bit 24
  test ^= ((a >> 23) & 0x01) * 32;

  if ((test & 0x1f) != 0x1f) {
    // Not all tests A-E correct
    if ((test & 0x20) == 0x20) {
      // F correct: Double error
      return 0xffffffff;
    }
    // Test F incorrect: Single error
    a ^= 1 << (30 - test);
  }
  return (a & 0x000004) >> 2 | (a & 0x000070) >> 3 | (a & 0x007f00) >> 4 | (a & 0x7f0000) >> 5;
}

// UCS-2 (16 bits) to UTF-8 (Unicode Normalization Form C (NFC)) conversion
static void telx_ucs2_to_utf8(char *r, uint16_t ch) {
  if (ch < 0x80) {
    r[0] = ch & 0x7f;
    r[1] = 0;
    r[2] = 0;
    r[3] = 0;
  } else if (ch < 0x800) {
    r[0] = (ch >> 6) | 0xc0;
    r[1] = (ch & 0x3f) | 0x80;
    r[2] = 0;
    r[3] = 0;
  } else {
    r[0] = (ch >> 12) | 0xe0;
    r[1] = ((ch >> 6) & 0x3f) | 0x80;
    r[2] = (ch & 0x3f) | 0x80;
    r[3] = 0;
  }
}

// check parity and translate any reasonable teletext character into ucs2
static uint16_t telx_to_ucs2(uint8_t c) {
  if (PARITY_8[c] == 0) {
    if(gconf.enable_dvb_teletext_debug)
      TRACE(TRACE_DEBUG, "DVB_Teletext", "! Unrecoverable data error; PARITY(%02x)", c);
    return 0x20;
  }

  uint16_t r = c & 0x7f;
  if (r >= 0x20) r = G0[LATIN][r - 0x20];
  return r;
}

static void remap_g0_charset(uint8_t c) {
  if (c != primary_charset.current) {
    uint8_t m = G0_LATIN_NATIONAL_SUBSETS_MAP[c];
    if (m == 0xff) 
      TRACE(TRACE_INFO, "DVB_Teletext", "G0 Latin National Subset ID 0x%1x.%1x is not implemented\n", (c >> 3), (c & 0x7));
    else {
      for (uint8_t j = 0; j < 13; j++) G0[LATIN][G0_LATIN_NATIONAL_SUBSETS_POSITIONS[j]] = G0_LATIN_NATIONAL_SUBSETS[m].characters[j];
      TRACE(TRACE_INFO, "DVB_Teletext", "Using G0 Latin National Subset ID 0x%1x.%1x (%s)", (c >> 3), (c & 0x7), G0_LATIN_NATIONAL_SUBSETS[m].language);
      primary_charset.current = c;
    }
  }
}

static void process_page(teletext_page_t *page, media_pipe_t *mp, media_buf_t *mb) {
  // optimization: slicing column by column -- higher probability we could find boxed area start mark sooner
  uint8_t page_is_empty = YES;
  for (uint8_t col = 0; col < 40; col++) {
    for (uint8_t row = 1; row < 25; row++) {
      if (page->text[row][col] == 0x0b) {
        page_is_empty = NO;
        goto page_is_empty;
      }
    }
  }
  page_is_empty:
  if (page_is_empty == YES) return;
  if (page->show_timestamp > page->hide_timestamp) page->hide_timestamp = page->show_timestamp;	

  char *str = NULL;

  // process data
  for (uint8_t row = 1; row < 25; row++) {
    // anchors for string trimming purpose
    uint8_t col_start = 40;
    uint8_t col_stop = 40;

    for (int8_t col = 39; col >= 0; col--) {
      if (page->text[row][col] == 0xb) {
        col_start = col;
	break;
      }
    }
    // line is empty
    if (col_start > 39) continue;

    for (uint8_t col = col_start + 1; col <= 39; col++) {
      if (page->text[row][col] > 0x20) {
        if (col_stop > 39) col_start = col;
        col_stop = col;
      }
      if (page->text[row][col] == 0xa) break;
    }
    // line is empty
    if (col_stop > 39) continue;

    // ETS 300 706, chapter 12.2: Alpha White ("Set-After") - Start-of-row default condition.
    // used for colour changes _before_ start box mark
    // white is default as stated in ETS 300 706, chapter 12.2
    // black(0), red(1), green(2), yellow(3), blue(4), magenta(5), cyan(6), white(7)
    uint8_t foreground_color = 0x7;
    uint8_t font_tag_opened = NO;

    for (uint8_t col = 0; col <= col_stop; col++) {
      // v is just a shortcut
      uint16_t v = page->text[row][col];

      if (col < col_start && v <= 0x7) 
        foreground_color = v;
      
      if (col == col_start) {
        if ((foreground_color != 0x7)) {
          strappend(&str, "<font color=\"");
          strappend(&str, TTXT_COLOURS[foreground_color]);
          strappend(&str, "\">");
          font_tag_opened = YES;
        }
      }

      if (col >= col_start) {
	if (v <= 0x7) {
          // ETS 300 706, chapter 12.2: Unless operating in "Hold Mosaics" mode,
          // each character space occupied by a spacing attribute is displayed as a SPACE.
          if (font_tag_opened == YES) {
            strappend(&str, "</font> ");
            font_tag_opened = NO;
          }

          // black is considered as white for telxcc purpose
          // telxcc writes <font/> tags only when needed
          //if ((v > 0x0) && (v < 0x7)) {
          strappend(&str, "<font color=\"");
          strappend(&str, TTXT_COLOURS[v]);
          strappend(&str, "\">");
          font_tag_opened = YES;
          //} else v = 0x20;
        }

	if (v >= 0x20) {
          // translate some chars into entities, if in colour mode
          for (uint8_t i = 0; i < ARRAY_LENGTH(ENTITIES); i++) {
            if (v == ENTITIES[i].character) {
              strappend(&str, ENTITIES[i].entity);
              // v < 0x20 won't be printed in next block
              v = 0;
              break;
            }
          }
        }

        if (v >= 0x20) {
          char u[4] = { 0, 0, 0, 0 };
          telx_ucs2_to_utf8(u, v);
          strappend(&str, u);
        }
      }
    }

    // no tag will left opened!
    if (font_tag_opened == YES) {
      strappend(&str, "</font>");
      font_tag_opened = NO;
    }

    // line delimiter
    strappend(&str, "\n");
  }
  video_overlay_t *vo;
  vo = video_overlay_render_cleartext(str, mb->mb_pts,
                                    mb->mb_duration ?
                                    mb->mb_pts + mb->mb_duration :
                                    PTS_UNSET,
                                    TEXT_PARSE_HTML_TAGS |
                                    TEXT_PARSE_HTML_ENTITIES |
                                    TEXT_PARSE_SLOPPY_TAGS,
                                    mb->mb_font_context);

  if(vo != NULL)
  video_overlay_enqueue(mp, vo);
  if(gconf.enable_dvb_teletext_debug)
    TRACE(TRACE_DEBUG, "DVB_Teletext", "%s", str);

  free(str);
}

static void process_telx_packet(data_unit_t data_unit_id, teletext_packet_payload_t *packet, uint64_t timestamp, media_pipe_t *mp, media_buf_t *mb) {
  // variable names conform to ETS 300 706, chapter 7.1.2
  uint8_t address = (unham_8_4(packet->address[1]) << 4) | unham_8_4(packet->address[0]);
  uint8_t m = address & 0x7;
  if (m == 0) m = 8;
  uint8_t y = (address >> 3) & 0x1f;
  uint8_t designation_code = (y > 25) ? unham_8_4(packet->data[0]) : 0x00;
  
  if (y == 0) {
    // CC map
    uint8_t i = (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
    uint8_t flag_subtitle = (unham_8_4(packet->data[5]) & 0x08) >> 3;
    cc_map[i] |= flag_subtitle << (m - 1);

    if ((page == 0) && (flag_subtitle == YES) && (i < 0xff)) {
      page = (m << 8) | (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
      if(page == 0x100) page = 0x888; //hack for 36E
      TRACE(TRACE_INFO, "DVB_Teletext", "Detected teletext page is %03x, not guaranteed", page);
    }

    // Page number and control bits
    uint16_t page_number = (m << 8) | (unham_8_4(packet->data[1]) << 4) | unham_8_4(packet->data[0]);
    uint8_t charset = ((unham_8_4(packet->data[7]) & 0x08) | (unham_8_4(packet->data[7]) & 0x04) | (unham_8_4(packet->data[7]) & 0x02)) >> 1;
    //uint8_t flag_suppress_header = unham_8_4(packet->data[6]) & 0x01;
    //uint8_t flag_inhibit_display = (unham_8_4(packet->data[6]) & 0x08) >> 3;

    // ETS 300 706, chapter 9.3.1.3:
    // When set to '1' the service is designated to be in Serial mode and the transmission of a page is terminated
    // by the next page header with a different page number.
    // When set to '0' the service is designated to be in Parallel mode and the transmission of a page is terminated
    // by the next page header with a different page number but the same magazine number.
    // The same setting shall be used for all page headers in the service.
    // ETS 300 706, chapter 7.2.1: Page is terminated by and excludes the next page header packet
    // having the same magazine address in parallel transmission mode, or any magazine address in serial transmission mode.
    transmission_mode = unham_8_4(packet->data[7]) & 0x01;

    // FIXME: Well, this is not ETS 300 706 kosher, however we are interested in DATA_UNIT_EBU_TELETEXT_SUBTITLE only
    if ((transmission_mode == TRANSMISSION_MODE_PARALLEL) && (data_unit_id != DATA_UNIT_EBU_TELETEXT_SUBTITLE)) return;

    if ((receiving_data == YES) && (
      ((transmission_mode == TRANSMISSION_MODE_SERIAL) && (PAGE(page_number) != PAGE(page))) ||
      ((transmission_mode == TRANSMISSION_MODE_PARALLEL) && (PAGE(page_number) != PAGE(page)) && (m == MAGAZINE(page)))
      )) {
           receiving_data = NO;
           return;
    }

    // Page transmission is terminated, however now we are waiting for our new page
    if (page_number != page) return;

    // Now we have the begining of page transmission; if there is page_buffer pending, process it
    if (page_buffer.tainted == YES) {
      // it would be nice, if subtitle hides on previous video frame, so we contract 40 ms (1 frame @25 fps)
      page_buffer.hide_timestamp = timestamp - 40;
      process_page(&page_buffer, mp, mb);
    }

    page_buffer.show_timestamp = timestamp;
    page_buffer.hide_timestamp = 0;
    memset(page_buffer.text, 0x00, sizeof(page_buffer.text));
    page_buffer.tainted = NO;
    receiving_data = YES;
    primary_charset.g0_x28 = UNDEF;

    uint8_t c = (primary_charset.g0_m29 != UNDEF) ? primary_charset.g0_m29 : charset;
    remap_g0_charset(c);

    /* I know -- not needed; in subtitles we will never need disturbing teletext page status bar
       displaying tv station name, current time etc.
    if (flag_suppress_header == NO) {
      for (uint8_t i = 14; i < 40; i++) page_buffer.text[y][i] = telx_to_ucs2(packet->data[i]);
      page_buffer.tainted = YES;
    } */
  } else if ((m == MAGAZINE(page)) && (y >= 1) && (y <= 23) && (receiving_data == YES)) {
    // ETS 300 706, chapter 9.4.1: Packets X/26 at presentation Levels 1.5, 2.5, 3.5 are used for addressing
    // a character location and overwriting the existing character defined on the Level 1 page
    // ETS 300 706, annex B.2.2: Packets with Y = 26 shall be transmitted before any packets with Y = 1 to Y = 25;
    // so page_buffer.text[y][i] may already contain any character received
    // in frame number 26, skip original G0 character
    for (uint8_t i = 0; i < 40; i++) if (page_buffer.text[y][i] == 0x00) page_buffer.text[y][i] = telx_to_ucs2(packet->data[i]);
    page_buffer.tainted = YES;
  } else if ((m == MAGAZINE(page)) && (y == 26) && (receiving_data == YES)) {
    // ETS 300 706, chapter 12.3.2: X/26 definition
    uint8_t x26_row = 0;
    uint8_t x26_col = 0;
    uint32_t triplets[13] = { 0 };
    for (uint8_t i = 1, j = 0; i < 40; i += 3, j++) triplets[j] = unham_24_18((packet->data[i + 2] << 16) | (packet->data[i + 1] << 8) | packet->data[i]);
    for (uint8_t j = 0; j < 13; j++) {
      if (triplets[j] == 0xffffffff) {
        // invalid data (HAM24/18 uncorrectable error detected), skip group
        if(gconf.enable_dvb_teletext_debug && states.programme_info_processed == YES)
          TRACE(TRACE_DEBUG, "DVB_Teletext", "! Unrecoverable data error; UNHAM24/18()=%04x", triplets[j]);
        continue;
      }

      uint8_t data = (triplets[j] & 0x3f800) >> 11;
      uint8_t mode = (triplets[j] & 0x7c0) >> 6;
      uint8_t address = triplets[j] & 0x3f;
      uint8_t row_address_group = (address >= 40) && (address <= 63);

      // ETS 300 706, chapter 12.3.1, table 27: set active position
      if ((mode == 0x04) && (row_address_group == YES)) {
        x26_row = address - 40;
        if (x26_row == 0) x26_row = 24;
        x26_col = 0;
      }

      // ETS 300 706, chapter 12.3.1, table 27: termination marker
      if ((mode >= 0x11) && (mode <= 0x1f) && (row_address_group == YES)) break;

      // ETS 300 706, chapter 12.3.1, table 27: character from G2 set
      if ((mode == 0x0f) && (row_address_group == NO)) {
        x26_col = address;
        if (data > 31) page_buffer.text[x26_row][x26_col] = G2[0][data - 0x20];
      }
      
      // ETS 300 706, chapter 12.3.1, table 27: G0 character with diacritical mark
      if ((mode >= 0x11) && (mode <= 0x1f) && (row_address_group == NO)) {
        x26_col = address;

        // A - Z
        if ((data >= 65) && (data <= 90)) page_buffer.text[x26_row][x26_col] = G2_ACCENTS[mode - 0x11][data - 65];
        // a - z
        else if ((data >= 97) && (data <= 122)) page_buffer.text[x26_row][x26_col] = G2_ACCENTS[mode - 0x11][data - 71];
        // other
        else page_buffer.text[x26_row][x26_col] = telx_to_ucs2(data);
      }
    }
  } else if ((m == MAGAZINE(page)) && (y == 28) && (receiving_data == YES)) {
    // TODO:
    //   ETS 300 706, chapter 9.4.7: Packet X/28/4
    //   Where packets 28/0 and 28/4 are both transmitted as part of a page, packet 28/0 takes precedence over 28/4 for all but the colour map entry coding.
    if ((designation_code == 0) || (designation_code == 4)) {
      // ETS 300 706, chapter 9.4.2: Packet X/28/0 Format 1
      // ETS 300 706, chapter 9.4.7: Packet X/28/4
      uint32_t triplet0 = unham_24_18((packet->data[3] << 16) | (packet->data[2] << 8) | packet->data[1]);
      if (triplet0 == 0xffffffff) {
        // invalid data (HAM24/18 uncorrectable error detected), skip group
        if(gconf.enable_dvb_teletext_debug && states.programme_info_processed == YES)
          TRACE(TRACE_DEBUG, "DVB_Teletext", "! Unrecoverable data error; UNHAM24/18()=%04x", triplet0);
      } else {
        // ETS 300 706, chapter 9.4.2: Packet X/28/0 Format 1 only
        if ((triplet0 & 0x0f) == 0x00) {
          primary_charset.g0_x28 = (triplet0 & 0x3f80) >> 7;
          remap_g0_charset(primary_charset.g0_x28);
        }
      }
    }
  } else if ((m == MAGAZINE(page)) && (y == 29)) {
    // TODO:
    //   ETS 300 706, chapter 9.5.1 Packet M/29/0
    //   Where M/29/0 and M/29/4 are transmitted for the same magazine, M/29/0 takes precedence over M/29/4.
    if ((designation_code == 0) || (designation_code == 4)) {
      // ETS 300 706, chapter 9.5.1: Packet M/29/0
      // ETS 300 706, chapter 9.5.3: Packet M/29/4
      uint32_t triplet0 = unham_24_18((packet->data[3] << 16) | (packet->data[2] << 8) | packet->data[1]);

      if (triplet0 == 0xffffffff) {
        // invalid data (HAM24/18 uncorrectable error detected), skip group
        if(gconf.enable_dvb_teletext_debug && states.programme_info_processed == YES)
          TRACE(TRACE_DEBUG, "DVB_Teletext", "! Unrecoverable data error; UNHAM24/18()=%04x", triplet0);
      } else {
        // ETS 300 706, table 11: Coding of Packet M/29/0
        // ETS 300 706, table 13: Coding of Packet M/29/4
        if ((triplet0 & 0xff) == 0x00) {
          primary_charset.g0_m29 = (triplet0 & 0x3f80) >> 7;
          // X/28 takes precedence over M/29
	  if (primary_charset.g0_x28 == UNDEF) 
            remap_g0_charset(primary_charset.g0_m29);
        }
      }
    }
  } else if ((m == 8) && (y == 30)) {
    // ETS 300 706, chapter 9.8: Broadcast Service Data Packets
    if (states.programme_info_processed == NO) {
      // ETS 300 706, chapter 9.8.1: Packet 8/30 Format 1
      if (unham_8_4(packet->data[0]) < 2) {
        char *str = NULL;
        for (uint8_t i = 20; i < 40; i++) {
          uint8_t c = telx_to_ucs2(packet->data[i]);
          // strip any control codes from PID, eg. TVP station
          if (c < 0x20) continue;
          char u[4] = { 0, 0, 0, 0 };
          telx_ucs2_to_utf8(u, c);
          strappend(&str, u);
        }
        TRACE(TRACE_INFO, "DVB_Teletext", "Programme Identification Data = %s", str);
        free(str);
        // OMG! ETS 300 706 stores timestamp in 7 bytes in Modified Julian Day in BCD format + HH:MM:SS in BCD format
        // + timezone as 5-bit count of half-hours from GMT with 1-bit sign
        // In addition all decimals are incremented by 1 before transmission.
        uint32_t t = 0;
        // 1st step: BCD to Modified Julian Day
        t += (packet->data[10] & 0x0f) * 10000;
        t += ((packet->data[11] & 0xf0) >> 4) * 1000;
        t += (packet->data[11] & 0x0f) * 100;
        t += ((packet->data[12] & 0xf0) >> 4) * 10;
        t += (packet->data[12] & 0x0f);
        t -= 11111;
        // 2nd step: conversion Modified Julian Day to unix timestamp
        t = (t - 40587) * 86400;
        // 3rd step: add time
        t += 3600 * ( ((packet->data[13] & 0xf0) >> 4) * 10 + (packet->data[13] & 0x0f) );
        t +=   60 * ( ((packet->data[14] & 0xf0) >> 4) * 10 + (packet->data[14] & 0x0f) );
        t +=        ( ((packet->data[15] & 0xf0) >> 4) * 10 + (packet->data[15] & 0x0f) );
        t -= 40271;
        // 4th step: conversion to time_t
        time_t t0 = (time_t)t;
        // ctime output itself is \n-ended
        TRACE(TRACE_INFO, "DVB_Teletext", "Programme Timestamp (UTC) = %s", ctime(&t0));
        TRACE(TRACE_INFO, "DVB_Teletext", "Transmission mode = %s", (transmission_mode == TRANSMISSION_MODE_SERIAL ? "serial" : "parallel"));
        states.programme_info_processed = YES;
      }
    }
  }
}

/**
 *
 */
void
video_overlay_decode(media_pipe_t *mp, media_buf_t *mb)
{
  media_codec_t *mc = mb->mb_cw;

  if(mc == NULL && mb->mb_codecid) {
    if(mb->mb_codecid == AV_CODEC_ID_DVB_TELETEXT) {
      int i = 1;
      while (i <= mb->mb_size - 6) {
        uint8_t data_unit_id = mb->mb_data[i++];
        uint8_t data_unit_len = mb->mb_data[i++];
        if ((data_unit_id == DATA_UNIT_EBU_TELETEXT_NONSUBTITLE) || (data_unit_id == DATA_UNIT_EBU_TELETEXT_SUBTITLE)) {
          // teletext payload has always size 44 bytes
          if (data_unit_len == 44) {
            // reverse endianess (via lookup table), ETS 300 706, chapter 7.1
            for (uint8_t j = 0; j < data_unit_len; j++) mb->mb_data[i + j] = REVERSE_8[mb->mb_data[i + j]];
              // FIXME: This explicit type conversion could be a problem some day -- do not need to be platform independant
            process_telx_packet(data_unit_id, (teletext_packet_payload_t *)&mb->mb_data[i], last_timestamp, mp, mb);
          }
	}
        i += data_unit_len;
      }
    }
    if(mb->mb_codecid == AV_CODEC_ID_MOV_TEXT || mb->mb_codecid == AV_CODEC_ID_TEXT) {
      if(mb->mb_size < 2)
	return;

      int offset = 2;

      char *str = malloc(mb->mb_size + 1 - offset);
      memcpy(str, mb->mb_data + offset, mb->mb_size - offset);
      str[mb->mb_size - offset] = 0;

      video_overlay_t *vo;
      vo = video_overlay_render_cleartext(str, mb->mb_pts,
					mb->mb_duration ?
					mb->mb_pts + mb->mb_duration :
					PTS_UNSET,
                                        TEXT_PARSE_HTML_TAGS |
                                        TEXT_PARSE_HTML_ENTITIES |
                                        TEXT_PARSE_SLOPPY_TAGS,
					mb->mb_font_context);

      if(vo != NULL)
        video_overlay_enqueue(mp, vo);

      free(str);
    }
  } else {

    if(mc->decode)
      mc->decode(mc, NULL, NULL, mb, 0);
#if ENABLE_LIBAV
    else
      video_subtitles_lavc(mp, mb, mc->ctx);
#endif
  }
}


/**
 *
 */
void
video_overlay_destroy(video_overlay_t *vo)
{
  if(vo->vo_pixmap != NULL)
    pixmap_release(vo->vo_pixmap);
  free(vo->vo_text);
  free(vo);
}


/**
 *
 */
video_overlay_t *
video_overlay_dup(video_overlay_t *src)
{
  video_overlay_t *dst = malloc(sizeof(video_overlay_t));
  memcpy(dst, src, sizeof(video_overlay_t));

  if(src->vo_pixmap)
    dst->vo_pixmap = pixmap_dup(src->vo_pixmap);

  if(src->vo_text) {
    dst->vo_text = malloc(src->vo_text_length * sizeof(uint32_t));
    memcpy(dst->vo_text, src->vo_text, src->vo_text_length * sizeof(uint32_t));
  }
  return dst;
}

/**
 *
 */
void
video_overlay_dequeue_destroy(media_pipe_t *mp, video_overlay_t *vo)
{
  TAILQ_REMOVE(&mp->mp_overlay_queue, vo, vo_link);
  video_overlay_destroy(vo);
}


/**
 *
 */
void
video_overlay_flush_locked(media_pipe_t *mp, int send)
{
  states.programme_info_processed = page_buffer.tainted = receiving_data = NO;
  memset(page_buffer.text, 0x00, sizeof(page_buffer.text));
  page = 0;

  video_overlay_t *vo;

  while((vo = TAILQ_FIRST(&mp->mp_overlay_queue)) != NULL)
    video_overlay_dequeue_destroy(mp, vo);

  if(!send)
    return;

  vo = calloc(1, sizeof(video_overlay_t));
  vo->vo_type = VO_FLUSH;
  TAILQ_INSERT_TAIL(&mp->mp_overlay_queue, vo, vo_link);
}
