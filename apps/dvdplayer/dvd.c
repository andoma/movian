/*
 *  DVD player
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

#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/cdrom.h>
#include <ctype.h>


#include <unistd.h>
#include <stdlib.h>

#include <netinet/in.h>

#include "showtime.h"
#include "dvd.h"
#include "input.h"
#include "video/video_decoder.h"
#include "video/video_menu.h"
#include "layout/layout.h"
#include "layout/layout_forms.h"

static int dvd_ctrl_input(dvd_player_t *dp, int wait);

static int dvd_player_menu(dvd_player_t *dp, ic_t *ic, media_pipe_t *mp);

#define DVD_AUDIO_STREAMS 8

#define DVD_SPU_STREAMS   32
#define DVD_SPU_MASK      (DVD_SPU_STREAMS - 1)







static int
dvd_filter_audio(void *aux, uint32_t sc, int codec_id)
{
  dvd_player_t *dp = aux;

  switch(dp->dp_audio_track) {
  case DP_AUDIO_DISABLE:
    return 0;

  case DP_AUDIO_FOLLOW_VM:
    return (dp->dp_audio_track_vm & 0x7) == (sc & 0x7);

  default:
    return (dp->dp_audio_track & 0x7) == (sc & 0x7);
  }
}

static int
dvd_filter_spu(void *aux, uint32_t sc, int codec_id)
{
  dvd_player_t *dp = aux;

  if(dp->dp_inmenu)
    return (dp->dp_spu_track_vm & 0x1f) == (sc & 0x1f);

  switch(dp->dp_spu_track) {
  case DP_SPU_DISABLE:
    return 0;

  case DP_AUDIO_FOLLOW_VM:
    return (dp->dp_spu_track_vm & 0x1f) == (sc & 0x1f);

  default:
    return (dp->dp_spu_track & 0x1f) == (sc & 0x1f);
  }
}


static void
dvd_ps_block(dvd_player_t *dp, uint8_t *buf, int len)
{ 
  uint32_t startcode;
  int pes_len;

  int stuff = buf[13] & 7;

  buf += 14;
  len -= 14;

  if(stuff != 0)
    return;

  while(len > 0) {

    if(len < 4)
      break;

    startcode = getu32(buf, len);
    pes_len = getu16(buf, len); 

    if(pes_len < 3)
      break;

    switch(startcode) {
    case PADDING_STREAM:
    case PRIVATE_STREAM_1:
    case PRIVATE_STREAM_2:
    case 0x1c0 ... 0x1df:
    case 0x1e0 ... 0x1ef:
      pes_do_block(&dp->dp_pp, startcode, buf, pes_len, 1, 0);
      len -= pes_len;
      buf += pes_len;
      break;

    default:
      return;
    }
  }
}


static void
dvd_audio_stream_change(dvd_player_t *dp, 
			dvdnav_audio_stream_change_event_t *dat)
{
  dp->dp_audio_track_vm = dat->physical;
}


static void
dvd_spu_stream_change(dvd_player_t *dp, 
		      dvdnav_spu_stream_change_event_t *dat)
{
  dp->dp_spu_track_vm = dat->physical_wide;
}


static void
dvd_gen_frame_kick(dvd_player_t *dp)
{
  media_buf_t *mb;
  codecwrap_t *cw = dp->dp_pp.pp_video.ps_cw;
  media_pipe_t *mp = &dp->dp_ai->ai_mp;

  if(cw == NULL)
    return;

  wrap_lock_codec(cw);

  mb = media_buf_alloc();
  mb->mb_cw = wrap_codec_ref(cw);
  mb->mb_size = 0;
  mb->mb_data = NULL;
  mb->mb_aspect_override = dp->dp_pp.pp_aspect_override;
  mb->mb_data_type = MB_VIDEO;
  mb->mb_duration = 1000000LL * av_q2d(cw->codec_ctx->time_base);

  wrap_unlock_codec(cw);
  mb_enqueue(mp, &mp->mp_video, mb);
}





static void
dvd_flush(dvd_player_t *dp)
{
  media_pipe_t *mp = &dp->dp_ai->ai_mp;

  dp->dp_pp.pp_audio.ps_force_reset = 1;
  dp->dp_pp.pp_video.ps_force_reset = 1;
  dp->dp_pp.pp_spu.ps_force_reset = 1;

  mp_flush(mp);
}


static char *
make_nice_title(const char *t)
{
  int l = strlen(t);
  char *ret = malloc(l + 1);
  char *r;
  int uc = 1;

  ret[l] = 0;
  r = ret;

  while(*t) {
    if(*t == '_' || *t == ' ') {
      *r++ = ' ';
      uc = 1;
    } else if(uc) {
      *r++ = toupper(*t);
      uc = 0;
    } else {
      *r++ = tolower(*t);
    }
    t++;
  }
  return ret;
}






static void
dvd_update_info(dvd_player_t *dp)
{
  int title, titles, part, parts, s, m, h;
  char tmp[100];
  int64_t t;

  return;

  dvdnav_get_number_of_titles(dp->dp_dvdnav, &titles);
  dvdnav_current_title_info(dp->dp_dvdnav, &title, &part);
  dvdnav_get_number_of_parts(dp->dp_dvdnav, title, &parts);
  t = dvdnav_get_current_time(dp->dp_dvdnav);

  if(title < 1 || title > titles)
    strcpy(tmp, "");
  else
    sprintf(tmp, "Title: %d / %d", title, titles);
  glw_set(dp->dp_widget_title, GLW_ATTRIB_CAPTION, tmp, NULL);

  if(tmp[0] == 0 || part < 1 || part > parts)
    strcpy(tmp, "");
  else
    sprintf(tmp, "Chapter: %d / %d", part, parts);

  glw_set(dp->dp_widget_chapter, GLW_ATTRIB_CAPTION, tmp, NULL);
  
  s = t / 90000LL;
  m = s / 60;
  h = s / 3600;

  sprintf(tmp, "%02d:%02d:%02d", h, m % 60, s % 60);

  glw_set(dp->dp_widget_time, GLW_ATTRIB_CAPTION, tmp, NULL);

}

/**
 *
 */
