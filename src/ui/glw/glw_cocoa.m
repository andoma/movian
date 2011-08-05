/*
 *  Cocoa UI
 *  Copyright (C) 2009-2010 Mattias Wadman
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

/*
 * Some code based on GL example from apple
 *
 * Interface is built from MainMenu.xib
 *
 * TOOD:
 * Cursor not hidden if visible when going from fullscreen to window
 * Video slow down when moving to other screen?
 *
 */

#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/glu.h>

/* for screensaver stuff */
#include <CoreServices/CoreServices.h>

#include "glw_cocoa.h"
#include "glw.h"

#include "showtime.h"
#include "settings.h"
#include "navigator.h"
#include "misc/strtab.h"


typedef struct glw_cocoa {  
  glw_root_t gr;
  
  /* used to pass args from glw_cocoa_start to prepareOpenGL */
  ui_t *start_ui;
  int start_primary;
  char *start_theme;
  
  int glready; /* prepareOpenGL has been run */
  
  int is_fullscreen;
  int want_fullscreen;
  
  int is_cursor_hidden;
  int is_fullwindow;
  
  int skip_first_openfile_check;
  
  setting_t *fullscreen_setting;
  
  int stop;

} glw_cocoa_t;


static glw_cocoa_t gcocoa;

#define _NSTabKey 9
#define _NSShiftTabKey 25 /* why other value when shift is pressed? */
#define _NSEnterKey 13
#define _NSBackspaceKey 127
#define _NSEscapeKey 27
#define _NSSpaceKey 32

static const struct {
  int key;
  int mod;
  int action1;
  int action2;
  int action3;
} keysym2action[] = {
  
  /* NSFunctionKeyMask is filtered out when matching mappings */
  
  { NSLeftArrowFunctionKey,   0,                ACTION_LEFT },
  { NSRightArrowFunctionKey,  0,                ACTION_RIGHT },
  { NSUpArrowFunctionKey,     0,                ACTION_UP },
  { NSDownArrowFunctionKey,   0,                ACTION_DOWN },
  { NSPageUpFunctionKey,      0,                ACTION_PAGE_UP, ACTION_NEXT_CHANNEL },
  { NSPageDownFunctionKey,    0,                ACTION_PAGE_DOWN, ACTION_PREV_CHANNEL },
  { NSHomeFunctionKey,        0,                ACTION_TOP },
  { NSEndFunctionKey,         0,                ACTION_BOTTOM },
  
  { _NSShiftTabKey,           NSShiftKeyMask,   ACTION_FOCUS_PREV },
  
  { NSLeftArrowFunctionKey,   NSAlternateKeyMask, ACTION_NAV_BACK },
  { NSRightArrowFunctionKey,  NSAlternateKeyMask, ACTION_NAV_FWD },
  
  { NSLeftArrowFunctionKey,   NSCommandKeyMask, ACTION_SEEK_BACKWARD }, 
  { NSRightArrowFunctionKey,  NSCommandKeyMask, ACTION_SEEK_FORWARD }, 
  
  { NSLeftArrowFunctionKey,   NSShiftKeyMask|NSCommandKeyMask, ACTION_PREV_TRACK }, 
  { NSRightArrowFunctionKey,  NSShiftKeyMask|NSCommandKeyMask, ACTION_NEXT_TRACK }, 
  
  /* only used for fullscreen, in windowed mode we dont get events with
   * NSCommandKeyMask set */
  { '+',                      NSCommandKeyMask, ACTION_ZOOM_UI_INCR },
  { '-',                      NSCommandKeyMask, ACTION_ZOOM_UI_DECR },
  { 'f',                      NSCommandKeyMask, ACTION_FULLSCREEN_TOGGLE },
  
  { NSF11FunctionKey,         0,                ACTION_FULLSCREEN_TOGGLE },
  
  /*
   { XF86XK_AudioLowerVolume, 0,   ACTION_VOLUME_DOWN },
   { XF86XK_AudioRaiseVolume, 0,   ACTION_VOLUME_UP },
   { XF86XK_AudioMute,        0,   ACTION_VOLUME_MUTE_TOGGLE },
   */
  
  /*
   { XF86XK_Back,             0,   ACTION_NAV_BACK },
   { XF86XK_Forward,          0,   ACTION_NAV_FWD },
   { XF86XK_AudioPlay,        0,   ACTION_PLAYPAUSE },
   { XF86XK_AudioStop,        0,   ACTION_STOP },
   { XF86XK_AudioPrev,        0,   ACTION_PREV_TRACK },
   { XF86XK_AudioNext,        0,   ACTION_NEXT_TRACK },
   { XF86XK_Eject,            0,   ACTION_EJECT },
   { XF86XK_AudioMedia,       0,   ACTION_HOME },
   { XK_Menu,                 0,   ACTION_HOME },
   */
  
  { NSF1FunctionKey,         0,   ACTION_MENU },
  { NSF2FunctionKey,         0,   ACTION_SHOW_MEDIA_STATS },
  { NSF3FunctionKey,         0,   ACTION_SYSINFO },
  
  { NSF1FunctionKey,          NSShiftKeyMask,   ACTION_PREV_TRACK },
  { NSF2FunctionKey,          NSShiftKeyMask,   ACTION_PLAYPAUSE },
  { NSF3FunctionKey,          NSShiftKeyMask,   ACTION_NEXT_TRACK },
  { NSF4FunctionKey,          NSShiftKeyMask,   ACTION_STOP },
  
  { NSF5FunctionKey,          NSShiftKeyMask,   ACTION_VOLUME_DOWN },
  { NSF6FunctionKey,          NSShiftKeyMask,   ACTION_VOLUME_MUTE_TOGGLE },
  { NSF7FunctionKey,          NSShiftKeyMask,   ACTION_VOLUME_UP },
  
  { NSF1FunctionKey,          NSCommandKeyMask,   ACTION_SEEK_BACKWARD },
  { NSF3FunctionKey,          NSCommandKeyMask,   ACTION_SEEK_FORWARD },
  
  
  /*
   { XF86XK_Sleep,            0,           ACTION_SLEEP },
   */
  
  { _NSBackspaceKey,          0,                ACTION_BS, ACTION_NAV_BACK },
  { _NSEnterKey,              0,                ACTION_ENTER, ACTION_ACTIVATE},
  { _NSEscapeKey,             0,                ACTION_CANCEL },
  { _NSTabKey,                0,                ACTION_FOCUS_NEXT },
  
  { NSF5FunctionKey,	    0,	ACTION_RELOAD_UI },
};


