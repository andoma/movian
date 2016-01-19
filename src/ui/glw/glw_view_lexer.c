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
#include "glw.h"
#include "glw_view.h"
#include "fileaccess/fileaccess.h"
#include "misc/str.h"

/**
 *
 */
static void
lexer_link_token(token_t *prev, rstr_t *f, int line, token_t *t,
		 token_type_t type)
{
  t->type = type;
  prev->next = t;

  t->file = rstr_dup(f);
  t->line = line;
}


/**
 *
 */
static token_t *
lexer_add_token_simple(glw_root_t *gr, 
		       token_t *prev, rstr_t *f, int line, token_type_t type)
{
  token_t *t = glw_view_token_alloc(gr);
  lexer_link_token(prev, f, line, t, type);
  return t;
}


/**
 *
 */
static token_t *
lexer_add_token_string(glw_root_t *gr,
		       token_t *prev, rstr_t *f, int line,
		       const char *start, const char *end, token_type_t type)
{
  token_t *t = glw_view_token_alloc(gr);
  t->t_rstring = rstr_allocl(start, end - start);

  deescape_cstyle(rstr_data(t->t_rstring));

  lexer_link_token(prev, f, line, t, type);
  return t;
}


/**
 *
 */
static token_t *
lexer_add_token_float(glw_root_t *gr, token_t *prev, rstr_t *f, int line,
		       const char *start, const char *end)
{
  token_t *t = lexer_add_token_simple(gr, prev, f, line, TOKEN_FLOAT);
  float sign = 1.0f;
  int n, s = 0, m = 0;

  if(*start == '-') {
    start++;
    sign = -1.0;
  }
  
  if(start == end) {
    // A bit strange
    t->t_float = -1.0;
    return t;
  }

  n = 0;
  while(start < end) {
    s = *start++;
    if(s < '0' || s > '9')
      break;
    n = n * 10 + s - '0';
  }
  
  t->t_float = n;
  if(start == end || s != '.') {
    t->t_float *= sign;
    return t;
  }

  n = 0;
  while(start < end) {
    s = *start++;
    if(s < '0' || s > '9')
      break;
    n = n * 10 + s - '0';
    m++;
  }

  t->t_float += pow(10, -m) * n;
  t->t_float *= sign;
  return t;
}


/**
 *
 */
static token_t *
lexer_single_char(glw_root_t *gr, token_t *next, rstr_t *f, int line, char s)
{
  token_type_t ty;
  switch(s) {
  case '#' : ty = TOKEN_HASH;                     break;
  case '=' : ty = TOKEN_ASSIGNMENT;               break;
  case '(' : ty = TOKEN_LEFT_PARENTHESIS;         break;
  case ')' : ty = TOKEN_RIGHT_PARENTHESIS;        break;
  case '[' : ty = TOKEN_LEFT_BRACKET;             break;
  case ']' : ty = TOKEN_RIGHT_BRACKET;            break;
  case '{' : ty = TOKEN_BLOCK_OPEN;               break;
  case '}' : ty = TOKEN_BLOCK_CLOSE;              break;
  case ';' : ty = TOKEN_END_OF_EXPR;              break;
  case ',' : ty = TOKEN_SEPARATOR;                break;
  case '.' : ty = TOKEN_DOT;                      break;

  case '+' : ty = TOKEN_ADD;                      break;
  case '-' : ty = TOKEN_SUB;                      break;
  case '*' : ty = TOKEN_MULTIPLY;                 break;
  case '/' : ty = TOKEN_DIVIDE;                   break;
  case '%' : ty = TOKEN_MODULO;                   break;
  case '$' : ty = TOKEN_DOLLAR;                   break;
  case '!' : ty = TOKEN_BOOLEAN_NOT;              break;
  case '&' : ty = TOKEN_AMPERSAND;                break;
  case '>' : ty = TOKEN_GT;                       break;
  case '<' : ty = TOKEN_LT;                       break;
  case ':' : ty = TOKEN_COLON;                    break;
  default:
    return NULL;
  }
  return lexer_add_token_simple(gr, next, f, line, ty);
}


#define lex_isalpha(v) \
 (((v) >= 'a' && (v) <= 'z') || ((v) >= 'A' && (v) <= 'Z') || ((v) == '_'))

#define lex_isdigit(v) \
  (((v) >= '0' && (v) <= '9') || (v) == '-')

#define lex_isalnum(v) (lex_isalpha(v) || lex_isdigit(v))


/**
 * Do lexical analysis of buffer in 'str'.
 *
 * And start do add tokens after 'prev'
 *
 * Returns pointer to last token, or NULL if an error occured.
 * If an error occured 'ei' will be filled with data
 */