static void *
dvd_player_thread(void *aux)
{
  dvd_player_t *dp = aux;
  media_pipe_t *mp = dp->dp_mp;
  appi_t *      ai = dp->dp_ai;

  int result, run = 1, kickctd = 0, len, event, i;
  uint8_t mem[DVD_VIDEO_LB_LEN], *block;
  pci_t *pci;
  dvdnav_highlight_event_t *hevent;
  void *data;
  inputevent_t ie;


  mp_set_playstatus(mp, MP_PLAY);

  media_pipe_acquire_audio(mp);


  while(run) {

    if(mp_is_paused(mp)) {
      run = dvd_ctrl_input(dp, 255);
      continue;
    }

    if(mp->mp_video.mq_len == 0) {
      kickctd++;

      if(kickctd > 1) {

	/* Video queue is empty, send an emtpy packet to force video decoder
	   to flush pending frames */

	dvd_gen_frame_kick(dp);

      }
    } else {
      kickctd = 0;
    }

    block = mem;
    result = dvdnav_get_next_cache_block(dp->dp_dvdnav, &block, &event, &len);

    if(result == DVDNAV_STATUS_ERR) {
      run = 0;
      break;
    }

    switch(event) {
    case DVDNAV_BLOCK_OK:
      ai->ai_req_fullscreen = 1;
      kickctd = 3;
      dvd_ps_block(dp, block, len);
      break;

    case DVDNAV_NOP:
      break;

    case DVDNAV_STILL_FRAME:
      dvd_gen_frame_kick(dp);
      run = dvd_ctrl_input(dp, *(int *)block);
      continue;

    case DVDNAV_SPU_STREAM_CHANGE:
      dvd_spu_stream_change(dp, (dvdnav_spu_stream_change_event_t *) block);
      break;


    case DVDNAV_AUDIO_STREAM_CHANGE:
      dvd_audio_stream_change(dp, (dvdnav_audio_stream_change_event_t *)
			      block);
      break;
      
    case DVDNAV_CELL_CHANGE:
      mp_send_cmd(mp, &mp->mp_video, MB_RESET_SPU);
      break;

    case DVDNAV_WAIT:
 
      /* wait for fifos to drain */

      if(mp->mp_video.mq_len == 0 && mp->mp_audio.mq_len == 0) {
	dvdnav_wait_skip(dp->dp_dvdnav);
      } else {
	usleep(10000);
      }
      break;

    case DVDNAV_NAV_PACKET:
      pci = malloc(sizeof(pci_t));
      memcpy(pci, dvdnav_get_current_nav_pci(dp->dp_dvdnav), sizeof(pci_t));
      
      dp->dp_inmenu = pci->hli.hl_gi.hli_ss ? 1 : 0;
      dvd_update_info(dp);
      
      mp_send_cmd_data(mp, &mp->mp_video, MB_DVD_PCI, pci);
      break;

    case DVDNAV_HIGHLIGHT:
      hevent = (dvdnav_highlight_event_t *) block;
      mp_send_cmd_u32_head(mp, &mp->mp_video, MB_DVD_HILITE, hevent->buttonN);
      break;

    case DVDNAV_VTS_CHANGE:
      i = dvdnav_get_video_aspect(dp->dp_dvdnav);
      dp->dp_pp.pp_aspect_override = i ? 2 : 1;
      mp_wait(mp, 1, 0);
      mp_send_cmd(mp, &mp->mp_video, MB_RESET_SPU);
      mp_send_cmd(mp, &mp->mp_video, MB_RESET);

      dp->dp_spu_track   = DP_SPU_FOLLOW_VM;
      dp->dp_audio_track = DP_AUDIO_FOLLOW_VM;
      dvd_flush(dp);
      break;
      
    case DVDNAV_HOP_CHANNEL:
      dvd_flush(dp);
      dvd_update_info(dp);
      break;

    case DVDNAV_SPU_CLUT_CHANGE:
      data = malloc(sizeof(uint32_t) * 16);
      memcpy(data, block, sizeof(uint32_t) * 16);
      mp_send_cmd_data(mp, &mp->mp_video, MB_CLUT, data);
      break;

    default:
      printf("Unhandled event %d\n", event);
      usleep(10);
      break;
    }

    dvdnav_free_cache_block(dp->dp_dvdnav, block);

    run = dvd_ctrl_input(dp, 0);
  }

  /* Make control thread stop */
  ie.type = INPUT_KEY;
  ie.u.key = INPUT_KEY_STOP;
  input_postevent(&ai->ai_ic, &ie);

  mp_set_playstatus(mp, MP_STOP);
  return NULL;
}


