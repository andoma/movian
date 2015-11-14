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

#include "event.h"
#include "misc/str.h"
#include "misc/minmax.h"
#include "htsmsg/htsmsg_store.h"
#include "settings.h"

#include <bcm_host.h>
#include <OMX_Core.h>
#include <interface/vmcs_host/vc_cecservice.h>
#include <interface/vchiq_arm/vchiq_if.h>

#include "rpi.h"

static int fixed_la;
static int control_input;

int auto_ui_shutdown;

#define CEC_VENDOR_ID_LG 0xe091

static const char *cec_cmd_to_str[] = {
  [0x00] = "FeatureAbort",
  [0x04] = "ImageViewOn",
  [0x05] = "TunerStepIncrement",
  [0x06] = "TunerStepDecrement",
  [0x07] = "TunerDeviceStatus",
  [0x08] = "GiveTunerDeviceStatus",
  [0x09] = "RecordOn",
  [0x0A] = "RecordStatus",
  [0x0B] = "RecordOff",
  [0x0D] = "TextViewOn",
  [0x0F] = "RecordTVScreen",
  [0x1A] = "GiveDeckStatus",
  [0x1B] = "DeckStatus",
  [0x32] = "SetMenuLanguage",
  [0x33] = "ClearAnalogTimer",
  [0x34] = "SetAnalogTimer",
  [0x35] = "TimerStatus",
  [0x36] = "Standby",
  [0x41] = "Play",
  [0x42] = "DeckControl",
  [0x43] = "TimerClearedStatus",
  [0x44] = "UserControlPressed",
  [0x45] = "UserControlReleased",
  [0x46] = "GiveOSDName",
  [0x47] = "SetOSDName",
  [0x64] = "SetOSDString",
  [0x67] = "SetTimerProgramTitle",
  [0x70] = "SystemAudioModeRequest",
  [0x71] = "GiveAudioStatus",
  [0x72] = "SetSystemAudioMode",
  [0x7A] = "ReportAudioStatus",
  [0x7D] = "GiveSystemAudioModeStatus",
  [0x7E] = "SystemAudioModeStatus",
  [0x80] = "RoutingChange",
  [0x81] = "RoutingInformation",
  [0x82] = "ActiveSource",
  [0x83] = "GivePhysicalAddress",
  [0x84] = "ReportPhysicalAddress",
  [0x85] = "RequestActiveSource",
  [0x86] = "SetStreamPath",
  [0x87] = "DeviceVendorID",
  [0x89] = "VendorCommand",
  [0x8A] = "VendorRemoteButtonDown",
  [0x8B] = "VendorRemoteButtonUp",
  [0x8C] = "GiveDeviceVendorID",
  [0x8D] = "MenuRequest",
  [0x8E] = "MenuStatus",
  [0x8F] = "GiveDevicePowerStatus",
  [0x90] = "ReportPowerStatus",
  [0x91] = "GetMenuLanguage",
  [0x92] = "SelectAnalogService",
  [0x93] = "SelectDigitalService",
  [0x97] = "SetDigitalTimer",
  [0x99] = "ClearDigitalTimer",
  [0x9A] = "SetAudioRate",
  [0x9D] = "InactiveSource",
  [0x9E] = "CECVersion",
  [0x9F] = "GetCECVersion",
  [0xA0] = "VendorCommandWithID",
  [0xA1] = "ClearExternalTimer",
  [0xA2] = "SetExternalTimer",
  [0xA3] = "ReportShortAudioDescriptor",
  [0xA4] = "RequestShortAudioDescriptor",
  [0xC0] = "InitARC",
  [0xC1] = "ReportARCInited",
  [0xC2] = "ReportARCTerminated",
  [0xC3] = "RequestARCInit",
  [0xC4] = "RequestARCTermination",
  [0xC5] = "TerminateARC",
  [0xF8] = "CDC",
  [0xFF] = "Abort",
};


