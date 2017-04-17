/*****************************************************************************
 * common.h: common definitions
 * Collection of useful common types and macros definitions
 *****************************************************************************
 * Copyright (C) 1998, 1999, 2000 VideoLAN
 *
 * Authors: Sam Hocevar <sam@via.ecp.fr>
 *          Vincent Seguin <seguin@via.ecp.fr>
 *          Gildas Bazin <gbazin@netcourrier.com>
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

#ifndef DVDCSS_COMMON_H
#define DVDCSS_COMMON_H

#if defined( _WIN32 )
#   include <io.h>                                             /* _lseeki64 */

/* several type definitions */
#   if defined( __MINGW32__ )
#       undef lseek
#       define lseek _lseeki64
#       if !defined( _OFF_T_ )
typedef long long _off_t;
typedef _off_t off_t;
#           define _OFF_T_
#       else
#           define off_t long long
#       endif
#   endif /* defined( __MINGW32__ ) */

#   if defined( _MSC_VER )
#       undef lseek
#       define lseek _lseeki64
#       if !defined( _OFF_T_DEFINED )
typedef __int64 off_t;
#           define _OFF_T_DEFINED
#       else
#           define off_t __int64
#       endif
#       define ssize_t SSIZE_T
#       define snprintf _snprintf
#       define strdup _strdup
#       define open _open
#       define close _close
#       define read _read
#       define write _write
#   endif /* defined( _MSC_VER ) */

#endif /* defined( _WIN32 ) */

#ifdef __ANDROID__
# undef  lseek
# define lseek lseek64
# undef  off_t
# define off_t off64_t
#endif /* __ANDROID__ */

#endif /* DVDCSS_COMMON_H */
