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

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>

#include <netinet/in.h>

#include "showtime.h"
#include "dvd.h"
#include "input.h"
#include "gl/gl_video.h"
#include "menu.h"
#include "miw.h"
#include "audio/audio_sched.h"

static glw_t *dvd_menu_spu_setup(glw_t *p, dvd_player_t *dp);
static glw_t *dvd_menu_audio_setup(glw_t *p, dvd_player_t *dp);

#define DVD_AUDIO_STREAMS 8
#define DVD_AUDIO_MASK      (DVD_AUDIO_STREAMS - 1)

#define DVD_SPU_STREAMS   32
#define DVD_SPU_MASK      (DVD_SPU_STREAMS - 1)




glw_t *
dvd_create_miw(dvd_player_t *dp, media_pipe_t *mp, const char *title)
{
  glw_t *y, *x, *c;

  c = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, c,
		 NULL);

  miw_playstatus_create(x, mp);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_WEIGHT, 20.0f,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_CAPTION, title,
	     NULL);

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_WEIGHT, 3.0f,
		 NULL);
 

  dp->dp_widget_title   = glw_create(GLW_TEXT_BITMAP,
				     GLW_ATTRIB_PARENT, y,
				     GLW_ATTRIB_CAPTION, "",
				     NULL);
 
  dp->dp_widget_chapter = glw_create(GLW_TEXT_BITMAP,
				     GLW_ATTRIB_PARENT, y,
				     GLW_ATTRIB_CAPTION, "",
				     NULL);

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_WEIGHT, 3.0f,
		 NULL);

  dp->dp_widget_time    = glw_create(GLW_TEXT_BITMAP,
				     GLW_ATTRIB_PARENT, y,
				     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
				     GLW_ATTRIB_CAPTION, "",
				     NULL);

  return c;
}





static int
dvd_filter_audio(void *aux, uint32_t sc)
{
  dvd_player_t *dp = aux;

  if(dp->dp_audio[dp->dp_audio_mode] == 0xffffffff)
    return 0; /* no sound */

  return (sc & DVD_AUDIO_MASK) == 
    (dp->dp_audio[dp->dp_audio_mode] & DVD_AUDIO_MASK);
}

static int
dvd_filter_spu(void *aux, uint32_t sc)
{
  dvd_player_t *dp = aux;

  if(!dp->dp_inmenu) {

    if(dp->dp_spu[dp->dp_spu_mode] == 0xffffffff)
      return 0; /* no spu */
  }

  return (sc & DVD_SPU_MASK) == (dp->dp_spu[dp->dp_spu_mode] & DVD_SPU_MASK);
}






void
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
  dp->dp_audio[DP_AUDIO_FOLLOW_VM] = dat->physical;
}


static void
dvd_spu_stream_change(dvd_player_t *dp, 
		      dvdnav_spu_stream_change_event_t *dat)
{
  dp->dp_spu[DP_AUDIO_FOLLOW_VM] = dat->physical_wide;
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
  mb->mb_rate = dp->dp_pp.pp_aspect_override;
  mb->mb_data_type = MB_VIDEO;
  mb->mb_duration = 1000000LL * av_q2d(cw->codec_ctx->time_base);

  wrap_unlock_codec(cw);
  mb_enqueue(mp, &mp->mp_video, mb);
}