#define AVEC(x...) (const action_type_t []){x, ACTION_NONE}
const static action_type_t *btn_to_action[256] = {
  [CEC_User_Control_Select]      = AVEC(ACTION_ACTIVATE),
  [CEC_User_Control_Left]        = AVEC(ACTION_LEFT),
  [CEC_User_Control_Up]          = AVEC(ACTION_UP),
  [CEC_User_Control_Right]       = AVEC(ACTION_RIGHT),
  [CEC_User_Control_Down]        = AVEC(ACTION_DOWN),
  [CEC_User_Control_Exit]        = AVEC(ACTION_NAV_BACK),

  [CEC_User_Control_Pause]       = AVEC(ACTION_PLAYPAUSE),
  [CEC_User_Control_Play]        = AVEC(ACTION_PLAY),
  [CEC_User_Control_Stop]        = AVEC(ACTION_STOP),

  [CEC_User_Control_Rewind]      = AVEC(ACTION_SEEK_BACKWARD),
  [CEC_User_Control_FastForward] = AVEC(ACTION_SEEK_FORWARD),

  [CEC_User_Control_Backward]    = AVEC(ACTION_SKIP_BACKWARD),
  [CEC_User_Control_Forward]     = AVEC(ACTION_SKIP_FORWARD),

  [CEC_User_Control_Record]      = AVEC(ACTION_RECORD),

  [CEC_User_Control_RootMenu]    = AVEC(ACTION_HOME),

  [CEC_User_Control_SetupMenu]   = AVEC(ACTION_MENU),
  [CEC_User_Control_ContentsMenu]= AVEC(ACTION_ITEMMENU, ACTION_SHOW_MEDIA_STATS),

  [CEC_User_Control_F1Blue]      = AVEC(ACTION_ENABLE_SCREENSAVER),
  [CEC_User_Control_F2Red]       = AVEC(ACTION_MENU),
  [CEC_User_Control_F3Green]     = AVEC(ACTION_SHOW_MEDIA_STATS),
  [CEC_User_Control_F4Yellow]    = AVEC(ACTION_ITEMMENU),
};


const static action_type_t *stop_meta_actions[256] = {
  [CEC_User_Control_Select]      = AVEC(ACTION_LOGWINDOW),

  [CEC_User_Control_Pause]       = AVEC(ACTION_NAV_BACK),
  [CEC_User_Control_Stop]        = AVEC(ACTION_STOP),
  [CEC_User_Control_Play]        = AVEC(ACTION_SYSINFO),

  [CEC_User_Control_Rewind]      = AVEC(ACTION_ITEMMENU),
  [CEC_User_Control_FastForward] = AVEC(ACTION_MENU),

  [CEC_User_Control_Backward]    = AVEC(ACTION_ITEMMENU),
  [CEC_User_Control_Forward]     = AVEC(ACTION_MENU),

  [CEC_User_Control_Left]        = AVEC(ACTION_MOVE_LEFT),
  [CEC_User_Control_Up]          = AVEC(ACTION_MOVE_UP),
  [CEC_User_Control_Right]       = AVEC(ACTION_MOVE_RIGHT),
  [CEC_User_Control_Down]        = AVEC(ACTION_MOVE_DOWN),
};


const static action_type_t *play_meta_actions[256] = {
  [CEC_User_Control_Select]      = AVEC(ACTION_PLAYQUEUE),

  [CEC_User_Control_Pause]       = AVEC(ACTION_VOLUME_MUTE_TOGGLE),
  [CEC_User_Control_Up]          = AVEC(ACTION_VOLUME_UP),
  [CEC_User_Control_Down]        = AVEC(ACTION_VOLUME_DOWN),

  [CEC_User_Control_Stop]        = AVEC(ACTION_SWITCH_VIEW),

  [CEC_User_Control_Play]        = AVEC(ACTION_SHOW_MEDIA_STATS),

  [CEC_User_Control_Left]        = AVEC(ACTION_SEEK_BACKWARD),
  [CEC_User_Control_Right]       = AVEC(ACTION_SEEK_FORWARD),
};


