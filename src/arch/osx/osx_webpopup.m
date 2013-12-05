/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#import <Webkit/Webkit.h>

#include "showtime.h"
#include "ui/webpopup.h"
#include "osx.h"
#include "misc/str.h"

TAILQ_HEAD(webpopup_queue, webpopup);


@interface WebWin : NSObject  <NSWindowDelegate> {
  NSWindow *_window;
  WebView *_webview;
  struct webpopup *_webpopup;
}

@end

/**
 *
 */
typedef struct webpopup {
  webpopup_result_t wp_wr; // Must be first
  TAILQ_ENTRY(webpopup) wp_link;
  char *wp_url;
  char *wp_title;
  char *wp_trap;
  hts_cond_t wp_cond;
  int wp_rc;
  WebWin *wp_ww;
} webpopup_t;


static hts_mutex_t webpopup_mtx;
static CFRunLoopSourceRef webpopup_source;
static struct webpopup_queue webpopup_pending;
static webpopup_t *wp_current;

@implementation WebWin


- (void)shutdown:(int) resultCode;
{
  webpopup_t *wp = _webpopup;
  if(wp != NULL && wp->wp_rc == -1) {
    wp->wp_rc = resultCode;
    CFRunLoopSourceSignal(webpopup_source);
    CFRunLoopWakeUp(CFRunLoopGetMain());
  }
}

- (void)webView:(WebView *)sender didStartProvisionalLoadForFrame:(WebFrame *)frame
{
  if([sender mainFrame] == frame) {
    const char *url = [[[[[frame provisionalDataSource] request] URL] absoluteString] UTF8String];
    webpopup_t *wp = _webpopup;
    if(wp != NULL && mystrbegins(url, wp->wp_trap)) {
      mystrset(&wp->wp_wr.wr_trapped.url, url);
      [self shutdown:WEBPOPUP_TRAPPED_URL];
    }
  }
}


/**
 *
 */
- (void)webView:(WebView *)sender didFinishLoadForFrame:(WebFrame *)frame
{
  if([sender mainFrame] != frame)
    return;

  NSURLRequest *request = [[frame dataSource] request];
  NSURLResponse *response = [[frame dataSource] response];
  
  const char *url = [[[request URL] absoluteString] UTF8String];
  int code = 200;
  if([response respondsToSelector:@selector(statusCode)])
    code = [((NSHTTPURLResponse *)response) statusCode];

  if(code != 200) {
    TRACE(TRACE_DEBUG, "OSX-WEBKIT", "Error %d", code);
    [self shutdown:WEBPOPUP_LOAD_ERROR];
    return;
  }

  webpopup_t *wp = _webpopup;
  if(wp != NULL && mystrbegins(url, wp->wp_trap)) {
    mystrset(&wp->wp_wr.wr_trapped.url, url);
    [self shutdown:WEBPOPUP_TRAPPED_URL];
  }
}


/**
 *
 */
- (void)webView:(WebView *)sender didFailLoadWithError:(NSError *)error forFrame:(WebFrame *)frame
{
  if([sender mainFrame] == frame)
    [self shutdown:WEBPOPUP_LOAD_ERROR];
}


/**
 *
 */
- (void)webView:(WebView *)sender didFailProvisionalLoadWithError:(NSError *)error forFrame:(WebFrame *)frame
{
  if([sender mainFrame] == frame) 
    [self shutdown:WEBPOPUP_CLOSED_BY_USER];
}


/**
 *
 */
- (BOOL)windowShouldClose:(id)sender
{
  [self shutdown:WEBPOPUP_CLOSED_BY_USER];
  return YES;
}


/**
 *
 */
- (void)dealloc {

  [_webview removeFromSuperview];
  [_webview setUIDelegate:nil];
  [_webview setPolicyDelegate:nil];
  [_webview setFrameLoadDelegate:nil];
  [_webview setResourceLoadDelegate:nil];

  [_webview release];
  [_window release];

  [super dealloc];
}

/**
 *
 */
