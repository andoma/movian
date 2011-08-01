/*
 *  Showtime mediacenter
 *  Copyright (C) 2010 Andreas Öman
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

#include <stdio.h>
#include "prop/prop.h"
#include "settings.h"
#include "showtime.h"
#include "serdev.h"
#include "misc/strtab.h"

#define CONFNAME "ipc/serdev/lgtv"

typedef struct {
  serdev_t *sd;
  htsmsg_t *store;
  
  int prior_input;

} lgtv_t;

struct strtab idinput_tab[] = {
  { "hdmi1",       0x90 },
  { "hdmi2",       0x91 },
  { "vga1",        0x60 },
  { "component1",  0x40 },
};



#define LGTV_TIMEOUT 100
#define LGTV_CR "\r\n"

/**
 *
 */
static void
input_to_txt(int input, char *buf, size_t buflen)
{
  const char *c;

  switch(input >> 4) {
  case 1: c = "Analog"; break;
  case 2: c = "AV"; break;
  case 4: c = "Component"; break;
  case 5: c = "RGB/DTV"; break;
  case 6: c = "RGB"; break;
  case 9: c = "HDMI"; break;
  default:c = "Unknown"; break;
  }
  snprintf(buf, buflen, "%s-%d", c, (input & 0xf) + 1);
}


/**
 *
 */
static int
lgtv_read_power_state(serdev_t *sd, int *state)
{
  char *r;
  int setid, status;
  if(serdev_writef(sd, "kz 01 ff"LGTV_CR))
    return -1;
 
  r = serdev_readline(sd, LGTV_TIMEOUT);
  if(r == NULL)
    return -1;
  
  if(sscanf(r, "Z %x OK%02xx", &setid, &status) != 2)
    return -1;

  *state = !status;
  return 0;
}


/**
 *
 */
static int
lgtv_read_current_input(serdev_t *sd, int *inp)
{
  char *r;
  int setid;
  if(serdev_writef(sd, "xb 01 ff"LGTV_CR))
    return -1;
 
  r = serdev_readline(sd, LGTV_TIMEOUT);
  if(r == NULL)
    return -1;
  
  if(sscanf(r, "B %x OK%02xx", &setid, inp) != 2)
    return -1;

  return 0;
}


/**
 *
 */
static int
lgtv_set_power_state(serdev_t *sd, int state)
{
  char *r;
  int setid, status;

  TRACE(TRACE_INFO, "LGTV",
	"Setting power state to %s", state ? "on" : "off");

  if(serdev_writef(sd, "ka 01 %02x"LGTV_CR, !!state))
    return -1;
 
  r = serdev_readline(sd, LGTV_TIMEOUT);
  if(r == NULL)
    return -1;
  
  if(sscanf(r, "A %x OK%02xx", &setid, &status) != 2)
    return -1;

  return 0;
}


/**
 *
 */
static int
lgtv_set_current_input(serdev_t *sd, int inp)
{
  char *r;
  int setid, val;

  TRACE(TRACE_INFO, "LGTV",
	"Setting current input to 0x%x", inp);

  if(serdev_writef(sd, "xb 01 %02x"LGTV_CR, inp))
    return -1;
 
  r = serdev_readline(sd, LGTV_TIMEOUT);
  if(r == NULL) {
    return -1;
  }
  if(sscanf(r, "B %x OK%02xx", &setid, &val) != 2)
    return -1;

  return 0;
}


/**
 *
 */
static void
lgtv_save_settings(void *opaque, htsmsg_t *msg)
{
  htsmsg_store_save(msg, CONFNAME);
}


/**
 *
 */
static int
get_confed_input(lgtv_t *lg)
{
  const char *s = htsmsg_get_str(lg->store, "inputsource");
	
  if(s == NULL)
    return -1;

  return str2val(s, idinput_tab);
}

/**
 *
 */
static void
lgtv_shutdown(void *opaque, int exitcode)
{
  lgtv_t *lg = opaque;
  int current_input;
  int confed_input;

  if(exitcode == SHOWTIME_EXIT_OK)
    return;

  confed_input = get_confed_input(lg);

  if(!htsmsg_get_u32_or_default(lg->store, "autopower", 0))
    return;

  if(lgtv_read_current_input(lg->sd, &current_input))
    current_input = -1;

  if(lg->prior_input != -1)
    lgtv_set_current_input(lg->sd, lg->prior_input);

  if(current_input >= 0 && confed_input == current_input)
    lgtv_set_power_state(lg->sd, 0);
}


/**
 *
 */
static void
lgtv_init(serdev_t *sd, int curpower)
{
  prop_t *s;
  setting_t *x;
  char buf[64];

  lgtv_t *lg = calloc(1, sizeof(lgtv_t));

  lg->sd = sd;
  lg->prior_input = -1;

  s = settings_add_dir(NULL, _p("LG Television set"), NULL, NULL, NULL);

  lg->store = htsmsg_store_load(CONFNAME) ?: htsmsg_create_map();
  lg->prior_input = -1;
  
  settings_create_bool(s, "autopower", _p("Automatic TV power on/off"), 0, 
		       lg->store, NULL, NULL,
		       SETTINGS_INITIAL_UPDATE, NULL,
		       lgtv_save_settings, lg);


  shutdown_hook_add(lgtv_shutdown, lg, 0);

  // Currently not powered on

  if(!htsmsg_get_u32_or_default(lg->store, "autopower", 0))
    return;

  if(!curpower) {
    lgtv_set_power_state(lg->sd, 1);
    sleep(4);
  }

  if(lgtv_read_current_input(sd, &lg->prior_input))
    return;

  if(lg->prior_input == 1)
    lg->prior_input = 0x10;
  
  input_to_txt(lg->prior_input, buf, sizeof(buf));

  
  TRACE(TRACE_INFO, "LGTV", "Current input source %s", buf);

  if(lg->prior_input != -1) {
    int input = lg->prior_input;

    x = settings_create_multiopt(s, "inputsource", _p("Input source"),
				 NULL, NULL);

    settings_multiopt_add_opt_cstr(x, "none",      "None",        1);
    settings_multiopt_add_opt_cstr(x, "hdmi1",     "HDMI-1",      input == 0x90);
    settings_multiopt_add_opt_cstr(x, "hdmi2",     "HDMI-2",      input == 0x91);
    settings_multiopt_add_opt_cstr(x, "vga1",      "VGA-1",       input == 0x60);
    settings_multiopt_add_opt_cstr(x, "component1","Component-1", input == 0x40);

    settings_multiopt_initiate(x, lg->store, lgtv_save_settings, lg);

    if(!curpower) {
      int v = get_confed_input(lg);
      if(v != -1)
	lgtv_set_current_input(lg->sd, v);
    }
  }
}


/**
 *
 */
int
lgtv_probe(serdev_t *sd)
{
  int powerstate;

  if(serdev_set(sd, 9600))
    return 1;

  if(lgtv_read_power_state(sd, &powerstate))
    return 1;

  TRACE(TRACE_INFO, "LGTV", 
	"LG Television set found on %s, currently powered %s", 
	sd->sd_path, powerstate ? "on" : "off");

  lgtv_init(sd, powerstate);
  
  return 0;
}
