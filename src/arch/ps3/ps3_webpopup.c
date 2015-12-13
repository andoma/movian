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

#include <psl1ght/lv2/memory.h>
#include <psl1ght/lv2.h>
#include <sysutil/events.h>
#include <sysutil/web.h>
#include <unistd.h>
#include "main.h"
#include "ui/webpopup.h"


static hts_mutex_t web_mutex;
static hts_cond_t web_cond;


static web_browser_config webcfg;
static mem_container_t memcontainer;
static int browser_open;

int browser_visible;


/**
 *
 */
static void
web_sys_callback(int type, void *userdata)
{
  int r;
  TRACE(TRACE_DEBUG, "WEB", "Got callback 0x%x", type);

  switch(type) {
  case WEBBROWSER_GRABBED:
    browser_visible = 1;
    break;

  case WEBBROWSER_RELEASED:
    browser_visible = 0;
    break;

  case WEBBROWSER_UNLOADING_FINISHED:
    webBrowserShutdown();
    break;

  case WEBBROWSER_SHUTDOWN_FINISHED:
    r = lv2MemContinerDestroy(memcontainer);
    if(r)
      TRACE(TRACE_ERROR, "WEB", "Unable to release container: 0x%x", r);

    hts_mutex_lock(&web_mutex);
    hts_cond_signal(&web_cond);
    browser_open = 0;
    hts_mutex_unlock(&web_mutex);

    break;
  }
}


/**
 *
 */
static void
nav_callback(int type, void *session, void *usrdat)
{
  TRACE(TRACE_DEBUG, "WEB", "nav callback %d", type);
}


/**
 *
 */
static int
start_browser(const char *url)
{
  // only one guy at the time
  while(browser_open)
    hts_cond_wait(&web_cond, &web_mutex);

  webBrowserConfig(&webcfg, 0x20000); // V2
  webBrowserConfigSetFunction(&webcfg, 0x20 | 0x40);
  webBrowserConfigSetHeapSize(&webcfg, 48*1024*1024);
  webBrowserConfigSetTabCount(&webcfg, 1);

  webcfg.x = 200;
  webcfg.y = 200;
  webcfg.width = 800;
  webcfg.height = 600;
  webcfg.resolution_factor = 1;

  int mc_size;
  webBrowserEstimate(&webcfg, &mc_size);
  TRACE(TRACE_DEBUG, "WEB", "Required memory for browser: %d", mc_size);

  int r = lv2MemContinerCreate(&memcontainer, mc_size);
  if(r) {
    TRACE(TRACE_ERROR, "WEB", "Unable to alloc mem for browser -- 0x%x", r);
    return r;
  }

  webBrowserSetRequestHook(&webcfg, OPD32(nav_callback), NULL);

  r = webBrowserInitialize(OPD32(web_sys_callback), memcontainer);
  if(r) {
    TRACE(TRACE_ERROR, "WEB", "Unable to alloc mem for browser -- 0x%x", r);
    return r;
  }

  TRACE(TRACE_DEBUG, "WEB", "Browser opening %s", url);
  webcfg.request_cb = (intptr_t)OPD32(nav_callback);
  webBrowserCreate(&webcfg, url);

  browser_open = 1;
  return 0;
}


/**
 *
 */
void
webbrowser_open(const char *url, const char *title)
{
  hts_mutex_lock(&web_mutex);
  start_browser(url);
  hts_mutex_unlock(&web_mutex);
}

void
webbrowser_close(void)
{
  int r = webBrowserDestroy();
  TRACE(TRACE_DEBUG, "WEB", "Browser force code:0x%x", r);
}

/**
 *
 */
webpopup_result_t *
webpopup_create(const char *url, const char *title, const char *traps)
{
  hts_mutex_lock(&web_mutex);

  if(start_browser(url)) {
    hts_mutex_unlock(&web_mutex);
    return NULL;
  }

  while(browser_open != 2)
    hts_cond_wait(&web_cond, &web_mutex);

  hts_mutex_unlock(&web_mutex);

  return NULL;
}


/**
 *
 */
static void
web_shutdown(void *opaque, int retcode)
{
  hts_mutex_lock(&web_mutex);

  if(browser_open) {
    TRACE(TRACE_DEBUG, "WEB", "Browser open on shutdown, forcing close");
    int r = webBrowserDestroy();
    TRACE(TRACE_DEBUG, "WEB", "Browser force code:0x%x", r);

    while(browser_open)
      hts_cond_wait(&web_cond,&web_mutex);
  }

  hts_mutex_unlock(&web_mutex);
}



/**
 *
 */
static void
webpopup_init(void)
{
  hts_mutex_init(&web_mutex);
  hts_cond_init(&web_cond, &web_mutex);
  shutdown_hook_add(web_shutdown, NULL, 2);
}

INITME(INIT_GROUP_API, webpopup_init, NULL, 0);
