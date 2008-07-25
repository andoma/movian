/*
 *  Common HID functions
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

#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>



#include "showtime.h"

#include "lircd.h"
#include "imonpad.h"
#include "lcdd.h"
#include "hid.h"


void
hid_init(void)
{
#if 0
  const char *irtype;

  irtype = config_get_str("lirctype", NULL);

  if(irtype != NULL) {
    
    if(!strcasecmp(irtype, "lircd")) {
      lircd_init();
    } else if(!strcasecmp(irtype, "imonpad")) {
      imonpad_init();
    }
  }

  if(config_get_bool("lcdd", 0)) {
    lcdd_init();
  }
#endif
}
