/*
 *  GL Widgets, model loader, function definitions
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
#include <stdio.h>
#include "glw_model.h"


static int 
glwf_sin(struct token *self, struct token **stack, glw_model_eval_context_t *ec)
{
  printf("it's a sin\n");
  return 0;
}

static int 
glwf_pow(struct token *self, struct token **stack, glw_model_eval_context_t *ec)
{
  printf("it's a pow\n");
  return 0;
}




static const token_func_t funcvec[] = {
  {"sin", glwf_sin},
  {"pow", glwf_pow},
};

int 
glw_model_function_resolve(token_t *t)
{
  int i;

  for(i = 0; i < sizeof(funcvec) / sizeof(funcvec[0]); i++)
    if(!strcmp(funcvec[i].name, t->payload.str)) {
      free(t->payload.str);
      t->payload.func = &funcvec[i];
      t->type = TOKEN_FUNCTION;
      return 0;
    }
  return -1;
}