#define CEC_DEBUG(fmt...) do {			\
  if(gconf.enable_cec_debug)			\
    TRACE(TRACE_DEBUG, "CEC", fmt);		\
  } while(0)


static int stop_is_meta_key;
static int play_is_meta_key;

static int64_t stop_key_timeout;
static int64_t play_key_timeout;

/**
 *
 */
static void
cec_emit_key_down(int code)
{
  int64_t now = arch_get_ts();

  if(stop_key_timeout < now && play_key_timeout < now) {
    if(code == CEC_User_Control_Stop && stop_is_meta_key) {
      CEC_DEBUG("Stop key intercepted as modifier");
      stop_key_timeout = arch_get_ts() + 1000000;
      return;
    }

    if(code == CEC_User_Control_Play && play_is_meta_key) {
      CEC_DEBUG("Play key intercepted as modifier");
      play_key_timeout = arch_get_ts() + 1000000;
      return;
    }
  }

  const action_type_t *avec;
  if(stop_key_timeout > now) {
    avec = stop_meta_actions[code];
    CEC_DEBUG("Selecting from stop key mapping: code:%d -> avec=%p",
	      code, avec);
  } else if(play_key_timeout > now) {
    avec = play_meta_actions[code];
    CEC_DEBUG("Selecting from play key mapping: code:%d -> avec=%p",
	      code, avec);
  } else {
    avec = btn_to_action[code];
  }

  if(avec != NULL) {
    int i = 0;
    while(avec[i] != 0)
      i++;
    event_t *e = event_create_action_multi(avec, i);
    e->e_flags |= EVENT_KEYPRESS;
    event_to_ui(e);
  } else {
    CEC_DEBUG("Unmapped code 0x%02x", code);
  }
}



static void
cec_send_msg(int follower, uint8_t *response, int len, int is_reply)
{
  int opcode = response[0];
  char hexbuf[64];

  bin2hex(hexbuf, sizeof(hexbuf), response + 1, len - 1);

  CEC_DEBUG("TX: %-27s [0x%02x]          (to:0x%x) %s\n",
	    cec_cmd_to_str[opcode], opcode,
	    follower, hexbuf);

  vc_cec_send_message(follower, response, len, is_reply);
}




static uint32_t myVendorId = CEC_VENDOR_ID_BROADCOM;
static uint32_t tv_vendor_id;
static uint16_t physical_address;
static CEC_AllDevices_T logical_address;
static uint16_t active_physical_address;

#if 0
static void
send_image_view_on(void)
{
  uint8_t response[1];
  response[0] = CEC_Opcode_ImageViewOn;
  cec_send_msg(0x0, response, 1, VC_FALSE);
}
#endif


static void
send_active_source(int is_reply)
{
  CEC_DEBUG("Sending active source. Physical address: 0x%x",
	    physical_address);
  vc_cec_send_ActiveSource(physical_address, is_reply);
  cec_we_are_not_active = 0;
  CEC_DEBUG("We are active");
  active_physical_address = physical_address;
}


static void
SetStreamPath(const VC_CEC_MESSAGE_T *msg)
{
  uint16_t requestedAddress;

  requestedAddress = (msg->payload[1] << 8) + msg->payload[2];
  if (requestedAddress != physical_address) {
    CEC_DEBUG("SetStreamPath -> requestAddress 0x%x not us, ignoring",
	      requestedAddress);

    return;
  }
  send_active_source(VC_FALSE);
}


static void
give_device_power_status(int target, int status)
{
  uint8_t response[2];
  response[0] = CEC_Opcode_ReportPowerStatus;
  response[1] = status;
  cec_send_msg(target, response, 2, VC_TRUE);
}


