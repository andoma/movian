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

#ifndef SOAP_H__
#define SOAP_H__

#include "htsmsg/htsbuf.h"
#include "htsmsg/htsmsg.h"

void soap_encode_arg(htsbuf_queue_t *xml, htsmsg_field_t *f);

void soap_encode_args(htsbuf_queue_t *xml, htsmsg_t *args);

int soap_exec(const char *uri, const char *service, int version,
	      const char *method, htsmsg_t *in, htsmsg_t **out,
	      char *errbuf, size_t errlen);

#endif // SOAP_H__
