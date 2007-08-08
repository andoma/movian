/*
 *  Very simple RPC using line based TCP connection
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

#ifndef COMS_H
#define COMS_H

#include <netinet/in.h>

FILE *open_fd_tcp(const char *addr, int port, struct sockaddr *self, 
		  socklen_t *selflen);

typedef struct rpc {
  FILE *f;
  char *ret;
  int retsize;

  char **argv;
  int argc;
  int argcmax;

} rpc_t;

void rpc_init(rpc_t *rpc, FILE *f);

int rpc_do0(rpc_t *rpc, int chkerr, const char *fmt, ...);

#define rpc_do(rpc, fmt...) rpc_do0(rpc, 0, fmt)
#define rpc_do_chk(rpc, fmt...) rpc_do0(rpc, 1, fmt)

void rpc_close(rpc_t *rpc);

#endif /* COMS_H */