/**
 *
 */
int
dvd_main(appi_t *ai, const char *url, int isdrive, glw_t *parent)
{
  dvd_player_t *dp = alloca(sizeof(dvd_player_t));
  int rcode = 0;
  const char *rawtitle;
  char *title;
  formatwrap_t *fw;
  media_pipe_t *mp;
  //  int options = 0;
  int run = 1, key;
  inputevent_t ie;
  glw_t *top;
  pthread_t player_tid;

  memset(dp, 0, sizeof(dvd_player_t));

  mp = &ai->ai_mp;
  dp->dp_mp = mp;
  dp->dp_ai = ai;

  if(!strncmp("file://", url, 7))
    url += 7;

  /**
   * Create top level stack
   */
  top = glw_create(GLW_CONTAINER_Z,
		   GLW_ATTRIB_PARENT, parent,
		   NULL);


  /**
   * Status overlay
   */
#if 0
  dp->dp_status_overlay = 
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_PARENT, top,
	       GLW_ATTRIB_SIGNAL_HANDLER, overlay_callback, dp, 100,
	       GLW_ATTRIB_FILENAME, "dvdplayback/overlay",
	       NULL);
#endif


  if(dvdnav_open(&dp->dp_dvdnav, url) != DVDNAV_STATUS_OK) {
    //    dvd_display_error(w, "An error occured when opening DVD", isdrive);
    glw_destroy(top);
    return -1;
  }

  dvdnav_set_readahead_flag(dp->dp_dvdnav, 1);
  dvdnav_set_PGC_positioning_flag(dp->dp_dvdnav, 1);
  dvdnav_get_title_string(dp->dp_dvdnav, &rawtitle);

  title = make_nice_title(rawtitle);
  if(*title == 0) {
    title = strrchr(url, '/');
    if(title != NULL)
      title = make_nice_title(title + 1);
  }

