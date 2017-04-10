/*****************************************************************************
 * libdvdcss.h: private DVD reading library data
 *****************************************************************************
 * Copyright (C) 1998-2001 VideoLAN
 *
 * Authors: Stéphane Borel <stef@via.ecp.fr>
 *          Sam Hocevar <sam@zoy.org>
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

#ifndef DVDCSS_LIBDVDCSS_H
#define DVDCSS_LIBDVDCSS_H

#include <limits.h>

#include "dvdcss/dvdcss.h"
#include "css.h"
#include "device.h"

/*****************************************************************************
 * libdvdcss method: used like init flags
 *****************************************************************************/
enum dvdcss_method {
    DVDCSS_METHOD_KEY,
    DVDCSS_METHOD_DISC,
    DVDCSS_METHOD_TITLE,
};
/*****************************************************************************
 * The libdvdcss structure
 *****************************************************************************/
struct dvdcss_s
{
    /* File descriptor */
    char * psz_device;
    int    i_fd;
    int    i_pos;

    /* File handling */
    int ( * pf_seek )  ( dvdcss_t, int );
    int ( * pf_read )  ( dvdcss_t, void *, int );
    int ( * pf_readv ) ( dvdcss_t, const struct iovec *, int );

    /* Decryption stuff */
    enum dvdcss_method i_method;
    struct css   css;
    int          b_ioctls;
    int          b_scrambled;
    struct dvd_title *p_titles;

    /* Key cache directory and pointer to the filename */
    char   psz_cachefile[PATH_MAX];
    char * psz_block;

    /* Error management */
    const char *psz_error;
    int    b_errors;
    int    b_debug;

#ifdef _WIN32
    int    b_file;
    char * p_readv_buffer;
    int    i_readv_buf_size;
#endif /* _WIN32 */

    void                *p_stream;
    dvdcss_stream_cb    *p_stream_cb;
};

/*****************************************************************************
 * Functions used across the library
 *****************************************************************************/
void print_error ( dvdcss_t, const char *, ... );
void print_debug ( const dvdcss_t, const char *, ... );

#endif /* DVDCSS_LIBDVDCSS_H */