static void
dvd_flush(dvd_player_t *dp)
{
  media_pipe_t *mp = &dp->dp_ai->ai_mp;
  mp_flush(mp, 1);
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

static int
dvd_ctrl_input(dvd_player_t *dp, int wait);




static void
dvd_update_info(dvd_player_t *dp)
{
  int title, titles, part, parts, s, m, h;
  char tmp[100];
  int64_t t;

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


int
dvd_main(appi_t *ai, const char *devname, int isdrive, glw_t *parent)
{
  int len, event, result, kickctd, i;
  uint8_t mem[DVD_VIDEO_LB_LEN], *block;
  pci_t *pci;
  dvdnav_highlight_event_t *hevent;
  dvd_player_t *dp = calloc(1, sizeof(dvd_player_t));
  int rcode = 0;
  const char *rawtitle;
  char *title;
  void *data;
  formatwrap_t *fw;
  media_pipe_t *mp;
  gvp_conf_t gc;
  glw_t *vmenu, *amenu, *smenu, *w, *gvp, *gvp0;

  w = glw_create(GLW_XFADER,
		 GLW_ATTRIB_PARENT, parent,
		 NULL);

  dp->dp_ai = ai;

  miw_loading(w, "...reading disc");


  if(dvdnav_open(&dp->dp_dvdnav, devname) != DVDNAV_STATUS_OK) {
    glw_destroy(w);
    return 1;
  }

  dvdnav_set_readahead_flag(dp->dp_dvdnav, 1);
  dvdnav_set_PGC_positioning_flag(dp->dp_dvdnav, 1);
  dvdnav_get_title_string(dp->dp_dvdnav, &rawtitle);

  title = make_nice_title(rawtitle);
  if(*title == 0) {
    title = strrchr(devname, '/');
    if(title != NULL)
      title = make_nice_title(title + 1);
  }

  smenu = dvd_menu_spu_setup(appi_menu_top(ai), dp);
  amenu = dvd_menu_audio_setup(appi_menu_top(ai), dp);
  vmenu = gvp_menu_setup(appi_menu_top(ai), &gc);

  
  mp = &ai->ai_mp;

  gvp_conf_init(&gc);
  gc.gc_postproc_type = GVP_PP_NONE;

  gvp0 = gvp = gvp_create(NULL, &ai->ai_mp, &gc, 0);

  gvp_set_dvd(gvp, dp);


  dp->dp_spu[DP_SPU_DISABLE] = 0xffffffff;
  dp->dp_spu[DP_AUDIO_DISABLE] = 0xffffffff;

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

  kickctd = 0;

  mp->mp_info_widget = dvd_create_miw(dp, mp, title);


  printf("entering dvd mainloop\n");


  mp_set_playstatus(mp, MP_PLAY);

  while(!rcode) {

    if(mp_is_paused(mp)) {
      rcode = dvd_ctrl_input(dp, 255);
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
      rcode = 1;
      break;
    }
    switch(event) {
    case DVDNAV_BLOCK_OK:
      if(ai->ai_req_fullscreen != AI_FS_BLANK) {
	ai->ai_req_fullscreen = AI_FS_BLANK;


	while(ai->ai_got_fullscreen == 0) {
	  inputevent_t ie;
	  if(input_getevent(&ai->ai_ic, 0, &ie, NULL)) {
	    usleep(100000);
	    continue;
	  }
	  goto out;
	}

	mp_auto_display(mp);
      }
      kickctd = 3;

      if(gvp0 != NULL) {
	glw_set(gvp0, GLW_ATTRIB_PARENT, w, NULL);
	gvp0 = NULL;
      }

      dvd_ps_block(dp, block, len);
      audio_sched_mp_activate(mp);
      break;

    case DVDNAV_NOP:
      break;

    case DVDNAV_STILL_FRAME:
      dvd_gen_frame_kick(dp);
      rcode = dvd_ctrl_input(dp, *(int *)block);
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

      dp->dp_spu_mode = DP_SPU_FOLLOW_VM;
      dp->dp_audio_mode = DP_AUDIO_FOLLOW_VM;
      dp->dp_pp.pp_audio.ps_force_reset = 1;
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

    rcode = dvd_ctrl_input(dp, 0);

  }
 out:

  glw_destroy(vmenu);
  glw_destroy(amenu);
  glw_destroy(smenu);

  ai->ai_req_fullscreen = AI_FS_NONE;

  pes_deinit(&dp->dp_pp);
  dvdnav_close(dp->dp_dvdnav);

  wrap_format_wait(fw);

  free(dp);
  free(title);

  audio_sched_mp_deactivate(mp, 1);
  glw_destroy(w);

  glw_destroy(mp->mp_info_widget);
  mp->mp_info_widget = NULL;
  return rcode;
}


static int
dvd_ctrl_input(dvd_player_t *dp, int wait)
{
  struct timespec ts;
  int key, r;
  pci_t *pci;
  appi_t *ai = dp->dp_ai;
  ic_t *ic = &ai->ai_ic;
  inputevent_t ie;
  media_pipe_t *mp = &ai->ai_mp;

  switch(wait) {
  case 0: /* no timeout, no wait */
    r = input_getevent(ic, 0, &ie, NULL);
    break;

  case 1 ... 254:  /* wait n seconds and skip if timeout */
    ts.tv_sec = time(NULL) + wait;
    ts.tv_nsec = 0;
    if(input_getevent(ic, 1, &ie, &ts)) {
      dvdnav_still_skip(dp->dp_dvdnav);
      return 0;
    }
    break;

  case 255:  /* wait forever */
    r = input_getevent(ic, 1, &ie, NULL);
    break;
  }

  key = ie.type == INPUT_KEY ? ie.u.key : 0;

  if(key == 0)
    return 0;

  pci = &dp->dp_pci;

  switch(key) {
  case INPUT_KEY_STOP:
  case INPUT_KEY_CLEAR:
  case INPUT_KEY_EJECT:
    return key;

  case INPUT_KEY_PLAYPAUSE:
  case INPUT_KEY_PLAY:
  case INPUT_KEY_PAUSE:
    mp_playpause(mp, key);
    return 0;


  case INPUT_KEY_DVD_AUDIO_MENU:
    mp_playpause(mp, MP_PLAY);
    dvdnav_menu_call(dp->dp_dvdnav, DVD_MENU_Audio);
    return 0;

  case INPUT_KEY_DVD_SPU_MENU:
    mp_playpause(mp, MP_PLAY);
    dp->dp_spu_mode = DP_SPU_FOLLOW_VM;
    dvdnav_menu_call(dp->dp_dvdnav, DVD_MENU_Subpicture);
    return 0;

  case INPUT_KEY_DVD_SPU_OFF:
    dp->dp_spu_mode = DP_SPU_DISABLE;
    return 0;
  }

  if(!dp->dp_inmenu) {
    switch(key) {

    case INPUT_KEY_SEEK_BACKWARD:
      mp_auto_display(mp);
      dvdnav_sector_search(dp->dp_dvdnav, -10000, SEEK_CUR);
      return 0;

    case INPUT_KEY_SEEK_FORWARD:
      mp_auto_display(mp);
      dvdnav_sector_search(dp->dp_dvdnav, 10000, SEEK_CUR);
      return 0;

    }
  }

  if(mp_is_paused(mp))
    return 0;  /* no other actions allows if paused */

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
    mp_auto_display(mp);
    dvdnav_next_pg_search(dp->dp_dvdnav);
    break;
  case INPUT_KEY_PREV:
    dvd_flush(dp);
    mp_auto_display(mp);
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
  
  return 0;
  
}


/*****************************************************************************
 *
 * DVD language codes
 *
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



/****************************************************************************
 *
 *   DVD Menu SPU functions
 *
 */

int
dvd_subtitle_get_spu_name(dvd_player_t *dp, char *buf, int track, int *phys,
			  int *iscurp)
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
 
  *phys = s;
  sprintf(buf, "%s", lc);
  *iscurp = dvd_filter_spu(dp, s);
  return 0;
}




void 
dvd_spu_set_track(dvd_player_t *dp, int track)
{
  if(track == -1) {
    dp->dp_spu_mode = DP_SPU_DISABLE;
  } else {
    dp->dp_spu_mode = DP_SPU_OVERRIDE;
    dp->dp_spu[DP_SPU_OVERRIDE] = track;
  }
}




static int
dvd_menu_spu(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  dvd_player_t *dp = opaque;
  char str[40];
  glw_t *c;
  int u32, iscur;

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    
    if(walltime == w->glw_holdtime)
      return 0;
    w->glw_holdtime = walltime;

    if(dvd_subtitle_get_spu_name(dp, str, glw_get_u32(w), &u32, &iscur)) {
      glw_set(w, GLW_ATTRIB_HIDDEN, 1, NULL);
    } else {
      glw_set(w, GLW_ATTRIB_HIDDEN, 0, NULL);

      if((c = glw_find_by_class(w, GLW_TEXT_BITMAP)) != NULL)
	glw_set(c, GLW_ATTRIB_CAPTION, str, NULL);
      
      if((c = glw_find_by_class(w, GLW_BITMAP)) != NULL)
	c->glw_alpha = iscur ? 1.0 : 0.0;
    }
    return 1;

  case GLW_SIGNAL_CLICK:
    dvd_spu_set_track(dp, glw_get_u32(w));
    return 1;

  default:
    return 0;
  }
}



static int
dvd_menu_spu_off(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  dvd_player_t *dp = opaque;
  glw_t *c;

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if(dp == NULL)
      return 1;
    
    if((c = glw_find_by_class(w, GLW_BITMAP)) != NULL)
      c->glw_alpha = dp->dp_spu_mode == DP_SPU_DISABLE ? 1.0 : 0.0;
    return 0;

  case GLW_SIGNAL_CLICK:
    dvd_spu_set_track(dp, -1);
    return 1;

  default:
    return 0;
  }
}






