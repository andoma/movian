/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2013 Andreas Öman
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

#include "event.h"

#include <bcm_host.h>
#include <OMX_Core.h>
#include <interface/vmcs_host/vc_cecservice.h>
#include <interface/vchiq_arm/vchiq_if.h>

#include "rpi.h"

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

  [CEC_User_Control_Rewind]      = AVEC(ACTION_SKIP_BACKWARD),
  [CEC_User_Control_Forward]     = AVEC(ACTION_SKIP_FORWARD),

  [CEC_User_Control_Record]      = AVEC(ACTION_RECORD),

  [CEC_User_Control_RootMenu]    = AVEC(ACTION_HOME),

  [CEC_User_Control_F1Blue]      = AVEC(ACTION_ENABLE_SCREENSAVER),
  [CEC_User_Control_F2Red]       = AVEC(ACTION_MENU),
  [CEC_User_Control_F3Green]     = AVEC(ACTION_SHOW_MEDIA_STATS),
  [CEC_User_Control_F4Yellow]    = AVEC(ACTION_ITEMMENU),
};



/**
 *
 */
static void
cec_emit_key_down(int code)
{
  const action_type_t *avec = btn_to_action[code];
  if(avec != NULL) {
    int i = 0;
    while(avec[i] != 0)
      i++;
    event_t *e = event_create_action_multi(avec, i);
    event_to_ui(e);
  } else {
    TRACE(TRACE_DEBUG, "CEC", "Unmapped code 0x%02x", code);
  }
}






const uint32_t myVendorId = CEC_VENDOR_ID_BROADCOM;
uint16_t physical_address;
CEC_AllDevices_T logical_address;



static void
SetStreamPath(const VC_CEC_MESSAGE_T *msg)
{
    uint16_t requestedAddress;

    requestedAddress = (msg->payload[1] << 8) + msg->payload[2];
    if (requestedAddress != physical_address)
        return;
    vc_cec_send_ActiveSource(physical_address, VC_FALSE);
}


static void
give_device_power_status(const VC_CEC_MESSAGE_T *msg)
{
    // Send CEC_Opcode_ReportPowerStatus
    uint8_t response[2];
    response[0] = CEC_Opcode_ReportPowerStatus;
    response[1] = CEC_POWER_STATUS_ON;
    vc_cec_send_message(msg->initiator, response, 2, VC_TRUE);
}


static void
give_device_vendor_id(const VC_CEC_MESSAGE_T *msg)
 {
  uint8_t response[4];
  response[0] = CEC_Opcode_DeviceVendorID;
  response[1] = (uint8_t) ((myVendorId >> 16) & 0xff);
  response[2] = (uint8_t) ((myVendorId >> 8) & 0xff);
  response[3] = (uint8_t) ((myVendorId >> 0) & 0xff);
  vc_cec_send_message(msg->initiator, response, 4, VC_TRUE);
}


static void
send_cec_version(const VC_CEC_MESSAGE_T *msg)
 {
  uint8_t response[2];
  response[0] = CEC_Opcode_CECVersion;
  response[1] = 0x5;
  vc_cec_send_message(msg->initiator, response, 2, VC_TRUE);
}


static void
vc_cec_report_physicalAddress(uint8_t dest)
{
    uint8_t msg[4];
    msg[0] = CEC_Opcode_ReportPhysicalAddress;
    msg[1] = (uint8_t) ((physical_address) >> 8 & 0xff);
    msg[2] = (uint8_t) ((physical_address) >> 0 & 0xff);
    msg[3] = CEC_DeviceType_Tuner;
    vc_cec_send_message(CEC_BROADCAST_ADDR, msg, 4, VC_TRUE);
}

static void
send_deck_status(const VC_CEC_MESSAGE_T *msg)
{
  uint8_t response[2];
  response[0] = CEC_Opcode_DeckStatus;
  response[1] = CEC_DECK_INFO_NO_MEDIA;
  vc_cec_send_message(msg->initiator, response, 2, VC_TRUE);
}


static void
send_osd_name(const VC_CEC_MESSAGE_T *msg, const char *name)
{
  uint8_t response[15];
  int l = MIN(14, strlen(name));
  response[0] = CEC_Opcode_SetOSDName;
  memcpy(response + 1, name, l);
  vc_cec_send_message(msg->initiator, response, l+1, VC_TRUE);
}


