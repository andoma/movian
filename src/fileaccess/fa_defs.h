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
#pragma once
#define FA_DEBUG           0x1
#define FA_NO_DEBUG        0x2  // Force debug to be off
#define FA_STREAMING       0x4
#define FA_CACHE           0x8
#define FA_BUFFERED_SMALL  0x10
#define FA_BUFFERED_BIG    0x20
#define FA_DISABLE_AUTH    0x40
#define FA_COMPRESSION     0x80
#define FA_NOFOLLOW        0x100
#define FA_WRITE           0x400  // Open for writing (always creates file)
#define FA_APPEND          0x800  /* Only if FA_WRITE:
                                     Seek to EOF when opening
                                     otherwise truncate */
#define FA_IMPORTANT       0x1000
#define FA_NO_RETRIES      0x2000
#define FA_NO_PARKING      0x4000
#define FA_BUFFERED_NO_PREFETCH 0x8000
#define FA_CONTENT_ON_ERROR     0x10000
#define FA_NON_INTERACTIVE      0x20000 // No auth popups, etc
#define FA_NO_COOKIES           0x40000
#define FA_SSL_VERIFY           0x80000