static void glw_cocoa_set_fullscreen(void *opaque, int value);
static void glw_cocoa_in_fullwindow(void *opaque, int v);
static void glw_cocoa_dispatch_event(uii_t *uii, event_t *e);

@implementation GLWGLView

- (void)applicationWillTerminate:(NSNotification *)aNotification {
  showtime_shutdown(0);
}

/* delegated from window */
- (void)windowWillClose:(NSNotification *)aNotification {
  showtime_shutdown(0);
}

- (IBAction)clickIncreaseZoom:(id)sender {
  glw_cocoa_dispatch_event(&gcocoa.gr.gr_uii,
                           event_create_action(ACTION_ZOOM_UI_INCR));
}

- (IBAction)clickDecreaseZoom:(id)sender {
  glw_cocoa_dispatch_event(&gcocoa.gr.gr_uii,
                           event_create_action(ACTION_ZOOM_UI_DECR));
}

- (IBAction)clickFullscreen:(id)sender {
  glw_cocoa_dispatch_event(&gcocoa.gr.gr_uii,
                           event_create_action(ACTION_FULLSCREEN_TOGGLE));
}

- (IBAction)clickAbout:(id)sender {
  nav_open("page:about", NULL);
}

/* delegated from NSApplication */
- (BOOL)application:(NSApplication *)theApplication
           openFile:(NSString *)filename {
  extern char ***_NSGetArgv();
  extern int *_NSGetArgc();
  char **_argv = *_NSGetArgv();
  int _argc = *_NSGetArgc();
  const char *cfilename = [filename UTF8String];
  
  /* passing a command line argument will cause a call to openFile: so ignore
   * the first call and it is the same file as last argv argument */
  if(!gcocoa.skip_first_openfile_check) {
    gcocoa.skip_first_openfile_check = 1;
    
    if(_argc > 1 && strcmp(cfilename, _argv[_argc - 1]) == 0)
      return NO;
  }
  
  /* stringWithFormat uses autorelease */
  nav_open([[NSString stringWithFormat:@"file://%@", filename] UTF8String], NULL);
  
  return YES;
}