static void
give_device_vendor_id(const VC_CEC_MESSAGE_T *msg)
 {
  uint8_t response[4];
  response[0] = CEC_Opcode_DeviceVendorID;
  response[1] = (uint8_t) ((myVendorId >> 16) & 0xff);
  response[2] = (uint8_t) ((myVendorId >> 8) & 0xff);
  response[3] = (uint8_t) ((myVendorId >> 0) & 0xff);
  cec_send_msg(msg->initiator, response, 4, VC_TRUE);
}


static void
send_cec_version(const VC_CEC_MESSAGE_T *msg)
 {
  uint8_t response[2];
  response[0] = CEC_Opcode_CECVersion;
  response[1] = 0x5;
  cec_send_msg(msg->initiator, response, 2, VC_TRUE);
}


static void
vc_cec_report_physicalAddress(uint8_t dest)
{
  uint8_t msg[4];
  msg[0] = CEC_Opcode_ReportPhysicalAddress;
  msg[1] = (uint8_t) ((physical_address) >> 8 & 0xff);
  msg[2] = (uint8_t) ((physical_address) >> 0 & 0xff);
  msg[3] = CEC_DeviceType_Tuner;
  cec_send_msg(CEC_BROADCAST_ADDR, msg, 4, VC_TRUE);
}

static void
send_deck_status(const VC_CEC_MESSAGE_T *msg)
{
  uint8_t response[2];
  response[0] = CEC_Opcode_DeckStatus;

  if(tv_vendor_id == CEC_VENDOR_ID_LG) {
    response[1] = 0x20;
  } else {
    response[1] = CEC_DECK_INFO_NO_MEDIA;
  }
  cec_send_msg(msg->initiator, response, 2, VC_TRUE);
}


static void
send_osd_name(const VC_CEC_MESSAGE_T *msg, const char *name)
{
  uint8_t response[15];
  int l = MIN(14, strlen(name));
  response[0] = CEC_Opcode_SetOSDName;
  memcpy(response + 1, name, l);
  cec_send_msg(msg->initiator, response, l+1, VC_TRUE);
}


/**
 *
 */
static void
handle_ActiveSource(const VC_CEC_MESSAGE_T *msg)
{
  active_physical_address = (msg->payload[1] << 8) | msg->payload[2];
  cec_we_are_not_active = active_physical_address != physical_address;


  CEC_DEBUG("Currently active address: 0x%x. That is %sus", 
	    active_physical_address,
	    cec_we_are_not_active ? "not " : "");

  CEC_DEBUG("We are %sactive", cec_we_are_not_active ? "not " : "");
}


/**
 *
 */
static void
handle_routing_change(const VC_CEC_MESSAGE_T *msg)
{
  uint16_t last = (msg->payload[1] << 8) | msg->payload[2];
  uint16_t cur  = (msg->payload[3] << 8) | msg->payload[4];
  cec_we_are_not_active = cur != physical_address;

  active_physical_address = cur;

  CEC_DEBUG("Routing change 0x%x -> 0x%x. That is %sus", 
	    last, cur, cec_we_are_not_active ? "not " : "");

  CEC_DEBUG("We are %sactive", cec_we_are_not_active ? "not " : "");
}

/**
 *
 */
