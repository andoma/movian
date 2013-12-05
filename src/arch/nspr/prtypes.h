/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#ifndef PRTYPES_H__
#define PRTYPES_H__

#include <stdint.h>
#include "arch/threads.h"

typedef hts_mutex_t PRLock;

typedef struct {
  hts_mutex_t *mtx;
  hts_cond_t cond;
} PRCondVar;

typedef int PRIntervalTime;

typedef enum { PR_FAILURE = -1, PR_SUCCESS = 0 } PRStatus;

typedef int32_t PRInt32;

typedef unsigned int PRUintn;

typedef int PRThread;

#define PR_INTERVAL_NO_TIMEOUT -1

typedef void (*PRThreadPrivateDTOR)(void *priv);

#endif /* PRTYPES_H__ */
