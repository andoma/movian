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
#include "ui/keymapper.h"
#include "strtab.h"


struct glw_cocoa {  
  glw_root_t gr;
  char *config_name;
  
  int running;
  
  glw_t *universe;
  
  int window_width;
  int window_height;
  float aspect_ratio;
  
  int font_size;
  int want_font_size;
  int is_fullscreen;
  int want_fullscreen;
  int is_pointer_enabled;
  int want_pointer_enabled;
  
  setting_t *fullscreen_setting;
};

/* used to pass ui pointer from glw_cocoa_start to prepareOpenGL */
static ui_t *gcocoa_ui;
static int gcocoa_primary;

static const keymap_defmap_t glw_default_keymap[] = {
  {EVENT_NONE, NULL},
};

/* based on NSEvent function key enums */
static struct strtab function_key_map[] = {
  {"Up arrow", 0xF700},
  {"Down arrow", 0xF701},
  {"Left arrow", 0xF702},
  {"Right arrow", 0xF703},
  {"F1", 0xF704},
  {"F2", 0xF705},
  {"F3", 0xF706},
  {"F4", 0xF707},
  {"F5", 0xF708},
  {"F6", 0xF709},
  {"F7", 0xF70A},
  {"F8", 0xF70B},
  {"F9", 0xF70C},
  {"F10", 0xF70D},
  {"F11", 0xF70E},
  {"F12", 0xF70F},
  {"F13", 0xF710},
  {"F14", 0xF711},
  {"F15", 0xF712},
  {"F16", 0xF713},
  {"F17", 0xF714},
  {"F18", 0xF715},
  {"F19", 0xF716},
  {"F20", 0xF717},
  {"F21", 0xF718},
  {"F22", 0xF719},
  {"F23", 0xF71A},
  {"F24", 0xF71B},
  {"F25", 0xF71C},
  {"F26", 0xF71D},
  {"F27", 0xF71E},
  {"F28", 0xF71F},
  {"F29", 0xF720},
  {"F30", 0xF721},
  {"F31", 0xF722},
  {"F32", 0xF723},
  {"F33", 0xF724},
  {"F34", 0xF725},
  {"F35", 0xF726},
  {"Insert", 0xF727},
  {"Delete", 0xF728},
  {"Home", 0xF729},
  {"Begin", 0xF72A},
  {"End", 0xF72B},
  {"Page up", 0xF72C},
  {"Page down", 0xF72D},
  {"Print screen", 0xF72E},
  {"Scroll lock", 0xF72F},
  {"Pause", 0xF730},
  {"SysReq", 0xF731},
  {"Break", 0xF732},
  {"Reset", 0xF733},
  {"Stop", 0xF734},
  {"Menu", 0xF735},
  {"User", 0xF736},
  {"System", 0xF737},
  {"Print", 0xF738},
  {"Clear line", 0xF739},
  {"Clear display", 0xF73A},
  {"Insert line", 0xF73B},
  {"Delete line", 0xF73C},
  {"Insert char", 0xF73D},
  {"Delete char", 0xF73E},
  {"Prev", 0xF73F},
  {"Next", 0xF740},
  {"Select", 0xF741},
  {"Execute", 0xF742},
  {"Undo", 0xF743},
  {"Redo", 0xF744},
  {"Find", 0xF745},
  {"Help", 0xF746},
  {"Mode switch", 0xF747}
};


static void display_settings_init(glw_cocoa_t *gcocoa);
static void display_settings_save(glw_cocoa_t *gcocoa);
static int refresh_rate();

/* based on example from:
 * http://developer.apple.com/documentation/Performance/Conceptual/Drawing/Articles/FlushingContent.html
 */
static int
refresh_rate()
{
  CFDictionaryRef mi;  
  int rate = 60; /* assume LCD screen */
  
  mi = CGDisplayCurrentMode(CGMainDisplayID());
  
  if(mi) {
    CFNumberRef v =
    (CFNumberRef)CFDictionaryGetValue(mi, kCGDisplayRefreshRate);
    
    if(v) {
      CFNumberGetValue(v, kCFNumberIntType, &rate);

      if(rate == 0)
        rate = 60;
    }
  }
  
  return rate;
}


@implementation GLWGLView

/* delegated from window */
- (void)windowWillClose:(NSNotification *)notification {
  [NSApp terminate:self];
}

- (IBAction)clickFullscreen:(id)sender {
  settings_toggle_bool(gcocoa->fullscreen_setting);
}

