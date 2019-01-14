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
#include <X11/Xlib.h>

#include "main.h"
#include "arch/arch.h"
#include "arch/posix/posix.h"
#include "linux.h"
#include "prop/prop.h"
#include "navigator.h"
#include "service.h"

// https://www.uninformativ.de/blog/postings/2017-04-02/0/POSTING-en.html

static void add_xdg_paths(void);

static int running = 1;

/**
 * Linux main
 */
int
main(int argc, char **argv)
{
  gconf.binary = argv[0];

  posix_init();

  XInitThreads();

  //  g_thread_init(NULL);

  parse_opts(argc, argv);

  linux_init();

  main_init();

  add_xdg_paths();

  glw_x11_main(&running);

  main_fini();

  arch_exit();
}


/**
 *
 */
void
arch_exit(void)
{
  exit(gconf.exit_code);
}


int
arch_stop_req(void)
{
  running = 0;
  return 0;
}



/**
 *
 */
static void
add_xdg_path(const char *class, const char *type)
{
  char id[64];
  char cmd[256];
  char path[512];
  snprintf(cmd, sizeof(cmd), "xdg-user-dir %s", class);
  FILE *fp = popen(cmd, "r");
  if(fp == NULL)
    return;

  if(fscanf(fp, "%511[^\n]", path) == 1) {
    char *title = strrchr(path, '/');
    title = title ? title + 1 : path;

    snprintf(id, sizeof(id), "xdg-user-dir-%s", class);

    service_create_managed(id, title, path, type, NULL, 0, 1,
			   SVC_ORIGIN_SYSTEM);
  }
  fclose(fp);
}


/**
 *
 */
static void
add_xdg_paths(void)
{
  add_xdg_path("MUSIC", "music");
  add_xdg_path("PICTURES", "photos");
  add_xdg_path("VIDEOS", "video");
}