static void
lginit()
{
  uint8_t msg[4];

  static int lg_inited;
  if(lg_inited)
    return;

  CEC_DEBUG("LG TV detected");

  lg_inited = 1;
  
  give_device_power_status(CEC_TV_ADDRESS, CEC_POWER_STATUS_ON_PENDING);

  myVendorId = CEC_VENDOR_ID_LG;

  msg[0] = CEC_Opcode_DeviceVendorID;
  msg[1] = myVendorId >> 16;
  msg[2] = myVendorId >> 8;
  msg[3] = myVendorId;
  cec_send_msg(0xf, msg, 4, VC_FALSE);

  msg[0] = CEC_Opcode_ReportPhysicalAddress;
  msg[1] = physical_address >> 8;
  msg[2] = physical_address;
  msg[3] = CEC_DeviceType_Tuner;
  cec_send_msg(0xf, msg, 4, VC_FALSE);

  give_device_power_status(CEC_TV_ADDRESS,CEC_POWER_STATUS_ON);


  if(control_input) {
    msg[0] = CEC_Opcode_ImageViewOn;
    cec_send_msg(CEC_TV_ADDRESS, msg, 1, VC_FALSE);

    msg[0] = CEC_Opcode_ActiveSource;
    msg[1] = physical_address >> 8;
    msg[2] = physical_address;
    cec_send_msg(CEC_BROADCAST_ADDR, msg, 3, VC_FALSE);
  }
}


/**
 *
 */
static void
handle_device_vendor_id(const VC_CEC_MESSAGE_T *msg)
{
  int deviceid = 
    (msg->payload[1] << 16) | (msg->payload[2] << 8) | msg->payload[3];
  
  if(msg->initiator != 0)
    return;

  tv_vendor_id = deviceid;
  if(deviceid == 0xe091) {
    lginit();
  }
}

#define SL_COMMAND_UNKNOWN_01           0x01
#define SL_COMMAND_UNKNOWN_02           0x02

#define SL_COMMAND_REQUEST_POWER_STATUS 0xa0
#define SL_COMMAND_POWER_ON             0x03
#define SL_COMMAND_CONNECT_REQUEST      0x04
#define SL_COMMAND_SET_DEVICE_MODE      0x05

/**
 *
 */
static void
handle_vendor_command_lg(const VC_CEC_MESSAGE_T *msg)
{
  uint8_t response[8];

  switch(msg->payload[1]) {
  case SL_COMMAND_UNKNOWN_01:
    response[0] = CEC_Opcode_VendorCommand;
    response[1] = 0x02;
    response[2] = 0x03;
    cec_send_msg(msg->initiator, response, 3, VC_TRUE);
    break;

  case SL_COMMAND_CONNECT_REQUEST:
    response[0] = CEC_Opcode_VendorCommand;
    response[1] = SL_COMMAND_SET_DEVICE_MODE;
    response[2] = CEC_DeviceType_Tuner;
    vc_cec_send_message(msg->initiator, response, 3, VC_TRUE);


    if(control_input) {
      response[0] = CEC_Opcode_ImageViewOn;
      vc_cec_send_message(CEC_TV_ADDRESS, response, 1, VC_FALSE);

      response[0] = CEC_Opcode_ActiveSource;
      response[1] = physical_address >> 8;
      response[2] = physical_address;
      vc_cec_send_message(CEC_BROADCAST_ADDR, response, 3, VC_FALSE);
    }

    vc_cec_set_osd_name(APPNAMEUSER);
    break;
  }  
}

/**
 *
 */
static void
handle_vendor_command(const VC_CEC_MESSAGE_T *msg)
{
  if(myVendorId == CEC_VENDOR_ID_LG)
    handle_vendor_command_lg(msg);
}