#if 0
  if(isdrive)
    options = DVD_START_MENU_EJECT;
  if(options) {
    if((rcode = dvd_start_menu(ai, w, title, options)) != 0) {
      dvdnav_close(dp->dp_dvdnav);
      glw_destroy(w);
      return rcode;
    }
  }
#endif


  vd_conf_init(&dp->dp_vdc);
  mp_set_video_conf(mp, &dp->dp_vdc);
  dp->dp_vdc.gc_deilace_type = VD_DEILACE_NONE;

  vd_create_widget(top, &ai->ai_mp, 1.0f);

  vd_set_dvd(&ai->ai_mp, dp);

  /**
   * Menu cubestack
   */

  dp->dp_menu_playfield = 
    glw_create(GLW_CUBESTACK,
	       GLW_ATTRIB_PARENT, top,
	       NULL);


  dp->dp_audio_track = DP_AUDIO_FOLLOW_VM;
  dp->dp_spu_track   = DP_SPU_FOLLOW_VM;

  /* Init MPEG elementary stream decoder */

  fw = wrap_format_create(NULL, 1);

  pes_init(&dp->dp_pp, &ai->ai_mp, fw);

  dp->dp_pp.pp_audio.ps_filter_cb = dvd_filter_audio;
  dp->dp_pp.pp_spu.ps_filter_cb = dvd_filter_spu;

  dp->dp_pp.pp_video.ps_aux = dp;
  dp->dp_pp.pp_audio.ps_aux = dp;
  dp->dp_pp.pp_spu.ps_aux = dp;

  dp->dp_pp.pp_video.ps_output = &mp->mp_video;
  dp->dp_pp.pp_spu.ps_output =   &mp->mp_video;
  dp->dp_pp.pp_audio.ps_output = &mp->mp_audio;

  input_init(&dp->dp_ic);

  pthread_create(&player_tid, NULL, dvd_player_thread, dp);

  while(run) {
    input_getevent(&ai->ai_ic, 1, &ie, NULL);

    switch(ie.type) {

    default:
      break;

      /**
       * Keyboard input
       */
    case INPUT_KEY:
      key = ie.u.key;

      switch(key) {
      case INPUT_KEY_STOP:
      case INPUT_KEY_EJECT:
	run = 0;
	printf("Sending STOP\n");
	ie.u.key = INPUT_KEY_STOP;
	break;

      case INPUT_KEY_MENU:
	run = dvd_player_menu(dp, &ai->ai_ic, mp);
	continue;

      default:
	break;

      }
    }
    input_postevent(&dp->dp_ic, &ie);
  }

  /* Make sure player stops */
  ie.u.key = INPUT_KEY_STOP;
  run = 0;
  input_postevent(&dp->dp_ic, &ie);

  /* collect the player thread */

  pthread_join(player_tid, NULL);
  input_flush_queue(&dp->dp_ic);


  ai->ai_req_fullscreen = 0;

  pes_deinit(&dp->dp_pp);

  wrap_format_wait(fw);

  free(title);

  dvdnav_close(dp->dp_dvdnav);

