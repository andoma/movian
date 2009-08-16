/*
 *  Cocoa UI
 *  Copyright (C) 2009 Mattias Wadman
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
#include "glw_video.h"
#include "misc/strtab.h"


typedef struct glw_cocoa {  
  glw_root_t gr;
  
  /* used to pass ui pointer from glw_cocoa_start to prepareOpenGL */
  ui_t *ui;
  
  int glready; /* prepareOpenGL has been run */
  char *config_name;  
  int running;
  int retcode;
  int primary;
  
  glw_t *universe;
  
  int window_width;
  int window_height;
  float aspect_ratio;
  
  int font_size;
  int want_font_size;
  int is_fullscreen;
  int want_fullscreen;

  int is_cursor_hidden;
  int is_fullwindow;
  
  int skip_first_openfile_check;
  
  setting_t *fullscreen_setting;
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
{ NSLeftArrowFunctionKey,   0,                ACTION_LEFT, ACTION_SEEK_BACKWARD },
{ NSRightArrowFunctionKey,  0,                ACTION_RIGHT, ACTION_SEEK_FORWARD },
{ NSUpArrowFunctionKey,     0,                ACTION_UP },
{ NSDownArrowFunctionKey,   0,                ACTION_DOWN },
{ NSPageUpFunctionKey,      0,                ACTION_PAGE_UP, ACTION_CHANNEL_PREV },
{ NSPageDownFunctionKey,    0,                ACTION_PAGE_DOWN, ACTION_CHANNEL_NEXT },
{ NSHomeFunctionKey,        0,                ACTION_TOP },
{ NSEndFunctionKey,         0,                ACTION_BOTTOM },

{ _NSBackspaceKey,          0,                ACTION_BS, ACTION_NAV_BACK },
{ _NSEnterKey,              0,                ACTION_ENTER },
{ _NSEscapeKey,             0,                ACTION_CLOSE },
{ _NSTabKey,                0,                ACTION_FOCUS_NEXT },
{ _NSShiftTabKey,           NSShiftKeyMask,   ACTION_FOCUS_PREV },

{ NSF11FunctionKey,         0,                ACTION_FULLSCREEN_TOGGLE },
{ 'f',                      NSCommandKeyMask, ACTION_FULLSCREEN_TOGGLE },


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

{ NSF1FunctionKey,          NSShiftKeyMask,   ACTION_PREV_TRACK },
{ NSF2FunctionKey,          NSShiftKeyMask,   ACTION_PLAYPAUSE },
{ NSF3FunctionKey,          NSShiftKeyMask,   ACTION_NEXT_TRACK },
{ NSF4FunctionKey,          NSShiftKeyMask,   ACTION_STOP },

{ NSF5FunctionKey,          NSShiftKeyMask,   ACTION_VOLUME_DOWN },
{ NSF6FunctionKey,          NSShiftKeyMask,   ACTION_VOLUME_MUTE_TOGGLE },
{ NSF7FunctionKey,          NSShiftKeyMask,   ACTION_VOLUME_UP },

/*
{ XF86XK_Sleep,            0,           ACTION_SLEEP },
 */
};





static void display_settings_init(glw_cocoa_t *gcocoa);
static void display_settings_save(glw_cocoa_t *gcocoa);
static void glw_cocoa_in_fullwindow(void *opaque, int v);
static void glw_cocoa_dispatch_event(uii_t *uii, event_t *e);

@implementation GLWGLView

- (void)applicationWillTerminate:(NSNotification *)aNotification {
  showtime_shutdown(0);
}

/* delegated from window */
- (BOOL)windowWillClose:(id)window {
  showtime_shutdown(0);
  return YES;
}

- (IBAction)clickFullscreen:(id)sender {
  settings_toggle_bool(gcocoa.fullscreen_setting);
}

- (IBAction)clickAbout:(id)sender {
  nav_open("page:about", NULL, NULL);
}

/* delegated from NSApplication */
- (BOOL)application:(NSApplication *)theApplication
           openFile:(NSString *)filename {
  extern char ***_NSGetArgv();
  extern int *_NSGetArgc();
  char **_argv = *_NSGetArgv();
  int _argc = *_NSGetArgc();
  const char *cfilename = [filename UTF8String];
  
  if(!gcocoa.skip_first_openfile_check) {
    gcocoa.skip_first_openfile_check = 1;
    
    if(_argc > 1 && strcmp(cfilename, _argv[_argc - 1]) == 0)
      return NO;
  }
  
  /* stringWithFormat uses autorelease */
  nav_open([[NSString stringWithFormat:@"file://%@", filename] UTF8String],
           NULL, NULL);
  
  return YES;
}