static void
cec_callback(void *callback_data, uint32_t param0, uint32_t param1,
	     uint32_t param2, uint32_t param3, uint32_t param4)
{
  VC_CEC_NOTIFY_T reason  = (VC_CEC_NOTIFY_T) CEC_CB_REASON(param0);
  VC_CEC_MESSAGE_T msg;
  CEC_OPCODE_T opcode;
  char hexbuf[64];

  uint32_t len     = CEC_CB_MSG_LENGTH(param0);
#if 0
  uint32_t retval  = CEC_CB_RC(param0);
  TRACE(TRACE_DEBUG, "CEC", 
	 "reason=0x%04x, len=0x%02x, retval=0x%02x, "
	 "param1=0x%08x, param2=0x%08x, param3=0x%08x, param4=0x%08x\n",
	 reason, len, retval, param1, param2, param3, param4);
#endif


  msg.length = len - 1;
  msg.initiator = CEC_CB_INITIATOR(param1);
  msg.follower  = CEC_CB_FOLLOWER(param1);

  if(msg.length) {
    uint32_t tmp = param1 >> 8;
    memcpy(msg.payload,                      &tmp,    sizeof(uint32_t)-1);
    memcpy(msg.payload+sizeof(uint32_t)-1,   &param2, sizeof(uint32_t));
    memcpy(msg.payload+sizeof(uint32_t)*2-1, &param3, sizeof(uint32_t));
    memcpy(msg.payload+sizeof(uint32_t)*3-1, &param4, sizeof(uint32_t));
  } else {
    memset(msg.payload, 0, sizeof(msg.payload));
  }


  switch(reason) {
  default:
    break;
  case VC_CEC_BUTTON_PRESSED:
    CEC_DEBUG("Key down: %x (%d,%d)", msg.payload[1],
	      stop_is_meta_key, play_is_meta_key);
    cec_emit_key_down(msg.payload[1]);
    break;

  case VC_CEC_BUTTON_RELEASE:
    CEC_DEBUG("Key up  : %x (%d,%d)", msg.payload[1],
	      stop_is_meta_key, play_is_meta_key);
    break;


  case VC_CEC_RX:

    opcode = CEC_CB_OPCODE(param1);
    bin2hex(hexbuf, sizeof(hexbuf), msg.payload+1, msg.length-1);
    CEC_DEBUG("RX: %-27s [0x%02x] (from:0x%x to:0x%x) %s\n",
	      cec_cmd_to_str[opcode], opcode,
	      CEC_CB_INITIATOR(param1), CEC_CB_FOLLOWER(param1),
	      hexbuf);

    if(CEC_CB_FOLLOWER(param1) != logical_address &&
       CEC_CB_FOLLOWER(param1) != 0xf)
      return;

    switch(opcode) {
    case CEC_Opcode_GiveDevicePowerStatus:
      give_device_power_status(msg.initiator, CEC_POWER_STATUS_ON);
      break;

    case CEC_Opcode_GiveDeviceVendorID:
      give_device_vendor_id(&msg);
      break;

    case CEC_Opcode_SetStreamPath:
      SetStreamPath(&msg);
      break;

    case CEC_Opcode_RoutingChange:
      handle_routing_change(&msg);
      break;

    case CEC_Opcode_GivePhysicalAddress:
      vc_cec_report_physicalAddress(msg.initiator);
      break;

    case CEC_Opcode_GiveOSDName:
      send_osd_name(&msg, APPNAMEUSER);
      break;

    case CEC_Opcode_GetCECVersion:
      send_cec_version(&msg);
      break;

    case CEC_Opcode_GiveDeckStatus:
      send_deck_status(&msg);
      break;

    case CEC_Opcode_RequestActiveSource:
      if(active_physical_address == physical_address)
	send_active_source(VC_FALSE);
      break;

    case CEC_Opcode_ActiveSource:
      handle_ActiveSource(&msg);
      break;

    case CEC_Opcode_Standby:
      cec_we_are_not_active = 1;
      CEC_DEBUG("We are not active");
      break;

    case CEC_Opcode_DeviceVendorID:
      handle_device_vendor_id(&msg);
      break;

    case CEC_Opcode_VendorCommand:
      handle_vendor_command(&msg);
      break;

    case CEC_Opcode_FeatureAbort:
      break;

    default:
      CEC_DEBUG("Unhandled RX command: 0x%02x", opcode);

      if(msg.follower == 0xf)
	break; // Never Abort on broadcast messages
      vc_cec_send_FeatureAbort(msg.initiator, opcode,
			       CEC_Abort_Reason_Unrecognised_Opcode);
      break;
    }
    break;
  }
}