#if 0
  if(rcode == -1)
    dvd_display_error(w, "An error occured during DVD decoding.", isdrive);
#endif

  glw_destroy(top);
  return rcode;
}


static int
dvd_ctrl_input(dvd_player_t *dp, int wait)
{
  ic_t         *ic = &dp->dp_ic;
  media_pipe_t *mp =  dp->dp_mp;

  struct timespec ts;
  int key, r;
  pci_t *pci;
  inputevent_t ie;

  switch(wait) {
  case 0: /* no timeout, no wait */
    r = input_getevent(ic, 0, &ie, NULL);
    break;

  case 1 ... 254:  /* wait n seconds and skip if timeout */
    ts.tv_sec = time(NULL) + wait;
    ts.tv_nsec = 0;
    if(input_getevent(ic, 1, &ie, &ts)) {
      dvdnav_still_skip(dp->dp_dvdnav);
      return 1;
    }
    break;

  case 255:  /* wait forever */
    r = input_getevent(ic, 1, &ie, NULL);
    break;
  }

  key = ie.type == INPUT_KEY ? ie.u.key : 0;

  if(key == 0)
    return 1;

  media_pipe_acquire_audio(mp);

  pci = &dp->dp_pci;

  switch(key) {
  case INPUT_KEY_STOP:
    return 0;

  case INPUT_KEY_PLAYPAUSE:
  case INPUT_KEY_PLAY:
  case INPUT_KEY_PAUSE:
    mp_playpause(mp, key);
    return 1;


  case INPUT_KEY_DVD_AUDIO_MENU:
    mp_playpause(mp, MP_PLAY);
    dvdnav_menu_call(dp->dp_dvdnav, DVD_MENU_Audio);
    return 1;

  case INPUT_KEY_DVD_SPU_MENU:
    mp_playpause(mp, MP_PLAY);
    dp->dp_spu_track = DP_SPU_FOLLOW_VM;
    dvdnav_menu_call(dp->dp_dvdnav, DVD_MENU_Subpicture);
    return 1;
  }

  if(!dp->dp_inmenu) {
    switch(key) {

    case INPUT_KEY_SEEK_BACKWARD:
      dvdnav_sector_search(dp->dp_dvdnav, -10000, SEEK_CUR);
      return 1;

    case INPUT_KEY_SEEK_FORWARD:
      dvdnav_sector_search(dp->dp_dvdnav, 10000, SEEK_CUR);
      return 1;
    }
  }

  if(mp_is_paused(mp))
    return 1;  /* no other actions allows if paused */

  switch(key) {
  case INPUT_KEY_UP:
    dvdnav_upper_button_select(dp->dp_dvdnav, pci);
    break;
  case INPUT_KEY_DOWN:
    dvdnav_lower_button_select(dp->dp_dvdnav, pci);
    break;
  case INPUT_KEY_LEFT:
    dvdnav_left_button_select(dp->dp_dvdnav, pci);
    break;
  case INPUT_KEY_RIGHT:
    dvdnav_right_button_select(dp->dp_dvdnav, pci);
    break;
  case INPUT_KEY_ENTER:
    dvdnav_button_activate(dp->dp_dvdnav, pci);
    break;
  case INPUT_KEY_NEXT:
    dvd_flush(dp);
    dvdnav_next_pg_search(dp->dp_dvdnav);
    break;
  case INPUT_KEY_PREV:
    dvd_flush(dp);
    dvdnav_prev_pg_search(dp->dp_dvdnav);
    break;
  case INPUT_KEY_DVDUP:
    dvd_flush(dp);
    dvdnav_go_up(dp->dp_dvdnav);
    break;
  case INPUT_KEY_RESTART_TRACK:
    dvd_flush(dp);
    dvdnav_top_pg_search(dp->dp_dvdnav);
    break;
  }
  return 1;
}


/**
 * DVD language codes
 */