static glw_t *
dvd_menu_spu_setup(glw_t *p, dvd_player_t *dp)
{
  glw_t *v, *w;
  int i;

  v = menu_create_submenu(p, "icon://subtitles.png", "Subtitles", 1);

  for(i = 0; i < 32; i++)
    w = menu_create_item(v, "icon://menu-current.png", "",
			 dvd_menu_spu, dp, i, 0);
  
  menu_create_item(v, "icon://menu-current.png", "(off)",
		   dvd_menu_spu_off, dp, -1, 0);
  return v;
}





/****************************************************************************
 *
 *   DVD Menu audio functions
 *
 */

int
dvd_audio_get_track_name(dvd_player_t *dp, char *buf, int track, int *phys,
			 int *iscurp)
{
  dvdnav_t *dvdnav;
  char *lc;
  uint16_t langcode;
  int s, chans, format;
  const char *chtxt, *fmtxt;

  *iscurp = 0;

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

  *phys = s;
  sprintf(buf, "%s (%s - %s)", lc, fmtxt, chtxt);

  *iscurp = dvd_filter_audio(dp, track);
  return 0;
}


void 
dvd_audio_set_track(dvd_player_t *dp, int track)
{
  if(track == -1) {
    dp->dp_audio_mode = DP_AUDIO_DISABLE;
  } else {
    dp->dp_audio_mode = DP_AUDIO_OVERRIDE;
    dp->dp_audio[DP_AUDIO_OVERRIDE] = track;
  }
}




