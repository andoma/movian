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

#pragma once

#include "networking/http.h"

#define WEBPOPUP_TRAPPED_URL    0
#define WEBPOPUP_CLOSED_BY_USER 1
#define WEBPOPUP_LOAD_ERROR     2

typedef struct webpopup_result {
  int wr_resultcode;


  struct {
    char *url; // Full URL

    char *hostname;

    char *path;

    int port;

    struct http_header_list qargs;

  } wr_trapped;

} webpopup_result_t;


webpopup_result_t *webpopup_create(const char *url, const char *title,
                                   const char *traps);

void webpopup_result_free(webpopup_result_t *wr);

void webpopup_finalize_result(webpopup_result_t *wr);

