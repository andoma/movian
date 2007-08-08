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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "coms.h"


FILE *
open_fd_tcp(const char *addr, int port, struct sockaddr *self, 
	    socklen_t *selflen)
{
  struct sockaddr_in sin;
  int s;

  memset(&sin, 0, sizeof(sin));

  sin.sin_family = AF_INET;
  sin.sin_port = htons(port);
  sin.sin_addr.s_addr = inet_addr(addr);
  
  s = socket(AF_INET, SOCK_STREAM, 0);
  if(s == -1)
    return NULL;
  
  if(connect(s, (struct sockaddr *)&sin, sizeof(sin))) {
    close(s);
    return NULL; 
  }
  
  if(self != NULL)
    getsockname(s, self, selflen);

  return fdopen(s, "rw+");
}

void
rpc_init(rpc_t *rpc, FILE *f)
{
  rpc->f = f;
  rpc->retsize = 5000;
  rpc->ret = malloc(rpc->retsize);

  rpc->argcmax = 100;
  rpc->argv = malloc(rpc->argcmax * sizeof(char *));

}


int
rpc_do0(rpc_t *rpc, int chkerr, const char *fmt, ...)
{
  char *x;
  int l;
  va_list ap;

  if(rpc->f == NULL)
    return -1;

  va_start(ap, fmt);
  vfprintf(rpc->f, fmt, ap);
  va_end(ap);
  fprintf(rpc->f, "\n");
  fflush(rpc->f);

  if(fgets(rpc->ret, rpc->retsize, rpc->f) == NULL) {
    rpc_close(rpc);
    return -1;
  }
    
  l = strlen(rpc->ret);

  x = rpc->ret;

  while(l > 0 && (x[l - 1] == 0x0a || x[l - 1] == 0x0d))
    x[--l] = 0;

  //  printf("%s\n", x);

  rpc->argc = 0;

  while(*x != 0 && rpc->argc < rpc->argcmax) {

    rpc->argv[rpc->argc] = x;
    rpc->argc++;
    
    while(*x != 0 && *x != '\t')
      x++;

    if(*x == '\t') {
      *x = 0;
      x++;
    }

  }
  if(chkerr)
    if(rpc->argc < 1 || strcmp(rpc->argv[0], "OK"))
      return 1;
#if 0
  for(l = 0; l < rpc->argc; l++)
    printf("\t%d:|%s|\n", l, rpc->argv[l]);
#endif

  return 0;
}



void
rpc_close(rpc_t *rpc)
{
  fclose(rpc->f);
  rpc->f = NULL;
  free(rpc->ret);
  free(rpc->argv);
}


