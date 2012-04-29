/*
 * main.c
 *
 * Copyright (C) an0nym0us
 *
 * This software is distributed under the terms of the GNU General Public
 * License ("GPL") version 3, as published by the Free Software Foundation.
 *
 */

#include <stdio.h>
#include <string.h>

#include <psl1ght/lv2.h>
#include <lv2/process.h>


int
main(int argc, char **argv)
{
  char path[200];
  strcpy(path, argv[0]);
  char *x = strrchr(path, '/');
  if(x == NULL)
    exit(1);
  strcpy(x + 1, "showtime.self");
  sysProcessExitSpawn2(path, 0, 0, 0, 0, 1200, 0x70);
  exit(0);
}