static int
dvd_menu_atrack(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  char str[40];
  dvd_player_t *dp = opaque;
  int u32, iscur;
  glw_t *c;

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if(walltime == w->glw_holdtime)
      return 0;
    w->glw_holdtime = walltime;

    if(dvd_audio_get_track_name(dp, str, glw_get_u32(w), &u32, &iscur)) {
      glw_set(w, GLW_ATTRIB_HIDDEN, 1, NULL);
    } else {
      glw_set(w, GLW_ATTRIB_HIDDEN, 0, NULL);

      if((c = glw_find_by_class(w, GLW_TEXT_BITMAP)) != NULL)
	glw_set(c, GLW_ATTRIB_CAPTION, str, NULL);
      
      if((c = glw_find_by_class(w, GLW_BITMAP)) != NULL)
	c->glw_alpha = iscur ? 1.0 : 0.0;
    }
    return 1;

  case GLW_SIGNAL_CLICK:
    dvd_audio_set_track(dp, glw_get_u32(w));
    return 1;

  default:
    return 0;
  }
}



static int
dvd_menu_atrack_off(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  dvd_player_t *dp = opaque;
  glw_t *c;

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if(dp == NULL)
      return 1;
    
    if((c = glw_find_by_class(w, GLW_BITMAP)) != NULL)
      c->glw_alpha = dp->dp_audio_mode == DP_AUDIO_DISABLE ? 1.0 : 0.0;
    return 0;

  case GLW_SIGNAL_CLICK:
    dvd_audio_set_track(dp, -1);
    return 1;

  default:
    return 0;
  }
}





static glw_t *
dvd_menu_audio_setup(glw_t *p, dvd_player_t *dp)
{
  glw_t *v, *w;
  int i;

  v = menu_create_submenu(p, "icon://audio.png", "Audio tracks", 1);

  for(i = 0; i < 8; i++)
    w = menu_create_item(v, "icon://menu-current.png", "",
			 dvd_menu_atrack, dp, i, 0);
  
  menu_create_item(v, "icon://menu-current.png", "(off)",
		   dvd_menu_atrack_off, dp, -1, 0);
  return v;
}