/* registered in initWithFrame */
- (void)handleGetURLEvent:(NSAppleEventDescriptor *)event
	   withReplyEvent:(NSAppleEventDescriptor *)replyEvent {
  nav_open([[[event descriptorAtIndex:1] stringValue] UTF8String],
	   NULL, NULL);
}


-(BOOL)acceptsFirstResponder {
  return YES;
}

-(BOOL)becomeFirstResponder {
  return YES;
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
  [self glwInit];
  
  while(gcocoa.is_fullscreen) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

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

    /* TODO: pool needed? does not call objc stuff */
    [pool release];
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
  
  [self glwWindowedTimerStart];
}

- (void)glwResize:(int)width height:(int)height {
  /* could be called before prepareOpenGL */
  if(!gcocoa.glready)
    return;
  
  gcocoa.window_width  = width;
  gcocoa.window_height = height;
  gcocoa.aspect_ratio = (float)width / (float)height;
  
  glViewport(0, 0, width, height);
}

- (void)glwInit {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glEnable(GL_TEXTURE_2D);
}

- (void)glwRender {
  if(gcocoa.font_size != gcocoa.want_font_size) {
    gcocoa.font_size = gcocoa.want_font_size;
    glw_lock(&gcocoa.gr);
    glw_font_change_size(&gcocoa.gr, gcocoa.font_size);
    glw_unlock(&gcocoa.gr);
    display_settings_save(&gcocoa);
  }
    
  if(gcocoa.want_fullscreen != gcocoa.is_fullscreen) {
    gcocoa.is_fullscreen = gcocoa.want_fullscreen;
    display_settings_save(&gcocoa);
    
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
  
  glw_reaper0(&gcocoa.gr);
  
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  
  memset(&rc, 0, sizeof(rc));
  rc.rc_size_x = gcocoa.window_width;
  rc.rc_size_y = gcocoa.window_height;
  rc.rc_fullwindow = 1;
  glw_layout0(gcocoa.gr.gr_universe, &rc);
  
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);
  
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  
  gluLookAt(0, 0, 1 / tan(45 * M_PI / 360),
            0, 0, 1,
            0, 1, 0);
  
  rc.rc_alpha = 1.0f;
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
 
  if(timer_cursor) {
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
  [NSCursor setHiddenUntilMouseMoves:YES];
}

- (void)glwDelayHideCursor {
  gcocoa.is_cursor_hidden = 1;
  
  if(timer_cursor) {
    [timer_cursor invalidate];
    [timer_cursor release];
    timer_cursor = nil;
  }

  timer_cursor = 
    [NSTimer scheduledTimerWithTimeInterval:(1.0) target:self
      selector:@selector(glwHideCursor) userInfo:nil repeats:NO];

  [timer_cursor retain];
}

- (void)glwUnHideCursor {
  gcocoa.is_cursor_hidden = 0;
  
  if(timer_cursor) {
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
  
  gpe.x = (2.0 * loc.x / gcocoa.window_width ) - 1;
  gpe.y = (2.0 * loc.y / gcocoa.window_height) - 1;
  gpe.type = type;
  if(type == GLW_POINTER_SCROLL)
    gpe.delta_y = -[event deltaY];
  
  glw_lock(&gcocoa.gr);
  glw_pointer_event(&gcocoa.gr, &gpe);
  glw_unlock(&gcocoa.gr);
}

- (void)scrollWheel:(NSEvent*)event {
  [self glwMouseEvent:GLW_POINTER_SCROLL event:event];
}

-(void)mouseMoved:(NSEvent *)event {
  [self glwMouseEvent:GLW_POINTER_MOTION event:event];
}

-(void)mouseDragged:(NSEvent*)event {
  [self glwMouseEvent:GLW_POINTER_MOTION event:event];
}

- (void)mouseDown:(NSEvent *)event {
  [self glwMouseEvent:GLW_POINTER_CLICK event:event];
}

- (void)mouseUp:(NSEvent *)event {
  [self glwMouseEvent:GLW_POINTER_RELEASE event:event];
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

- (unsigned int)characterIndexForPoint:(NSPoint)thePoint {
  return 0;
}

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

    e = event_create_unicode(uc);
    glw_cocoa_dispatch_event(&gcocoa.gr.gr_uii, e);
    //ui_dispatch_event(e, [su UTF8String], &gcocoa.gr.gr_uii);
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
  int mod = [event modifierFlags] & (NSShiftKeyMask | NSCommandKeyMask);
  event_t *e = NULL;
  int i;
  action_type_t av[3];
    
  if((mod & ~NSShiftKeyMask) == 0 && (c == _NSSpaceKey || isgraph(c))) {
    e = event_create_unicode(c);
  } else {
    for(i = 0; i < sizeof(keysym2action) / sizeof(keysym2action[0]); i++) {
      if(keysym2action[i].key == cim &&
         keysym2action[i].mod == mod) {

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
  const char *theme_path = SHOWTIME_GLW_DEFAULT_THEME_URL;
  GLint v = 1;
  
  gcocoa.glready = 1;

  timer_cursor = nil;

  /* default font size */
  gcocoa.want_font_size = 40;
    
  gcocoa.config_name = strdup("glw/cocoa/default");
    
  /* must be called after GL is ready, calls GL functions */
  if(glw_init(&gcocoa.gr, gcocoa.want_font_size, theme_path, gcocoa.ui,
	      gcocoa.primary))
    return;
  
  prop_subscribe(0,
		 PROP_TAG_NAME("ui","fullwindow"),
		 PROP_TAG_CALLBACK_INT, glw_cocoa_in_fullwindow, self,
		 PROP_TAG_ROOT, gcocoa.gr.gr_uii.uii_prop,
		 NULL);
  
  [[self openGLContext] setValues:&v forParameter:NSOpenGLCPSwapInterval];
  
  NSRect bounds = [self bounds];
  [self glwResize:bounds.size.width height:bounds.size.height];
  [self glwInit];
  
  /* Load fragment shaders */
  glw_video_global_init(&gcocoa.gr);

  display_settings_init(&gcocoa);
  
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

/**
 * Switch displaymode, we just set a variable and let mainloop switch
 * later on
 */
static void
display_set_mode(void *opaque, int value)
{
  glw_cocoa_t *gc = opaque;
  gc->want_fullscreen = value;
}

/**
 * Change font size
 */
static void
display_set_fontsize(void *opaque, int value)
{
  glw_cocoa_t *gc = opaque;
  gc->want_font_size = value;
}

/**
 * Add a settings pane with relevant settings
 */
static void
display_settings_init(glw_cocoa_t *gc)
{
  prop_t *r;
  htsmsg_t *settings = htsmsg_store_load("displays/%s", gc->config_name);

  r = settings_add_dir(NULL, "display",
                       "Display settings for GLW/Cocoa", "display");
  
  gc->fullscreen_setting =
    settings_add_bool(r, "fullscreen", "Fullscreen mode", 0, settings,
                      display_set_mode, gc,
                      SETTINGS_INITIAL_UPDATE);
      
  settings_add_int(r, "fontsize",
		   "Font size", 20, settings, 14, 40, 1,
		   display_set_fontsize, gc,
		   SETTINGS_INITIAL_UPDATE, "px");
  
  htsmsg_destroy(settings);  
}

/**
 * Save display settings
 */
static void
display_settings_save(glw_cocoa_t *gc)
{
  htsmsg_t *m = htsmsg_create_map();
  
  htsmsg_add_u32(m, "fullscreen", gc->want_fullscreen);
  htsmsg_add_u32(m, "fontsize",   gc->want_font_size);
  
  htsmsg_store_save(m, "displays/%s", gc->config_name);
  htsmsg_destroy(m);
}

static void
glw_cocoa_screensaver_inhibit(CFRunLoopTimerRef timer, void *info)
{
  UpdateSystemActivity(OverallAct);
}

static void
glw_cocoa_in_fullwindow(void *opaque, int v)
{
  gcocoa.is_fullwindow = v;
}

static int
glw_cocoa_start(ui_t *ui, int argc, char *argv[], int primary)
{
  gcocoa.ui = ui;
  gcocoa.primary = primary;

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
    event_unref(e);
  } else {
    glw_dispatch_event(uii, e);
  }
}


ui_t glw_ui = {
  .ui_title = "glw",
  .ui_start = glw_cocoa_start,
  .ui_dispatch_event = glw_cocoa_dispatch_event,
  .ui_flags = UI_MAINTHREAD,
};