- (id)initWithUrl:(NSString *)url
               wp:(webpopup_t *)wp
            title:(NSString *)title
{
  if(self = [super init]) {

    _webpopup = wp;

    NSRect frame = NSMakeRect(300.0, 300.0, 1280, 800);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask
                                            backing:NSBackingStoreBuffered
                                              defer:NO];

    [_window setReleasedWhenClosed:NO];
    [_window setTitle:title];
    [_window setDelegate:self];

   _webview = [[WebView alloc] initWithFrame:frame
                                    frameName:@"Frame"
                                    groupName:nil];
    
    [_window setContentView:_webview];
  
    //[window addChildWindow:popup ordered:NSWindowAbove];
    [_window makeKeyAndOrderFront:nil];


    [_webview setUIDelegate:self];
    [_webview setPolicyDelegate:self];
    [_webview setFrameLoadDelegate:self];
    [_webview setResourceLoadDelegate:self];

    [[_webview mainFrame] loadRequest:[NSURLRequest requestWithURL:[NSURL
                                                                      URLWithString:url]]];

#if 0
    NSProgressIndicator *noteSpinner = [[[NSProgressIndicator alloc] initWithFrame:NSMakeRect(NSMidX(frame)-16, NSMidY(frame)-16, 32, 32)] autorelease];
    [noteSpinner setStyle:NSProgressIndicatorSpinningStyle];
    [noteSpinner startAnimation:self];
    [_webview addSubview:noteSpinner];
#endif
  }

  return self;
}

@end




/**
 *
 */
static void
wp_open_new(void)
{
  webpopup_t *wp;
  hts_mutex_lock(&webpopup_mtx);
  wp_current = wp = TAILQ_FIRST(&webpopup_pending);
  if(wp != NULL) {
    TAILQ_REMOVE(&webpopup_pending, wp, wp_link);
  
    NSString *url   = [[NSString alloc] initWithUTF8String: wp->wp_url];
    NSString *title = [[NSString alloc] initWithUTF8String: wp->wp_title];
    wp->wp_ww = [[WebWin alloc] initWithUrl:url wp:wp title:title];
    [url release];
    [title release];
  }

  hts_mutex_unlock(&webpopup_mtx);
}


/**
 *
 */
static void
webpopup_perform(void *aux)
{
  if(wp_current != NULL && wp_current->wp_rc != -1) {
    webpopup_t *wp = wp_current;

    assert(wp->wp_ww != NULL);
    [wp->wp_ww release];
    wp_current->wp_ww = NULL;

    hts_mutex_lock(&webpopup_mtx);
    wp->wp_wr.wr_resultcode = wp->wp_rc;
    hts_cond_signal(&wp->wp_cond);
    hts_mutex_unlock(&webpopup_mtx);
    wp_current = NULL;
  }

  if(wp_current == NULL) {
    wp_open_new();
  }
}



/**
 *
 */
webpopup_result_t *
webpopup_create(const char *url, const char *title, const char *trap)
{
  webpopup_t *wp = calloc(1, sizeof(webpopup_t));
  webpopup_result_t *wr = &wp->wp_wr;
  hts_cond_init(&wp->wp_cond, &webpopup_mtx);
  wp->wp_rc = wp->wp_wr.wr_resultcode = -1;
  wp->wp_url   = strdup(url);
  wp->wp_title = strdup(title);
  wp->wp_trap  = strdup(trap);
  hts_mutex_lock(&webpopup_mtx);
  TAILQ_INSERT_TAIL(&webpopup_pending, wp, wp_link);
  CFRunLoopSourceSignal(webpopup_source);
  CFRunLoopWakeUp(CFRunLoopGetMain());

  while(wp->wp_wr.wr_resultcode == -1)
    hts_cond_wait(&wp->wp_cond, &webpopup_mtx);

  hts_mutex_unlock(&webpopup_mtx);

  webpopup_finalize_result(wr);

  free(wp->wp_url);
  free(wp->wp_title);
  free(wp->wp_trap);

  return wr;
}


/**
 *
 */
void
webpopup_init(void)
{
  hts_mutex_init(&webpopup_mtx);
  TAILQ_INIT(&webpopup_pending);

  CFRunLoopSourceContext context = {0, NULL, NULL, NULL, NULL, NULL, NULL,
				    NULL, NULL, webpopup_perform};

  webpopup_source = CFRunLoopSourceCreate(NULL, 0, &context);

  CFRunLoopAddSource(CFRunLoopGetCurrent(), webpopup_source,
		     kCFRunLoopDefaultMode);
}
