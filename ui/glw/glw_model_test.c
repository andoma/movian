/*
 *  GL Widgets, model loader, parser
 *  Copyright (C) 2008 Andreas Öman
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

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <alloca.h>
#include <stdarg.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include "glw_model.h"




static const void *
loadfile(const char *filename, size_t *sizeptr)
{
  struct stat buf;
  char *r;
  int fd;

  if(stat(filename, &buf))
    return NULL;

  if((fd = open(filename, O_RDONLY)) < 0)
    return NULL;

  r = malloc(buf.st_size + 1);

  read(fd, r, buf.st_size);
  r[buf.st_size] = 0;
  close(fd);
  return r;
}

static void
freefile(const void *data)
{
  free((void *)data);
}

const void *(*glw_rawloader)(const char *filename, size_t *sizeptr);
void (*glw_rawunload)(const void *data);





/**
 *
 */
int
main(int argc, char **argv)
{
  token_t *sof, *eof;
  token_t *l;
  
  errorinfo_t ei;

  glw_rawloader = loadfile;
  glw_rawunload = freefile;

  sof = calloc(1, sizeof(token_t));
  sof->type = TOKEN_START;
  sof->file =  refstr_create(argv[1]);

  l = glw_model_load1(argv[1], &ei, sof);
  
  if(l == NULL) {
    fprintf(stderr, "%s:%d: Error: %s\n", ei.file, ei.line, ei.error);
    glw_model_free_chain(sof);
    return -1;
  }

  eof = calloc(1, sizeof(token_t));
  eof->type = TOKEN_END;
  eof->file =  refstr_create(argv[1]);
  l->next = eof;

  printf("--- After initial lexing ---\n");

  glw_model_print_tree(sof, 0);

  if(glw_model_preproc(sof, &ei)) {
    fprintf(stderr, "%s:%d: Error: %s\n", ei.file, ei.line, ei.error);
    glw_model_free_chain(sof);
    return -1;
  }

  printf("--- After preproc ---\n");

  glw_model_print_tree(sof, 0);


  if(glw_model_parse(sof, &ei)) {
    fprintf(stderr, "%s:%d: Error: %s\n", ei.file, ei.line, ei.error);
    glw_model_free_chain(sof);
    return -1;
  }

  printf("--- After parse ---\n");

  glw_model_print_tree(sof, 0);

  printf("--- Evaluating ---\n");

  if(glw_model_eval_block(sof, &ei)) {
    fprintf(stderr, "%s:%d: Error: %s\n", ei.file, ei.line, ei.error);
    glw_model_free_chain(sof);
    return -1;
  }

  glw_model_free_chain(sof);

  return 0;
}