token_t *
glw_view_lexer(glw_root_t *gr, const char *src, errorinfo_t *ei,
               rstr_t *f, token_t *prev)
{
  const char *start;
  int line = 1;
  token_t *t;

  while(*src != 0) {
      
    if(*src == '\n') {
      /* newline */
      /* TODO: DOS CR support ? */
      src++;
      line++;
      continue;
    }

    if(*src <= 32) {
      /* whitespace */
      src++;
      continue;
    }

    if(src[0] == 'v' && src[1] == 'o' && src[2] == 'i' && src[3] == 'd') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_VOID);
      src+=4;
      continue;
    }

    if(src[0] == 't' && src[1] == 'r' && src[2] == 'u' && src[3] == 'e') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_INT);
      src+=4;
      prev->t_int = 1;
      continue;
    }

    if(src[0] == 'f' && src[1] == 'a' && src[2] == 'l' && src[3] == 's' &&
       src[4] == 'e') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_INT);
      src+=5;
      prev->t_int = 0;
      continue;
    }

    if(*src == '/' && src[1] == '/') {
      // C++ style comment
      src += 2;
      while(*src != '\n')
	src++;
      src++;
      line++;
      continue;
    }

    if(*src == '/' && src[1] == '*') {
      /* A normal C-comment */
      src += 2;

      while(*src != '/' || src[-1] != '*') {
	if(*src == '\n')
	  line++;
	src++;
      }

      src++;
      continue;
    }

    if(src[0] == '&' && src[1] == '&') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_BOOLEAN_AND);
      src+=2;
      continue;
    }

    if(src[0] == '?' && src[1] == '=') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_COND_ASSIGNMENT);
      src+=2;
      continue;
    }

    if(src[0] == '<' && src[1] == '-') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_LINK_ASSIGNMENT);
      src+=2;
      continue;
    }

    if(src[0] == ':' && src[1] == '=') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_REF_ASSIGNMENT);
      src+=2;
      continue;
    }

    if(src[0] == '_' && src[1] == '=' && src[2] == '_') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_DEBUG_ASSIGNMENT);
      src+=3;
      continue;
    }

    if(src[0] == '|' && src[1] == '|') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_BOOLEAN_OR);
      src+=2;
      continue;
    }

    if(src[0] == '^' && src[1] == '^') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_BOOLEAN_XOR);
      src+=2;
      continue;
    }

    if(src[0] == '=' && src[1] == '=') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_EQ);
      src+=2;
      continue;
    }

    if(src[0] == '!' && src[1] == '=') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_NEQ);
      src+=2;
      continue;
    }

    if(src[0] == '?' && src[1] == '?') {
      prev = lexer_add_token_simple(gr, prev, f, line, TOKEN_NULL_COALESCE);
      src+=2;
      continue;
    }


    if(!(src[0] == '-' && lex_isdigit(src[1]))) {
      if((t = lexer_single_char(gr, prev, f, line, *src)) != NULL) {
	src++;
	prev = t;
	continue;
      }
    }


    start = src;


    if(*src == '"' || *src == '\'') {
      /* A quoted string " ... " */
      char stop = *src;
      src++;
      start++;

      while((*src != stop || (src[-1] == '\\' && src[-2] != '\\')) && *src != 0) {
	if(*src == '\n')
	  line++;
	src++;
      }
      if(*src != stop) {
	snprintf(ei->error, sizeof(ei->error), "Unterminated quote");
	snprintf(ei->file,  sizeof(ei->file),  "%s", rstr_get(f));
	ei->line = line;
	return NULL;
      }

      prev = lexer_add_token_string(gr, prev, f, line, start, src,
				    TOKEN_RSTRING);
      if(stop == '\'')
	prev->t_rstrtype = PROP_STR_RICH;
      src++;
      continue;
    }


    if(lex_isalpha(*src)) {
      /* Alphanumeric string */
      while(lex_isalnum(*src))
	src++;

      prev = lexer_add_token_string(gr, prev, f, line, start, src, 
				    TOKEN_IDENTIFIER);
      continue;
    }

    if(lex_isdigit(*src)) {
      /* Integer */
      while(lex_isdigit(*src))
	src++;

      if(*src == '.') {
	src++;
	/* , or a float */
	while(lex_isdigit(*src))
	  src++;

      }
      if(*src == 'f')
	/* we support having the 'f' postfix around too */
	src++;
      
      prev = lexer_add_token_float(gr, prev, f, line, start, src);
      continue;
    }

    snprintf(ei->error, sizeof(ei->error), "Invalid char '%c'",
	     *src > 31 ? *src : ' ');
    snprintf(ei->file,  sizeof(ei->file),  "%s", rstr_get(f));
    ei->line = line;
    return NULL;
  }
  return prev;
}



/**
 * Load a view file and do lexographical parsing
 *
 * Returns pointer to last token, or NULL if an error occured.
 * If an error occured 'ei' will be filled with data
 */
token_t *
glw_view_load1(glw_root_t *gr, rstr_t *url, errorinfo_t *ei, token_t *prev,
               int may_unlock)
{
  token_t *last;
  char errbuf[256];

  rstr_t *p = fa_absolute_path(url, prev->file);

  if(may_unlock)
    glw_unlock(gr);

  buf_t *b = fa_load(rstr_get(p),
                     FA_LOAD_VPATHS(gr->gr_vpaths),
                     FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                     NULL);

  if(may_unlock)
    glw_lock(gr);

  if(b == NULL) {
    snprintf(ei->error, sizeof(ei->error), "Unable to open \"%s\" -- %s",
	     rstr_get(p), errbuf);
    snprintf(ei->file,  sizeof(ei->file),  "%s", rstr_get(prev->file));
    ei->line = prev->line;
    rstr_release(p);
    return NULL;
  }

  last = glw_view_lexer(gr, buf_cstr(b), ei, p, prev);
  buf_release(b);
  rstr_release(p);
  return last;
}