const struct {
  const char *langcode;
  const char *displayname;
} langtbl[] = {
  {"AB", "Abkhazian"},
  {"LT", "Lithuanian"},
  {"AA", "Afar"},
  {"MK", "Macedonian"},
  {"AF", "Afrikaans"},
  {"MG", "Malagasy"},
  {"SQ", "Albanian"},
  {"MS", "Malay"},
  {"AM", "Amharic"},
  {"ML", "Malayalam"},
  {"AR", "Arabic"},
  {"MT", "Maltese"},
  {"HY", "Armenian"},
  {"MI", "Maori"},
  {"AS", "Assamese"},
  {"MR", "Marathi"},
  {"AY", "Aymara"},
  {"MO", "Moldavian"},
  {"AZ", "Azerbaijani"},
  {"MN", "Mongolian"},
  {"BA", "Bashkir"},
  {"NA", "Nauru"},
  {"EU", "Basque"},
  {"NE", "Nepali"},
  {"BN", "Bengali"},
  {"NO", "Norwegian"},
  {"DZ", "Bhutani"},
  {"OC", "Occitan"},
  {"BH", "Bihari"},
  {"OR", "Oriya"},
  {"BI", "Bislama"},
  {"OM", "Afan"},
  {"BR", "Breton"},
  {"PA", "Panjabi"},
  {"BG", "Bulgarian"},
  {"PS", "Pashto"},
  {"MY", "Burmese"},
  {"FA", "Persian"},
  {"BE", "Byelorussian"},
  {"PL", "Polish"},
  {"KM", "Cambodian"},
  {"PT", "Portuguese"},
  {"CA", "Catalan"},
  {"QU", "Quechua"},
  {"ZH", "Chinese"},
  {"RM", "Rhaeto-Romance"},
  {"CO", "Corsican"},
  {"RO", "Romanian"},
  {"HR", "Croatian"},
  {"RU", "Russian"},
  {"CS", "Czech"},
  {"SM", "Samoan"},
  {"DA", "Danish"},
  {"SG", "Sangho"},
  {"NL", "Dutch"},
  {"SA", "Sanskrit"},
  {"EN", "English"},
  {"GD", "Gaelic"},
  {"EO", "Esperanto"},
  {"SH", "Serbo-Crotain"},
  {"ET", "Estonian"},
  {"ST", "Sesotho"},
  {"FO", "Faroese"},
  {"SR", "Serbian"},
  {"FJ", "Fiji"},
  {"TN", "Setswana"},
  {"FI", "Finnish"},
  {"SN", "Shona"},
  {"FR", "French"},
  {"SD", "Sindhi"},
  {"FY", "Frisian"},
  {"SI", "Singhalese"},
  {"GL", "Galician"},
  {"SS", "Siswati"},
  {"KA", "Georgian"},
  {"SK", "Slovak"},
  {"DE", "German"},
  {"SL", "Slovenian"},
  {"EL", "Greek"},
  {"SO", "Somali"},
  {"KL", "Greenlandic"},
  {"ES", "Spanish"},
  {"GN", "Guarani"},
  {"SU", "Sundanese"},
  {"GU", "Gujarati"},
  {"SW", "Swahili"},
  {"HA", "Hausa"},
  {"SV", "Swedish"},
  {"IW", "Hebrew"},
  {"TL", "Tagalog"},
  {"HI", "Hindi"},
  {"TG", "Tajik"},
  {"HU", "Hungarian"},
  {"TT", "Tatar"},
  {"IS", "Icelandic"},
  {"TA", "Tamil"},
  {"IN", "Indonesian"},
  {"TE", "Telugu"},
  {"IA", "Interlingua"},
  {"TH", "Thai"},
  {"IE", "Interlingue"},
  {"BO", "Tibetian"},
  {"IK", "Inupiak"},
  {"TI", "Tigrinya"},
  {"GA", "Irish"},
  {"TO", "Tonga"},
  {"IT", "Italian"},
  {"TS", "Tsonga"},
  {"JA", "Japanese"},
  {"TR", "Turkish"},
  {"JW", "Javanese"},
  {"TK", "Turkmen"},
  {"KN", "Kannada"},
  {"TW", "Twi"},
  {"KS", "Kashmiri"},
  {"UK", "Ukranian"},
  {"KK", "Kazakh"},
  {"UR", "Urdu"},
  {"RW", "Kinyarwanda"},
  {"UZ", "Uzbek"},
  {"KY", "Kirghiz"},
  {"VI", "Vietnamese"},
  {"RN", "Kirundi"},
  {"VO", "Volapuk"},
  {"KO", "Korean"},
  {"CY", "Welsh"},
  {"KU", "Kurdish"},
  {"WO", "Wolof"},
  {"LO", "Laothian"},
  {"JI", "Yiddish"},
  {"LA", "Latin"},
  {"YO", "Yoruba"},
  {"LV", "Lettish"},
  {"XH", "Xhosa"},
  {"LN", "Lingala"},
  {"ZU", "Zulu"},
  {NULL, NULL}
};