/* registered in initWithFrame */
- (void)handleGetURLEvent:(NSAppleEventDescriptor *)event
	   withReplyEvent:(NSAppleEventDescriptor *)replyEvent {
  nav_open([[[event descriptorAtIndex:1] stringValue] UTF8String], NULL);
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (void)becomeKeyWindow {
  if(gcocoa.is_fullwindow)
    [self glwDelayHideCursor];
}

- (void)resignKeyWindow {
  if(!gcocoa.is_fullwindow)
    [self glwUnHideCursor];
}

- (void)viewDidMoveToWindow {
  [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void)fullscreenLoop {
  NSOpenGLContext *fullScreenContext;  
  CGLContextObj cglContext;
  CGDisplayErr err;
  GLint oldSwapInterval;
  GLint newSwapInterval;
  
  NSOpenGLPixelFormatAttribute attrs[] = {
    NSOpenGLPFAFullScreen,
    NSOpenGLPFAScreenMask, CGDisplayIDToOpenGLDisplayMask(kCGDirectMainDisplay),
    NSOpenGLPFAColorSize, 24,
    NSOpenGLPFADepthSize, 16,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAAccelerated,
    0
  };
  
  /* allocate fullscreen context */
  NSOpenGLPixelFormat *pixelFormat = [[NSOpenGLPixelFormat alloc]
                                      initWithAttributes:attrs];    
  fullScreenContext =
  [[NSOpenGLContext alloc] initWithFormat:pixelFormat
                             shareContext:[self openGLContext]];
  [pixelFormat release];
  pixelFormat = nil;
  
  if(fullScreenContext == nil) {
    TRACE(TRACE_ERROR, "OpenGL", "failed to create fullscreen context");
    return;
  }
  
  err = CGCaptureAllDisplays();
  if(err != CGDisplayNoErr) {
    [fullScreenContext release];
    fullScreenContext = nil;
    TRACE(TRACE_ERROR, "OpenGL", "CGCaptureAllDisplays failed");
    return;
  }
  
  [self glwWindowedTimerStop];
  
  /* mouse events seams to get passed to apple menu bar in fullscreen
   * when using NSOpenGL */
  [NSMenu setMenuBarVisible:NO];
  
  /* go fullscreen and make switch to new gl context */
  [fullScreenContext setFullScreen];
  [fullScreenContext makeCurrentContext];
  
  /* save the current swap interval so we can restore it later, and then set
   * the new swap interval to lock us to the display's refresh rate. */
  cglContext = CGLGetCurrentContext();
  CGLGetParameter(cglContext, kCGLCPSwapInterval, &oldSwapInterval);
  newSwapInterval = 1;
  CGLSetParameter(cglContext, kCGLCPSwapInterval, &newSwapInterval);
  
  [self glwResize:CGDisplayPixelsWide(kCGDirectMainDisplay)
           height:CGDisplayPixelsHigh(kCGDirectMainDisplay)];
  
  /* Setup OpenGL state */
  glw_opengl_init_context(&gcocoa.gr);
  
  while(gcocoa.is_fullscreen) {
    /* dispatch events to the usual event methods */
    NSEvent *event;
    while((event = [NSApp nextEventMatchingMask:NSAnyEventMask
                                      untilDate:[NSDate distantPast]
                                         inMode:NSDefaultRunLoopMode
                                        dequeue:YES])) {
      switch ([event type]) {
        case NSKeyDown:
          [self keyDown:event];
          break;          
          
        case NSMouseMoved:
          [self mouseMoved:event];
          break;
          
        case NSLeftMouseDragged:
        case NSRightMouseDragged:
        case NSOtherMouseDragged:
          [self mouseDragged:event];
          break;
          
        case NSLeftMouseDown:
        case NSRightMouseDown:
        case NSOtherMouseDown:
          [self mouseDown:event];
          break;
          
        case NSLeftMouseUp:
        case NSRightMouseUp:
        case NSOtherMouseUp:
          [self mouseUp:event];
          break;
          
        case NSScrollWheel:
          [self scrollWheel:event];
          break;
          
        default:
          break;
      }
    }
    
    [self glwRender];
    [fullScreenContext flushBuffer];
  }
  
  /* make screen black before switching to non-fullscreen */
  glClearColor(0.0, 0.0, 0.0, 0.0);
  glClear(GL_COLOR_BUFFER_BIT);
  [fullScreenContext flushBuffer];
  glClear(GL_COLOR_BUFFER_BIT);
  [fullScreenContext flushBuffer];
  
  /* restore swap interval */
  CGLSetParameter(cglContext, kCGLCPSwapInterval, &oldSwapInterval);
  
  /* exit fullscreen mode and release our fullscreen context */
  [NSOpenGLContext clearCurrentContext];
  [fullScreenContext clearDrawable];
  [fullScreenContext release];
  fullScreenContext = nil;
  
  /* release control of the display */
  CGReleaseAllDisplays();
  
  [self setNeedsDisplay:YES];
  [self reshape];
  
  [[self window] setAcceptsMouseMovedEvents:YES];
  /* make sure window is focused when coming back */
  [[self window] makeKeyAndOrderFront:nil];
  
  [NSMenu setMenuBarVisible:YES];
  
  [self glwWindowedTimerStart];
}

- (void)glwResize:(int)width height:(int)height {
  /* could be called before prepareOpenGL */
  if(!gcocoa.glready)
    return;
  
  gcocoa.gr.gr_width  = width;
  gcocoa.gr.gr_height = height;
  
  glViewport(0, 0, width, height);
}

- (void)glwRender {    
  if(gcocoa.want_fullscreen != gcocoa.is_fullscreen) {
    gcocoa.is_fullscreen = gcocoa.want_fullscreen;
    
    glw_set_fullscreen(&gcocoa.gr, gcocoa.is_fullscreen);

    if(gcocoa.want_fullscreen) {
      [self fullscreenLoop];
      return;
    }
  }
  
  if(gcocoa.is_fullwindow && !gcocoa.is_cursor_hidden)
    [self glwDelayHideCursor];
  
  if(!gcocoa.is_fullwindow && gcocoa.is_cursor_hidden)
    [self glwUnHideCursor];
  
  glw_rctx_t rc;
  
  glw_lock(&gcocoa.gr);
  glw_prepare_frame(&gcocoa.gr, 0);
  
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  
  glw_rctx_init(&rc, gcocoa.gr.gr_width, gcocoa.gr.gr_height);
  glw_layout0(gcocoa.gr.gr_universe, &rc);
  glw_render0(gcocoa.gr.gr_universe, &rc);
  
  glw_unlock(&gcocoa.gr);
}

- (void)glwWindowedTimerStart {
  timer = [NSTimer timerWithTimeInterval:(0.001)
				  target:self
			        selector:@selector(glwWindowedTimer)
			        userInfo:nil repeats:YES];
  
  [[NSRunLoop currentRunLoop] addTimer:timer
                               forMode:NSDefaultRunLoopMode];
  [[NSRunLoop currentRunLoop] addTimer:timer
                               forMode:NSEventTrackingRunLoopMode];
  
  [timer retain];
  
  if(timer_cursor != nil) {
    [timer_cursor invalidate];
    [timer_cursor release];
    timer_cursor = nil;
  }
}

- (void)glwWindowedTimerStop {
  [timer invalidate];
  [timer release];  
}

- (void)glwWindowedTimer {
  /* force call to drawRect */
  [self setNeedsDisplay:YES];
}

- (void)glwHideCursor {
  glw_pointer_event_t gpe;
  
  [NSCursor setHiddenUntilMouseMoves:YES];
  
  gpe.type = GLW_POINTER_GONE;   
  glw_lock(&gcocoa.gr);
  glw_pointer_event(&gcocoa.gr, &gpe);
  glw_unlock(&gcocoa.gr);
}

- (void)glwDelayHideCursor {
  gcocoa.is_cursor_hidden = 1;
  
  if(mouse_down > 0)
    return;
  
  if(timer_cursor != nil) {
    [timer_cursor invalidate];
    [timer_cursor release];
    timer_cursor = nil;
  }
  
  timer_cursor = 
  [NSTimer scheduledTimerWithTimeInterval:(1.0) target:self
                                 selector:@selector(glwHideCursor)
                                 userInfo:nil
                                  repeats:NO];
  
  [timer_cursor retain];
}

- (void)glwUnHideCursor {
  gcocoa.is_cursor_hidden = 0;
  
  if(timer_cursor != nil) {
    [timer_cursor invalidate];
    [timer_cursor release];
    timer_cursor = nil;
  }
  
  [NSCursor setHiddenUntilMouseMoves:NO];
}

- (void)glwMouseEvent:(int)type event:(NSEvent*)event {  
  NSPoint loc = [event locationInWindow];
  glw_pointer_event_t gpe;
  
  if(gcocoa.is_cursor_hidden) 
    [self glwUnHideCursor];
  
  gpe.x = (2.0 * loc.x / gcocoa.gr.gr_width) - 1;
  gpe.y = (2.0 * loc.y / gcocoa.gr.gr_height) - 1;
  gpe.type = type;
  if(type == GLW_POINTER_SCROLL)
    gpe.delta_y = -[event deltaY];
  
  glw_lock(&gcocoa.gr);
  glw_pointer_event(&gcocoa.gr, &gpe);
  glw_unlock(&gcocoa.gr);
}

- (void)scrollWheel:(NSEvent *)event {
  [self glwMouseEvent:GLW_POINTER_SCROLL event:event];
}

- (void)glwEventFromMouseEvent:(NSEvent *)event {
  struct {
    int nsevent;
    int glw_event;
  } events[] = {
    {NSLeftMouseDown, GLW_POINTER_LEFT_PRESS},
    {NSLeftMouseUp, GLW_POINTER_LEFT_RELEASE},
    {NSRightMouseDown, GLW_POINTER_RIGHT_PRESS},
    {NSRightMouseUp, GLW_POINTER_RIGHT_RELEASE}
  };
  
  int i;
  for(i = 0; i < sizeof(events)/sizeof(events[0]); i++) {
    if(events[i].nsevent != [event type])
      continue;
    
    if([event type] == NSLeftMouseDown ||
       [event type] == NSRightMouseDown)
      mouse_down++;
    else
      mouse_down--;
    
    [self glwMouseEvent:events[i].glw_event event:event];
    return;
  }

  if([event type] == NSOtherMouseUp) {
    event_t *e = event_create_action(ACTION_MENU);
    glw_cocoa_dispatch_event(&gcocoa.gr.gr_uii, e);
  }
}

- (void)mouseDown:(NSEvent *)event {
  [self glwEventFromMouseEvent:event];
}

- (void)mouseMoved:(NSEvent *)event {
  [self glwMouseEvent:GLW_POINTER_MOTION_UPDATE event:event];
}

- (void)mouseDragged:(NSEvent *)event {
  [self glwMouseEvent:GLW_POINTER_MOTION_UPDATE event:event];
}
- (void)mouseUp:(NSEvent *)event {
  [self glwEventFromMouseEvent:event];
}

- (void)rightMouseDown:(NSEvent *)event {
  [self glwEventFromMouseEvent:event];
}

- (void)rightMouseDragged:(NSEvent *)event {
  [self glwMouseEvent:GLW_POINTER_MOTION_UPDATE event:event];
}
- (void)rightMouseUp:(NSEvent *)event {
  [self glwEventFromMouseEvent:event];
}

- (void)otherMouseDown:(NSEvent *)event {
  [self glwEventFromMouseEvent:event];
}

- (void)otherMouseDragged:(NSEvent *)event {
  [self glwMouseEvent:GLW_POINTER_MOTION_UPDATE event:event];
}

- (void)otherMouseUp:(NSEvent *)event {
  [self glwEventFromMouseEvent:event];
}

- (void)compositeClear {
  compositeKey = NO;
  if(compositeString) {
    [compositeString release];
    compositeString = nil;
  }
}

/* start of NSTextInput protocol */

- (NSArray *)validAttributesForMarkedText {
  static NSArray *a = nil;
  if(!a)
    a = [NSArray new];
  return a;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
- (unsigned int)characterIndexForPoint:(NSPoint)thePoint {
  return 0;
}
#else
- (NSUInteger)characterIndexForPoint:(NSPoint)thePoint {
  return 0;
}
#endif
- (NSRect)firstRectForCharacterRange:(NSRange)theRange {
  return NSZeroRect;
}

- (NSAttributedString *)attributedSubstringFromRange:(NSRange)theRange {
  static NSAttributedString *as = nil;
  if(!as)
    as = [NSAttributedString new];
  return as;
}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
- (long)conversationIdentifier {
  return (long)self;
}
#else
- (NSInteger)conversationIdentifier {
  return (NSInteger)self;
}
#endif

- (void)doCommandBySelector:(SEL)aSelector {  
}

- (void)setMarkedText:(id)aString selectedRange:(NSRange)selRange {
  NSString *s = aString;
  
  [self compositeClear];
  if([s length] == 0)
    return;
  
  compositeKey = YES;
  compositeString = [s copy];
}

- (BOOL)hasMarkedText {
  return compositeString != nil;
}

- (NSRange)markedRange {
  return NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange {
  return NSMakeRange(0, compositeString ? [compositeString length] : 0);
}

- (void)unmarkText {
  [self compositeClear];
}

/* end of NSTextInput protocol */

- (void)insertText:(id)aString {
  NSString *s = aString;
  int i;
  
  [self compositeClear];
  
  for(i = 0; i < [s length]; i++) {
    unichar uc = [s characterAtIndex:i];
    NSString *su = [[NSString alloc] initWithCharacters:&uc length:1];
    event_t *e = NULL;
    
    e = event_create_int(EVENT_UNICODE, uc);
    glw_cocoa_dispatch_event(&gcocoa.gr.gr_uii, e);
    [su release];
  }
}

- (void)keyDown:(NSEvent *)event {
  static NSMutableArray *eventArray;
  NSString *chars = [event characters];
  NSString *charsim = [event charactersIgnoringModifiers];
  
  if(compositeKey || [chars length] == 0  || [charsim length] == 0) {
    if(!eventArray)
      eventArray = [[NSMutableArray alloc] initWithCapacity:1];
    
    compositeKey = YES;
    [eventArray addObject:event];
    /* uses NSTextInput protocol and results in calls to insertText: */
    [self interpretKeyEvents:eventArray];
    [eventArray removeObject:event];
    return;
  }
  
  unichar c = [chars characterAtIndex:0];
  unichar cim = [[event charactersIgnoringModifiers] characterAtIndex:0];
  /* only care for some modifier keys */
  int mod = [event modifierFlags] & 
  (NSShiftKeyMask | NSCommandKeyMask |
   NSFunctionKeyMask | NSAlternateKeyMask);
  event_t *e = NULL;
  action_type_t av[3];
  int i;
  
  if((mod & ~NSShiftKeyMask) == 0 && (c == _NSSpaceKey || isgraph(c))) {
    e = event_create_int(EVENT_UNICODE, c);
  } else {
    for(i = 0; i < sizeof(keysym2action) / sizeof(keysym2action[0]); i++) {
      if(keysym2action[i].key == cim &&
         keysym2action[i].mod == (mod & ~NSFunctionKeyMask)) {
        av[0] = keysym2action[i].action1;
        av[1] = keysym2action[i].action2;
        av[2] = keysym2action[i].action3;
        
        if(keysym2action[i].action3 != ACTION_NONE)
          e = event_create_action_multi(av, 3);
        if(keysym2action[i].action2 != ACTION_NONE)
          e = event_create_action_multi(av, 2);
        else
          e = event_create_action_multi(av, 1);
        break;
      }
    }
  }
  
  if(e != NULL)
    glw_cocoa_dispatch_event(&gcocoa.gr.gr_uii, e);
}

- (void)reshape {
  NSRect bounds = [self bounds];
  [self glwResize:bounds.size.width height:bounds.size.height];
}

- (void)prepareOpenGL {
  gcocoa.glready = 1;
  
  timer_cursor = nil;
  
  /* must be called after GL is ready, calls GL functions */
  if(glw_init(&gcocoa.gr, SHOWTIME_GLW_DEFAULT_THEME_URL, NULL,
              gcocoa.start_ui, gcocoa.start_primary,
              "glw/cocoa/default", NULL))
    return;
  
  gcocoa.fullscreen_setting = 
  settings_create_bool(gcocoa.gr.gr_settings, "fullscreen", _p("Fullscreen mode"),
                       0, gcocoa.gr.gr_settings_store,
                       glw_cocoa_set_fullscreen, NULL,
                       SETTINGS_INITIAL_UPDATE, gcocoa.gr.gr_courier,
                       glw_settings_save, &gcocoa.gr);
  
  prop_subscribe(0,
                 PROP_TAG_NAME("ui","fullwindow"),
                 PROP_TAG_CALLBACK_INT, glw_cocoa_in_fullwindow, self,
                 PROP_TAG_ROOT, gcocoa.gr.gr_uii.uii_prop,
                 NULL);
  
  glw_load_universe(&gcocoa.gr);
  
  GLint v = 1;
  [[self openGLContext] setValues:&v forParameter:NSOpenGLCPSwapInterval];
  
  NSRect bounds = [self bounds];
  [self glwResize:bounds.size.width height:bounds.size.height];
  
  /* Setup OpenGL state */
  glw_opengl_init_context(&gcocoa.gr);
  
  [self glwWindowedTimerStart];  
}

- (void)drawRect:(NSRect)rect {
  [self glwRender];
  [[self openGLContext] flushBuffer];
}

- initWithFrame:(NSRect)frameRect {
  NSOpenGLPixelFormatAttribute attrs[] = {
    /* Specifying "NoRecovery" gives us a context that cannot fall back to the
     * software renderer.  This makes the View-based context a compatible with
     * the fullscreen context, enabling us to use the "shareContext" feature
     * to share textures, display lists, and other OpenGL objects between
     * the two.
     */
    NSOpenGLPFANoRecovery,
    NSOpenGLPFAColorSize, 24,
    NSOpenGLPFADepthSize, 16,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAAccelerated,
    0
  };
  
  NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc]
                                      initWithAttributes:attrs];
  
  if(!pixelFormat) {
    TRACE(TRACE_ERROR, "OpenGL", "failed to alloc hardware pixelformat");
    /* no reason to continue */
    exit(1);
  }
  
  [[NSAppleEventManager sharedAppleEventManager]
   setEventHandler:self
   andSelector:@selector(handleGetURLEvent:withReplyEvent:)
   forEventClass:kInternetEventClass andEventID:kAEGetURL];
  
  self = [super initWithFrame:frameRect pixelFormat:pixelFormat];
  
  return self;
}

@end

static void
glw_cocoa_set_fullscreen(void *opaque, int value)
{
  gcocoa.want_fullscreen = value;
}

static void
glw_cocoa_in_fullwindow(void *opaque, int value)
{
  gcocoa.is_fullwindow = value;
}

static void
glw_cocoa_screensaver_inhibit(CFRunLoopTimerRef timer, void *info)
{
  UpdateSystemActivity(OverallAct);
}

static int
glw_cocoa_start(ui_t *ui, prop_t *root, int argc, char *argv[], int primary)
{
  gcocoa.start_ui = ui;
  gcocoa.start_primary = primary;
  gcocoa.gr.gr_uii.uii_prop = root;

  CFRunLoopTimerRef timer;
  CFRunLoopTimerContext context = { 0, NULL, NULL, NULL, NULL };
  timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent(), 30, 0, 0,
                               glw_cocoa_screensaver_inhibit, &context);
  CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopCommonModes);
  
  NSApplicationMain(0, NULL);
  
  return 0;
}

static void
glw_cocoa_dispatch_event(uii_t *uii, event_t *e)
{
  glw_cocoa_t *gc = (glw_cocoa_t *)uii;
  
  if(event_is_action(e, ACTION_FULLSCREEN_TOGGLE)) {
    settings_toggle_bool(gc->fullscreen_setting);
  } else {
    glw_dispatch_event(uii, e);
  }
  event_release(e);
}

ui_t glw_ui = {
  .ui_title = "glw",
  .ui_start = glw_cocoa_start,
  .ui_dispatch_event = glw_cocoa_dispatch_event,
  /* NSApplicationMain must run in main thread */
  .ui_flags = UI_MAINTHREAD,
};