- (IBAction)clickAbout:(id)sender {
  nav_open("page://about", 0);
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
    TRACE(TRACE_ERROR, "opengl", "failed to create fullscreen context");
    return;
  }
  
  err = CGCaptureAllDisplays();
  if(err != CGDisplayNoErr) {
    [fullScreenContext release];
    fullScreenContext = nil;
    TRACE(TRACE_ERROR, "opengl", "CGCaptureAllDisplays failed");
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
  
  while(gcocoa->is_fullscreen) {
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
  if(!gcocoa)
    return;
  
  gcocoa->window_width  = width;
  gcocoa->window_height = height;
  gcocoa->aspect_ratio = (float)width / (float)height;
  
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
  if(gcocoa->font_size != gcocoa->want_font_size) {
    gcocoa->font_size = gcocoa->want_font_size;
    glw_lock(&gcocoa->gr);
    glw_font_change_size(&gcocoa->gr, gcocoa->font_size);
    glw_unlock(&gcocoa->gr);
    display_settings_save(gcocoa);
  }
  
  if(gcocoa->want_pointer_enabled != gcocoa->is_pointer_enabled) {
    gcocoa->is_pointer_enabled = gcocoa->want_pointer_enabled;
    if(gcocoa->want_pointer_enabled)
      [NSCursor unhide];
    else
      [NSCursor hide];
    display_settings_save(gcocoa);
  }
  
  if(gcocoa->want_fullscreen != gcocoa->is_fullscreen) {
    gcocoa->is_fullscreen = gcocoa->want_fullscreen;
    display_settings_save(gcocoa);
    
    if(gcocoa->want_fullscreen) {
      [self fullscreenLoop];
      return;
    }
  }

  glw_rctx_t rc;
  
  glw_lock(&gcocoa->gr);
  
  glw_reaper0(&gcocoa->gr);
  
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  
  memset(&rc, 0, sizeof(rc));
  rc.rc_size_x = gcocoa->window_width;
  rc.rc_size_y = gcocoa->window_height;
  rc.rc_fullscreen = 1;
  glw_layout0(gcocoa->gr.gr_universe, &rc);
  
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);
  
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  
  gluLookAt(0, 0, 1 / tan(45 * M_PI / 360),
            0, 0, 1,
            0, 1, 0);
  
  rc.rc_alpha = 1.0f;
  glw_render0(gcocoa->gr.gr_universe, &rc);
    
  glw_unlock(&gcocoa->gr);
}

- (void)glwWindowedTimerStart {
  timer = [NSTimer scheduledTimerWithTimeInterval:(1.0/refresh_rate())
                                           target:self
                                         selector:@selector(glwWindowedTimer)
                                         userInfo:nil repeats:YES];
  [timer retain];
}

- (void)glwWindowedTimerStop {
  [timer invalidate];
  [timer release];
}

- (void)glwWindowedTimer {
  /* force call to drawRect */
  [self setNeedsDisplay:YES];
}

- (void)glwMouseEvent:(int)type event:(NSEvent*)event {
  if(!gcocoa->is_pointer_enabled)
    return;
  
  NSPoint loc = [event locationInWindow];
  glw_pointer_event_t gpe;
  
  gpe.x = (2.0 * loc.x / gcocoa->window_width ) - 1;
  gpe.y = (2.0 * loc.y / gcocoa->window_height) - 1;
  gpe.type = type;
  if(type == GLW_POINTER_SCROLL)
    gpe.delta_y = -[event deltaY];
  
  glw_lock(&gcocoa->gr);
  glw_pointer_event(&gcocoa->gr, &gpe);
  glw_unlock(&gcocoa->gr);  
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

- (void)keyDown:(NSEvent *)event {
  unichar c = [[event characters] characterAtIndex:0];
  unichar cim = [[event charactersIgnoringModifiers] characterAtIndex:0];
  int mod = [event modifierFlags];
  char buf[64], buf2[64];
  event_t *e = NULL;
  
  /* command+f is always available */
  if(gcocoa->is_fullscreen && c == 'f' && mod & NSCommandKeyMask) {
    settings_toggle_bool(gcocoa->fullscreen_setting);
    return;
  }

  switch(cim) {
    case NSRightArrowFunctionKey: e = event_create_simple(EVENT_RIGHT); break;
    case NSLeftArrowFunctionKey: e = event_create_simple(EVENT_LEFT); break;
    case NSUpArrowFunctionKey: e = event_create_simple(EVENT_UP); break;
    case NSDownArrowFunctionKey: e = event_create_simple(EVENT_DOWN); break;
    case 8: e = event_create_simple(EVENT_BACKSPACE); break;
    case 127 /* delete */ : e = event_create_simple(EVENT_BACKSPACE); break;
    case 13: e = event_create_simple(EVENT_ENTER); break;
    case 27 /* esc */: e = event_create_simple(EVENT_CLOSE); break;
    case 9 /* tab */: e = event_create_simple(EVENT_FOCUS_NEXT); break;
    case 25 /* shift+tab */: e = event_create_simple(EVENT_FOCUS_PREV); break;
    default:
      break;
  }
  
  if(e) {
    ui_dispatch_event(e, NULL, &gcocoa->gr.gr_uii);
    return;
  }
  
  if(val2str(cim, function_key_map) != NULL)
    strcpy(buf2, val2str(cim, function_key_map));
  else if(isgraph(cim))
    snprintf(buf2, sizeof(buf2), "%c", cim);
  else
    snprintf(buf2, sizeof(buf2), "%d", cim);
  
  snprintf(buf, sizeof(buf),
           "%s%s%s%s%s",
           mod & NSShiftKeyMask ? "Shift - " : "",
           mod & NSAlternateKeyMask ? "Alt - ": "",
           mod & NSControlKeyMask ? "Ctrl - " : "",
           mod & NSCommandKeyMask ? "Command - " : "",
           buf2);
  
  e = event_create_unicode(c);
  ui_dispatch_event(e, buf, &gcocoa->gr.gr_uii);
}

- (void)reshape {
  NSRect bounds = [self bounds];
  [self glwResize:bounds.size.width height:bounds.size.height];
}

- (void)prepareOpenGL {
  gcocoa = calloc(1, sizeof(glw_cocoa_t));
  const char *theme_path = SHOWTIME_DEFAULT_THEME_URL;
  GLint v = 1;
  
  /* default cursor is on at start */
  gcocoa->is_pointer_enabled = 1;
  gcocoa->want_font_size = 40;
    
  gcocoa->config_name = strdup("glw/cocoa/default");
    
  /* must be called after GL is ready, calls GL functions */
  if(glw_init(&gcocoa->gr, gcocoa->want_font_size, theme_path, gcocoa_ui,
	      gcocoa_primary))
    return;
  
  [[self openGLContext] setValues:&v forParameter:NSOpenGLCPSwapInterval];
  
  NSRect bounds = [self bounds];
  [self glwResize:bounds.size.width height:bounds.size.height];
  [self glwInit];
  
  /* Load fragment shaders */
  glw_video_global_init(&gcocoa->gr);

  display_settings_init(gcocoa);
  
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
    TRACE(TRACE_ERROR, "opengl", "failed to alloc hardware pixelformat");
    /* no reason to continue */
    exit(1);
  }
    
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
  glw_cocoa_t *gcocoa   = opaque;
  gcocoa->want_fullscreen = value;
}

