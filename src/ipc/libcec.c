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

#include <libcec/cecc.h>

#include "settings.h"
#include "htsmsg/htsmsg_store.h"
#include "main.h"
#include "event.h"

static int longpress_select;
static libcec_configuration cec_config;
static libcec_connection_t conn;

static int
log_message(void *lib, const cec_log_message message)
{
  int level;
  switch (message.level) {
  case CEC_LOG_ERROR:
    level = TRACE_ERROR;
    break;

  case CEC_LOG_WARNING:
  case CEC_LOG_NOTICE:
    level = TRACE_INFO;
    break;

  default:
    if(!gconf.enable_cec_debug)
      return 1;
    level = TRACE_DEBUG;
    break;
  }
  TRACE(level, "CEC", "%s", message.message);
  return 1;
}


#define AVEC(x...) (const action_type_t []){x, ACTION_NONE}
const static action_type_t *btn_to_action[256] = {
  [CEC_USER_CONTROL_CODE_SELECT]      = AVEC(ACTION_ACTIVATE),
  [CEC_USER_CONTROL_CODE_LEFT]        = AVEC(ACTION_LEFT),
  [CEC_USER_CONTROL_CODE_UP]          = AVEC(ACTION_UP),
  [CEC_USER_CONTROL_CODE_RIGHT]       = AVEC(ACTION_RIGHT),
  [CEC_USER_CONTROL_CODE_DOWN]        = AVEC(ACTION_DOWN),
  [CEC_USER_CONTROL_CODE_EXIT]        = AVEC(ACTION_NAV_BACK),
  [CEC_USER_CONTROL_CODE_DOT]         = AVEC(ACTION_ITEMMENU),
  [CEC_USER_CONTROL_CODE_ROOT_MENU]   = AVEC(ACTION_MENU),

  [CEC_USER_CONTROL_CODE_PLAY]        = AVEC(ACTION_PLAYPAUSE),
  [CEC_USER_CONTROL_CODE_STOP]        = AVEC(ACTION_STOP),
  [CEC_USER_CONTROL_CODE_PAUSE]       = AVEC(ACTION_PAUSE),
  [CEC_USER_CONTROL_CODE_RECORD]      = AVEC(ACTION_RECORD),
  [CEC_USER_CONTROL_CODE_REWIND]      = AVEC(ACTION_SEEK_BACKWARD),
  [CEC_USER_CONTROL_CODE_FAST_FORWARD]= AVEC(ACTION_SEEK_FORWARD),
  [CEC_USER_CONTROL_CODE_FORWARD]     = AVEC(ACTION_SKIP_FORWARD),
  [CEC_USER_CONTROL_CODE_BACKWARD]    = AVEC(ACTION_SKIP_BACKWARD),

  [CEC_USER_CONTROL_CODE_CHANNEL_UP]  = AVEC(ACTION_NEXT_CHANNEL),
  [CEC_USER_CONTROL_CODE_CHANNEL_DOWN]= AVEC(ACTION_PREV_CHANNEL),

  [CEC_USER_CONTROL_CODE_F1_BLUE]     = AVEC(ACTION_SYSINFO),
  [CEC_USER_CONTROL_CODE_F4_YELLOW]   = AVEC(ACTION_SHOW_MEDIA_STATS),
};

static int
keypress(void *aux, const cec_keypress kp)
{
  event_t *e = NULL;
  if(gconf.enable_cec_debug)
    TRACE(TRACE_DEBUG, "CEC", "Got keypress code=0x%x duration=0x%x",
          kp.keycode, kp.duration);

  if(longpress_select) {
    if(kp.keycode == CEC_USER_CONTROL_CODE_SELECT) {
      if(kp.duration == 0)
        return 0;

      if(kp.duration < 500)
        e = event_create_action(ACTION_ACTIVATE);
      else
        e = event_create_action(ACTION_ITEMMENU);
    }
  }

  if(e == NULL) {
    const action_type_t *avec = NULL;
    if(kp.duration == 0 || kp.keycode == cec_config.comboKey) {
      avec = btn_to_action[kp.keycode];
    }

    if(avec != NULL) {
      int i = 0;
      while(avec[i] != 0)
        i++;
      e = event_create_action_multi(avec, i);
    }
  }

  if(e != NULL) {
    e->e_flags |= EVENT_KEYPRESS;
    event_to_ui(e);
  }
  return 1;
}