/**
 *
 */
static void
set_logical_address(void *opaque, const char *str)
{
  fixed_la = str ? atoi(str) & 0xf : 0;
}


/**
 * We deal with CEC and HDMI events, etc here
 */
static void *
cec_thread(void *aux)
{
  htsmsg_t *s = htsmsg_store_load("cec") ?: htsmsg_create_map();

  prop_t *set;

  set =
    settings_add_dir(NULL, _p("Consumer Electronics Control"),
		     "display", NULL,
		     _p("Configure communications with your TV"),
		     "settings:cec");

  setting_create(SETTING_BOOL, set,
		 SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE_CSTR("Switch TV input source"),
		 SETTING_WRITE_BOOL(&control_input),
		 SETTING_HTSMSG("controlinput", s, "cec"),
		 NULL);

  setting_create(SETTING_BOOL, set,
		 SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE_CSTR("Shutdown UI when TV is off"),
		 SETTING_WRITE_BOOL(&auto_ui_shutdown),
		 SETTING_HTSMSG("auto_shutdown", s, "cec"),
		 NULL);

  setting_create(SETTING_BOOL, set,
		 SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE_CSTR("Use Stop button to send other remote codes"),
		 SETTING_WRITE_BOOL(&stop_is_meta_key),
		 SETTING_HTSMSG("stop_is_meta_key", s, "cec"),
		 NULL);

  setting_create(SETTING_BOOL, set,
		 SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE_CSTR("Use Play button to send other remote codes"),
		 SETTING_WRITE_BOOL(&play_is_meta_key),
		 SETTING_HTSMSG("play_is_meta_key", s, "cec"),
		 NULL);

  setting_create(SETTING_STRING, set,
		 SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE_CSTR("Override CEC Logical address"),
		 SETTING_CALLBACK(set_logical_address, NULL),
		 SETTING_HTSMSG("logicaladdress", s, "cec"),
		 NULL);

  vcos_log_set_level(CECHOST_LOG_CATEGORY, 
		     gconf.enable_cec_debug ? VCOS_LOG_TRACE : VCOS_LOG_NEVER);

  vc_cec_set_passive(1);

  vc_cec_register_callback(((CECSERVICE_CALLBACK_T) cec_callback), NULL);
  vc_cec_register_all();

  physical_address = 0xffff;


 restart:
  while(1) {
    if(vc_cec_get_physical_address(&physical_address) ||
       physical_address == 0xffff) {
    } else {
      CEC_DEBUG("Got physical address 0x%04x\n", physical_address);
      break;
    }
    sleep(1);
  }

  if(!fixed_la) {
    const int addresses = 
      (1 << CEC_AllDevices_eRec1) |
      (1 << CEC_AllDevices_eRec2) |
      (1 << CEC_AllDevices_eRec3) |
      (1 << CEC_AllDevices_eFreeUse);

    for(logical_address = 0; logical_address < 15; logical_address++) {
      if(((1 << logical_address) & addresses) == 0)
	continue;
      if(vc_cec_poll_address(logical_address) > 0)
	break;
    }

    if(logical_address == 15) {
      printf("Unable to find a free logical address, retrying\n");
      sleep(1);
      goto restart;
    }

  } else {
    logical_address = fixed_la;
  }

  CEC_DEBUG("Got logical address 0x%x\n", logical_address);

  vc_cec_set_logical_address(logical_address, CEC_DeviceType_Tuner, myVendorId);

  while(1) {
    sleep(1);
    vcos_log_set_level(CECHOST_LOG_CATEGORY, 
		       gconf.enable_cec_debug ? VCOS_LOG_TRACE : VCOS_LOG_NEVER);
  }

  return NULL;
}


/**
 *
 */
void
rpi_cec_init(void)
{
  return;
  hts_thread_create_detached("cec", cec_thread, NULL, THREAD_PRIO_BGTASK);
}
