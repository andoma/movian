/*
 *  Networking
 *  Copyright (C) 2007-2008 Andreas Öman
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

#ifndef NET_H__
#define NET_H__

#include <sys/types.h>
#include <stdint.h>
#include <htsmsg/htsbuf.h>

void net_setup(void);

int tcp_connect(const char *hostname, int port, char *errbuf,
		size_t errbufsize, int timeout);

int tcp_write_queue(int fd, htsbuf_queue_t *q);

int tcp_write_queue_dontfree(int fd, htsbuf_queue_t *q);

int tcp_read_line(int fd, char *buf, const size_t bufsize,
		  htsbuf_queue_t *spill);

int tcp_read_data(int fd, char *buf, const size_t bufsize,
		  htsbuf_queue_t *spill);

int tcp_read_data_nowait(int fd, char *buf, const size_t bufsize, 
			 htsbuf_queue_t *spill);

int tcp_read(int fd, void *buf, size_t len, int all);

int tcp_write(int fd, const void *data, size_t len);

void tcp_close(int fd);


#endif /* NET_H__ */