static void
source_activated(void *aux, const cec_logical_address la, const uint8_t on)
{
  TRACE(TRACE_INFO, "CEC", "Logical address %d is %s",
        la, on ? "active" : "inactive");
}


static int
handle_cec_command(void *aux, const cec_command cmd)
{
  switch(cmd.opcode) {
  case CEC_OPCODE_STANDBY:
    if(cmd.initiator == CECDEVICE_TV) {
      TRACE(TRACE_INFO, "CEC", "TV STANDBY");
    }
    break;
  default:
    break;
  }
  return 1;
}


static ICECCallbacks g_callbacks = {
  .CBCecLogMessage = log_message,
  .CBCecKeyPress   = keypress,
  .CBCecSourceActivated = source_activated,
  .CBCecCommand = handle_cec_command,
};



/**
 *
 */
static void
set_activate_source(void *opaque, int value)
{
  cec_config.bActivateSource = value;
  libcec_set_configuration(conn, &cec_config);
}


/**
 *
 */
static void
set_stop_combo_mode(void *opaque, int value)
{
  cec_config.comboKey = value ? CEC_USER_CONTROL_CODE_STOP : 0xfe;
  libcec_set_configuration(conn, &cec_config);
}


/**
 *
 */
static void
set_longpress_select(void *opaque, int value)
{
  longpress_select = value;
}


/**
 *
 */
static void *
libcec_init_thread(void *aux)
{
  htsmsg_t *s = htsmsg_store_load("cec") ?: htsmsg_create_map();

  prop_t *set;

  set =
    settings_add_dir(NULL, _p("TV Control"),
		     "display", NULL,
		     _p("Configure communications with your TV"),
		     "settings:cec");



  libcec_clear_configuration(&cec_config);
  cec_config.callbacks = &g_callbacks;
  snprintf(cec_config.strDeviceName, sizeof(cec_config.strDeviceName),
           "%s", APPNAMEUSER);


  cec_config.deviceTypes.types[0] = CEC_DEVICE_TYPE_RECORDING_DEVICE;

  conn = libcec_initialise(&cec_config);
  if(conn == NULL) {
    TRACE(TRACE_ERROR, "CEC", "Unable to init libcec");
    return NULL;
  }

  libcec_init_video_standalone(conn);

  cec_adapter ca;
  int num_adapters = libcec_find_adapters(conn, &ca, 1, NULL);
  if(num_adapters < 1) {
    libcec_destroy(conn);
    TRACE(TRACE_ERROR, "CEC", "No adapters found");
    return NULL;
  }
  TRACE(TRACE_DEBUG, "CEC", "Using adapter %s on %s",
        ca.comm, ca.path);


  setting_create(SETTING_BOOL, set,
		 SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE(_p("Switch TV input source")),
                 SETTING_VALUE(1),
		 SETTING_CALLBACK(set_activate_source, NULL),
		 SETTING_HTSMSG("controlinput", s, "cec"),
		 NULL);

  setting_create(SETTING_BOOL, set,
		 SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE(_p("Use STOP key for combo input")),
                 SETTING_VALUE(1),
		 SETTING_CALLBACK(set_stop_combo_mode, NULL),
		 SETTING_HTSMSG("stopcombo", s, "cec"),
		 NULL);

  setting_create(SETTING_BOOL, set,
		 SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE(_p("Longpress SELECT for item menu")),
                 SETTING_VALUE(1),
		 SETTING_CALLBACK(set_longpress_select, NULL),
		 SETTING_HTSMSG("longpress_select", s, "cec"),
		 NULL);


#if 0
  setting_create(SETTING_BOOL, set,
		 SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE_CSTR("Shutdown UI when TV is off"),
		 SETTING_WRITE_BOOL(&auto_ui_shutdown),
		 SETTING_HTSMSG("auto_shutdown", s, "cec"),
		 NULL);
#endif

  if(!libcec_open(conn, ca.comm, 5000)) {
    TRACE(TRACE_ERROR, "CEC", "Unable to open connection to %s",
          ca.comm);
    libcec_destroy(conn);
    return NULL;
  }
  return NULL;
}


static void
libcec_init(void)
{
  hts_thread_create_detached("cec", libcec_init_thread, NULL,
                             THREAD_PRIO_BGTASK);
}


static void
libcec_fini(void)
{

}

INITME(INIT_GROUP_IPC, libcec_init, libcec_fini);