static const char *
dvd_langcode_to_string(uint16_t langcode)
{
  char str[3];
  int i;

  str[0] = langcode >> 8;
  str[1] = langcode & 0xff;
  str[2] = 0;

  i = 0;
  
  while(langtbl[i].langcode != NULL) {
    if(!strcasecmp(langtbl[i].langcode, str))
      return langtbl[i].displayname;
    i++;
  }
  return "Other";
}



/**
 * Get name of SPU track
 */
static int
dvd_subtitle_get_spu_name(dvd_player_t *dp, char *buf, size_t size, int track)
{
  dvdnav_t *dvdnav;
  char *lc;
  uint16_t langcode;
  int s;

  dvdnav = dp->dp_dvdnav;
  if((s = dvdnav_get_spu_logical_stream(dvdnav, track)) == -1)
    return -1;
  
  if((langcode = dvdnav_spu_stream_to_lang(dvdnav, track)) == 0xffff)
    return -1;

  lc = (char *)dvd_langcode_to_string(langcode);
 
  snprintf(buf, size, "%s", lc);
  return 0;
}


/**
 * Add SPU track options to the form given
 */
static void
add_spu_tracks(dvd_player_t *dp, glw_t *t)
{
  char buf[100];
  int i;

  layout_form_add_option(t, "subtitle_tracks", "Disabled",  DP_SPU_DISABLE);
  layout_form_add_option(t, "subtitle_tracks", "Auto", DP_SPU_FOLLOW_VM);

  for(i = 0; i < 32; i++) {
    if(dvd_subtitle_get_spu_name(dp, buf, sizeof(buf), i))
      continue;
    layout_form_add_option(t, "subtitle_tracks", buf, i);
  }
}



/**
 * Get name of audio track
 */
static int
dvd_audio_get_track_name(dvd_player_t *dp, char *buf, size_t size, int track)
{
  dvdnav_t *dvdnav;
  char *lc;
  uint16_t langcode;
  int s, chans, format;
  const char *chtxt, *fmtxt;

  printf("Adding audio track %d\n", track);

  dvdnav = dp->dp_dvdnav;
  if((s = dvdnav_get_audio_logical_stream(dvdnav, track)) == -1)
    return -1;
  
  if((langcode = dvdnav_audio_stream_to_lang(dvdnav, track)) == 0xffff)
    return -1;

  lc = (char *)dvd_langcode_to_string(langcode);
 
  format = dvdnav_audio_stream_format(dvdnav, track & 0x1f);
  chans = dvdnav_audio_stream_channels(dvdnav, track & 0x1f);

  switch(chans) {
  case 1:
    chtxt = "mono";
    break;
  case 2:
    chtxt = "stereo";
    break;
  case 6:
    chtxt = "5.1";
    break;
  default:
    chtxt = "???";
    break;
  }

  switch(format) {
  case DVDNAV_FORMAT_AC3:
    fmtxt = "ac3";
    break;
  case DVDNAV_FORMAT_MPEGAUDIO:
    fmtxt = "mpeg";
    break;
  case DVDNAV_FORMAT_LPCM:
    fmtxt = "pcm";
    break;
  case DVDNAV_FORMAT_DTS:
    fmtxt = "dts";
    break;
  case DVDNAV_FORMAT_SDDS:
    fmtxt = "sdds";
    break;
  default:
    fmtxt = "???";
    break;
  }

  snprintf(buf, size, "%s (%s - %s)", lc, fmtxt, chtxt);
  printf("\t%s\n", buf);
  return 0;
}