static void
cec_callback(void *callback_data, uint32_t param0, uint32_t param1,
	     uint32_t param2, uint32_t param3, uint32_t param4)
{
  VC_CEC_NOTIFY_T reason  = (VC_CEC_NOTIFY_T) CEC_CB_REASON(param0);
  VC_CEC_MESSAGE_T msg;
  CEC_OPCODE_T opcode;

  uint32_t len     = CEC_CB_MSG_LENGTH(param0);
#if 1
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
    cec_emit_key_down(msg.payload[1]);
    break;


  case VC_CEC_RX:

    opcode = CEC_CB_OPCODE(param1);
    TRACE(TRACE_DEBUG, "CEC", 
	  "%s (from:0x%x to:0x%x)\n", cec_cmd_to_str[opcode],
	  CEC_CB_INITIATOR(param1), CEC_CB_FOLLOWER(param1));

    switch(opcode) {
    case CEC_Opcode_GiveDevicePowerStatus:
      give_device_power_status(&msg);
      break;

    case CEC_Opcode_GiveDeviceVendorID:
      give_device_vendor_id(&msg);
      break;

    case CEC_Opcode_SetStreamPath:
      SetStreamPath(&msg);
      break;

    case CEC_Opcode_GivePhysicalAddress:
      vc_cec_report_physicalAddress(msg.initiator);
      break;

    case CEC_Opcode_GiveOSDName:
      send_osd_name(&msg, "Showtime");
      break;

    case CEC_Opcode_GetCECVersion:
      send_cec_version(&msg);
      break;

    case CEC_Opcode_GiveDeckStatus:
      send_deck_status(&msg);
      break;

    default:
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
tv_service_callback(void *callback_data, uint32_t reason,
		    uint32_t param1, uint32_t param2)
{
  TRACE(TRACE_DEBUG, "TV", "State change 0x%08x 0x%08x 0x%08x",
	reason, param1, param2);

  if(reason & 1) {
    display_status = DISPLAY_STATUS_OFF;
    TRACE(TRACE_INFO, "TV", "Display status = off");
  } else {
    display_status = DISPLAY_STATUS_ON;
    TRACE(TRACE_INFO, "TV", "Display status = on");
  }
}


/**
 * We deal with CEC and HDMI events, etc here
 */
static void *
cec_thread(void *aux)
{
  TV_DISPLAY_STATE_T state;

  vc_tv_register_callback(tv_service_callback, NULL);
  vc_tv_get_display_state(&state);

  vc_cec_set_passive(1);

  vc_cec_register_callback(((CECSERVICE_CALLBACK_T) cec_callback), NULL);
  vc_cec_register_all();

 restart:
  while(1) {
    if(!vc_cec_get_physical_address(&physical_address) &&
       physical_address == 0xffff) {
    } else {
      TRACE(TRACE_DEBUG, "CEC",
	    "Got physical address 0x%04x\n", physical_address);
      break;
    }
    
    sleep(1);
  }


  const int addresses = 
    (1 << CEC_AllDevices_eRec1) |
    (1 << CEC_AllDevices_eRec2) |
    (1 << CEC_AllDevices_eRec3) |
    (1 << CEC_AllDevices_eFreeUse);

  for(logical_address = 0; logical_address < 15; logical_address++) {
    if(((1 << logical_address) & addresses) == 0)
      continue;
    if(vc_cec_poll_address(CEC_AllDevices_eRec1) > 0)
      break;
  }

  if(logical_address == 15) {
    printf("Unable to find a free logical address, retrying\n");
    sleep(1);
    goto restart;
  }

  vc_cec_set_logical_address(logical_address, CEC_DeviceType_Rec, myVendorId);

  while(1) {
    sleep(1);
  }

  vc_cec_set_logical_address(0xd, CEC_DeviceType_Rec, myVendorId);
  return NULL;
}


/**
 *
 */
void
rpi_cec_init(void)
{
  hts_thread_create_detached("cec", cec_thread, NULL, THREAD_PRIO_BGTASK);
}
