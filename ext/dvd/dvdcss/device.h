/*****************************************************************************
 * device.h: DVD device access
 *****************************************************************************
 * Copyright (C) 1998-2002 VideoLAN
 *
 * Authors: Stéphane Borel <stef@via.ecp.fr>
 *          Sam Hocevar <sam@zoy.org>
 *          Håkan Hjort <d95hjort@dtek.chalmers.se>
 *
 * libdvdcss is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * libdvdcss is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with libdvdcss; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *****************************************************************************/

#ifndef DVDCSS_DEVICE_H
#define DVDCSS_DEVICE_H

#include "config.h"

/*****************************************************************************
 * iovec structure: vectored data entry
 *****************************************************************************/
#if defined( WIN32 ) && !defined( SYS_CYGWIN )
#   include <io.h>                                                 /* read() */
#else
#   include <sys/types.h>

#if !defined(WII) && !defined(PS3)
#   include <sys/uio.h>                                      /* struct iovec */
#endif

#endif

#if ( defined( WIN32 ) && !defined( SYS_CYGWIN ) ) || defined ( WII ) || defined (PS3)
struct iovec
{
    void *iov_base;     /* Pointer to data. */
    size_t iov_len;     /* Length of data.  */
};
#endif

#include "dvdcss/dvdcss.h"


/*****************************************************************************
 * Device reading prototypes
 *****************************************************************************/
int  dvdcss_use_ioctls   ( dvdcss_t );
void dvdcss_check_device ( dvdcss_t );
int  dvdcss_open_device  ( dvdcss_t,  struct svfs_ops *svfs_ops );
int  dvdcss_close_device ( dvdcss_t );

#endif /* DVDCSS_DEVICE_H */