/**
 * Add audio track options to the form given
 */
static void
add_audio_tracks(dvd_player_t *dp, glw_t *t)
{
  char buf[100];
  int i;

  layout_form_add_option(t, "audio_tracks", "Disabled",  -1);
  layout_form_add_option(t, "audio_tracks", "Auto", -2);

  for(i = 0; i < 8; i++) {
    if(dvd_audio_get_track_name(dp, buf, sizeof(buf), i))
      continue;
    layout_form_add_option(t, "audio_tracks", buf, i);
  }
}


/**
 * DVD control menu
 */
static int
dvd_player_menu(dvd_player_t *dp, ic_t *ic, media_pipe_t *mp)
{
  glw_t *t, *m;
  appi_t *ai = dp->dp_ai;
  struct layout_form_entry_list lfelist;
  inputevent_t ie;
  int r = 1, run = 1;

  dp->dp_force_status_display = 1;

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, dp->dp_menu_playfield,
		 GLW_ATTRIB_FILENAME, "dvdplayback/menu",
		 NULL);

  TAILQ_INIT(&lfelist);

  /**
   * Video tab
   */
  video_menu_add_tab(m, &ai->ai_gfs, ic, &dp->dp_vdc, "dvdplayback/video");

  /**
   * Audio tab
   */

  TAILQ_INIT(&lfelist);

  t = layout_form_add_tab(m,
			  "menu",           "dvdplayback/audio-icon",
			  "menu_container", "dvdplayback/audio-tab");
  
  add_audio_tracks(dp, m);

  LFE_ADD_OPTION(&lfelist, "audio_tracks", &dp->dp_audio_track);
  layout_form_initialize(&lfelist, m, &ai->ai_gfs, ic, 1);

 /**
   * Subtitles tab
   */

  TAILQ_INIT(&lfelist);

  t = layout_form_add_tab(m,
			  "menu",           "dvdplayback/subtitles-icon",
			  "menu_container", "dvdplayback/subtitles-tab");
  
  add_spu_tracks(dp, m);

  LFE_ADD_OPTION(&lfelist, "subtitle_tracks", &dp->dp_spu_track);
  layout_form_initialize(&lfelist, m, &ai->ai_gfs, ic, 1);


  /**
   * Tab menu
   */
  LFE_ADD(&lfelist, "menu");
  layout_form_initialize(&lfelist, m, &ai->ai_gfs, ic, 1);

  while(run) {
    input_getevent(ic, 1, &ie, NULL);

    switch(ie.type) {
    default:
      break;

     case INPUT_KEY:

      switch(ie.u.key) {
      default:
	break;


      case INPUT_KEY_LEFT:
      case INPUT_KEY_RIGHT:
      case INPUT_KEY_UP: 
      case INPUT_KEY_DOWN:
	continue;

      case INPUT_KEY_STOP:
      case INPUT_KEY_CLOSE:
      case INPUT_KEY_EJECT:
	r = 0;
	run = 0;
	continue;

      case INPUT_KEY_MENU:
      case INPUT_KEY_BACK:
	run = 0;
	continue;
      }
    }
    input_postevent(&dp->dp_ic, &ie);
  }

  glw_detach(m);
  dp->dp_force_status_display = 0;
  return r;
}

