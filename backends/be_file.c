/*
 *  Backend using file I/O
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


#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "showtime.h"
#include "ui/glw/glw.h"
#include "navigator.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_probe.h"

TAILQ_HEAD(be_file_entry_queue,  be_file_entry);

nav_backend_t be_file;

typedef struct be_file_page {
  nav_page_t h;

  hts_thread_t bfp_scanner;

  prop_t *bfp_nodes;

  struct be_file_entry_queue bfp_entries;

} be_file_page_t;


/**
 *
 */
typedef struct be_file_entry {
  TAILQ_ENTRY(be_file_entry) bfe_link;
  char *bfe_url;
  prop_t *bfe_prop;


} be_file_entry_t;



/**
 *
 */
static int
be_file_canhandle(const char *url)
{
  fa_protocol_t *fap;
  return fa_resolve_proto(url, &fap) != NULL;
}


/**
 *
 */
static void
scandir_callback(void *arg, const char *url, const char *filename, int type)
{
  prop_t *p;
  be_file_page_t *bfp = arg;
  prop_t *urlp;
  be_file_entry_t *bfe;

  p = prop_create(NULL, "node");

  prop_set_string(prop_create(p, "filename"), filename);

  urlp = prop_create(p, "url");
  prop_set_string(urlp, url);

  bfe = malloc(sizeof(be_file_entry_t));
  bfe->bfe_url = strdup(url);
  bfe->bfe_prop = p;
  prop_ref_inc(p);
  TAILQ_INSERT_TAIL(&bfp->bfp_entries, bfe, bfe_link);

  prop_set_parent(p, bfp->bfp_nodes);
}


/**
 *
 */
static void *
scanner(void *aux)
{
  be_file_page_t *bfp = aux;
  be_file_entry_t *bfe;
  prop_t *media;

  TAILQ_INIT(&bfp->bfp_entries);
  fileaccess_scandir(bfp->h.np_url, scandir_callback, bfp);


  while((bfe = TAILQ_FIRST(&bfp->bfp_entries)) != NULL) {
    TAILQ_REMOVE(&bfp->bfp_entries, bfe, bfe_link);

    media = prop_create(bfe->bfe_prop, "media");
    fa_probe(media, prop_create(bfe->bfe_prop, "url"), bfe->bfe_url);

    prop_ref_dec(bfe->bfe_prop);
    free(bfe->bfe_url);
    free(bfe);
  }

  return NULL;
}


/**
 *
 */
static nav_page_t *
be_file_open(const char *url0, char *errbuf, size_t errlen)
{
  fa_protocol_t *fap;
  const char *url;
  struct stat buf;
  be_file_page_t *bfp;
  prop_t *p;

  if((url = fa_resolve_proto(url0, &fap)) == NULL) {
    snprintf(errbuf, errlen, "Protocol not handled");
    return NULL;
  }

  if(fap->fap_stat(url, &buf)) {
    snprintf(errbuf, errlen, "Unable to stat url");
    return NULL;
  }

  bfp = nav_page_create(&be_file, url0, sizeof(be_file_page_t));

  p = bfp->h.np_prop_root;

  if(S_ISDIR(buf.st_mode)) {

    /* TODO: Check if it is a DVD */

    prop_set_string(prop_create(p, "type"), "filedirectory");
    
    bfp->bfp_nodes = prop_create(p, "nodes");
    
    hts_thread_create_detached(&bfp->bfp_scanner, scanner, bfp);

  } else {
    abort();

  }

  return &bfp->h;
}


/**
 *
 */
nav_backend_t be_file = {
  .nb_canhandle = be_file_canhandle,
  .nb_open = be_file_open,
};