/**
 * Switch pointer on/off
 */
static void
display_set_pointer(void *opaque, int value)
{
  glw_cocoa_t *gcocoa = opaque;
  gcocoa->want_pointer_enabled = value;
}

/**
 * Change font size
 */
static void
display_set_fontsize(void *opaque, int value)
{
  glw_cocoa_t *gcocoa = opaque;
  gcocoa->want_font_size = value;
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
    
  settings_add_bool(r, "pointer",
		    "Mouse pointer", 1, settings,
		    display_set_pointer, gc,
		    SETTINGS_INITIAL_UPDATE);
  
  settings_add_int(r, "fontsize",
		   "Font size", 20, settings, 14, 40, 1,
		   display_set_fontsize, gc,
		   SETTINGS_INITIAL_UPDATE, "px");
  
  htsmsg_destroy(settings);
  
  gc->gr.gr_uii.uii_km =
    keymapper_create(r, gc->config_name, "Keymap", glw_default_keymap);
}

/**
 * Save display settings
 */
static void
display_settings_save(glw_cocoa_t *gcocoa)
{
  htsmsg_t *m = htsmsg_create_map();
  
  htsmsg_add_u32(m, "fullscreen", gcocoa->want_fullscreen);
  htsmsg_add_u32(m, "pointer",    gcocoa->want_pointer_enabled);
  htsmsg_add_u32(m, "fontsize",   gcocoa->want_font_size);
  
  htsmsg_store_save(m, "displays/%s", gcocoa->config_name);
  htsmsg_destroy(m);
}

static void
glw_cocoa_screensaver_inhibit(CFRunLoopTimerRef timer, void *info)
{
  UpdateSystemActivity(OverallAct);
}

static int
glw_cocoa_start(ui_t *ui, int argc, char *argv[], int primary)
{
  gcocoa_ui = ui; 
  gcocoa_primary = primary;
  CFRunLoopTimerRef timer;
  CFRunLoopTimerContext context = { 0, NULL, NULL, NULL, NULL };
  
  timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent(), 30, 0, 0,
                               glw_cocoa_screensaver_inhibit, &context);
  CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopCommonModes);
  
  NSApplicationMain(0, NULL);
  
  return 0;
}

static int
glw_cocoa_dispatch_event(uii_t *uii, event_t *e)
{
  glw_cocoa_t *gcocoa = (glw_cocoa_t *)uii;
  
  switch(e->e_type) {
    case EVENT_FULLSCREEN_TOGGLE:
      settings_toggle_bool(gcocoa->fullscreen_setting);
      return 1;
      
    default:
      return glw_dispatch_event(uii, e);
  }
}

ui_t glw_ui = {
  .ui_title = "glw",
  .ui_start = glw_cocoa_start,
  .ui_dispatch_event = glw_cocoa_dispatch_event,
  .ui_flags = UI_MAINTHREAD,
};
