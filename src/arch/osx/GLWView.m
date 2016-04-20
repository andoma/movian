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
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "main.h"
#include "osx.h"
#include "osx_c.h"
#include "src/ui/glw/glw.h"

@interface GLWView (hidden)

- (CVReturn)getFrameForTime:(const CVTimeStamp *)ot;
- (void)drawFrame;

@end


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


  { NSLeftArrowFunctionKey,   NSShiftKeyMask,   ACTION_MOVE_LEFT },
  { NSRightArrowFunctionKey,  NSShiftKeyMask,   ACTION_MOVE_RIGHT },
  { NSUpArrowFunctionKey,     NSShiftKeyMask,   ACTION_MOVE_UP },
  { NSDownArrowFunctionKey,   NSShiftKeyMask,   ACTION_MOVE_DOWN },

  { NSPageUpFunctionKey,      0, ACTION_PAGE_UP, ACTION_PREV_CHANNEL, ACTION_SKIP_BACKWARD },
  { NSPageDownFunctionKey,    0, ACTION_PAGE_DOWN, ACTION_NEXT_CHANNEL, ACTION_SKIP_FORWARD },
  { NSHomeFunctionKey,        0,                ACTION_TOP },
  { NSEndFunctionKey,         0,                ACTION_BOTTOM },

  { _NSShiftTabKey,           NSShiftKeyMask,   ACTION_FOCUS_PREV },

  { NSLeftArrowFunctionKey,   NSAlternateKeyMask, ACTION_NAV_BACK },
  { NSRightArrowFunctionKey,  NSAlternateKeyMask, ACTION_NAV_FWD },

  { NSLeftArrowFunctionKey,   NSControlKeyMask, ACTION_SKIP_BACKWARD },
  { NSRightArrowFunctionKey,  NSControlKeyMask, ACTION_SKIP_FORWARD },
  { NSUpArrowFunctionKey,     NSControlKeyMask, ACTION_VOLUME_UP },
  { NSDownArrowFunctionKey,   NSControlKeyMask, ACTION_VOLUME_DOWN },

  { NSLeftArrowFunctionKey,   NSControlKeyMask | NSShiftKeyMask, ACTION_SEEK_BACKWARD },
  { NSRightArrowFunctionKey,  NSControlKeyMask | NSShiftKeyMask , ACTION_SEEK_FORWARD },
  { NSDownArrowFunctionKey,   NSControlKeyMask | NSShiftKeyMask, ACTION_VOLUME_MUTE_TOGGLE },

  /* only used for fullscreen, in windowed mode we dont get events with
   * NSCommandKeyMask set */
  { '0',                      NSCommandKeyMask, ACTION_ZOOM_UI_RESET },
  { '+',                      NSCommandKeyMask, ACTION_ZOOM_UI_INCR },
  { '-',                      NSCommandKeyMask, ACTION_ZOOM_UI_DECR },
  { _NSEnterKey,              NSCommandKeyMask, ACTION_FULLSCREEN_TOGGLE },

  { _NSBackspaceKey,          0,                ACTION_BS, ACTION_NAV_BACK },
  { _NSEnterKey,              0,                ACTION_ENTER, ACTION_ACTIVATE},
  { _NSEnterKey,              NSShiftKeyMask,   ACTION_ITEMMENU },
  { _NSEscapeKey,             0,                ACTION_CANCEL,ACTION_NAV_BACK },
  { _NSTabKey,                0,                ACTION_FOCUS_NEXT},
};


@implementation GLWView


/**
 *
 */
static CVReturn
newframe(CVDisplayLinkRef displayLink, const CVTimeStamp *now,
	 const CVTimeStamp *outputTime, CVOptionFlags flagsIn,
	 CVOptionFlags *flagsOut, void *displayLinkContext)
{
  CVReturn result = [(GLWView *)displayLinkContext getFrameForTime:outputTime];
  return result;
}


/**
 *
 */
static void
glw_in_fullwindow(void *opaque, int val)
{
  GLWView *view = (GLWView *)opaque;
  view->in_full_window = val;
}


/**
 *
 */
- (void)hideCursor
{
  glw_pointer_event_t gpe = {0};

  if(!is_key_window)
    return;

  if(cursor_hidden)
    return;

  cursor_hidden = YES;
  [NSCursor hide];

  gpe.type = GLW_POINTER_GONE;
  glw_pointer_event(gr, &gpe);
}


/**
 *
 */
- (void)showCursor
{
  hide_cursor_at = gr->gr_time_usec + GLW_CURSOR_AUTOHIDE_TIME;

  if(!cursor_hidden)
    return;

  cursor_hidden = NO;
  [NSCursor unhide];
}


/**
 *
 */
- (void)autoHideCursor
{
  if(cursor_hidden)
    return;

  if(gr->gr_time_usec > hide_cursor_at) {
    [self hideCursor];
  }
}


/**
 *
 */
- (CVReturn)getFrameForTime:(const CVTimeStamp *)ot
{
  gr->gr_framerate = ot->rateScalar * ot->videoTimeScale /
    ot->videoRefreshPeriod;

  gr->gr_frameduration = 1000000.0 * ot->videoRefreshPeriod /
    (ot->rateScalar * ot->videoTimeScale);

  prop_set_float(prop_create(gr->gr_prop_ui, "framerate"), gr->gr_framerate);

  [self drawFrame];

  return kCVReturnSuccess;
}

- (void)glwMouseEvent:(int)type event:(NSEvent*)event {
  NSPoint loc = [self convertPointToBacking:[event locationInWindow]];
  glw_pointer_event_t gpe;

  gpe.screen_x = (2.0 * loc.x / gr->gr_width) - 1;
  gpe.screen_y = (2.0 * loc.y / gr->gr_height) - 1;
  gpe.type = type;

  gpe.ts = [event timestamp] * 1000000.0;

  switch(type) {
  case GLW_POINTER_SCROLL:
    {
      NSPoint p = {[event deltaX], [event deltaY]};
      NSPoint p2 = [self convertPointToBacking:p];

      gpe.delta_x = p2.x / 8;
      gpe.delta_y = -p2.y / 8;
      break;
    }

  case GLW_POINTER_FINE_SCROLL:
    {
      NSPoint p = {[event deltaX], [event deltaY]};
      NSPoint p2 = [self convertPointToBacking:p];
      gpe.delta_x = p2.x * 4;
      gpe.delta_y = p2.y * -4;
      break;
    }
  }

  glw_lock(gr);
  glw_pointer_event(gr, &gpe);
  glw_unlock(gr);
}

- (void)scrollWheel:(NSEvent *)event
{
  if([event hasPreciseScrollingDeltas]) {
    [self glwMouseEvent:GLW_POINTER_FINE_SCROLL event:event];
  } else {
    [self glwMouseEvent:GLW_POINTER_SCROLL event:event];
  }
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
    event_to_ui(e);
  }
}



- (void)mouseDown:(NSEvent *)event {
  [self glwEventFromMouseEvent:event];
}

- (void)mouseMoved:(NSEvent *)event {
  [self showCursor];
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

- (void)viewDidMoveToWindow {
  [[self window] setAcceptsMouseMovedEvents:YES];
}

/**
 *
 */
- (void)keyDown:(NSEvent *)event
{
  NSString *chars = [event characters];
  #if 0
  static NSMutableArray *eventArray;// static == bad
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
  #endif

  unichar c = [chars characterAtIndex:0];
  unichar cim = [[event charactersIgnoringModifiers] characterAtIndex:0];
  /* only care for some modifier keys */
  int mod = [event modifierFlags] &
  (NSShiftKeyMask | NSCommandKeyMask | NSControlKeyMask |
   NSFunctionKeyMask | NSAlternateKeyMask);

  event_t *e = NULL;
  action_type_t av[3];
  int i;

  for(i = 0; i < sizeof(keysym2action) / sizeof(keysym2action[0]); i++) {
    if(keysym2action[i].key == cim &&
       keysym2action[i].mod == (mod & ~NSFunctionKeyMask)) {
      av[0] = keysym2action[i].action1;
      av[1] = keysym2action[i].action2;
      av[2] = keysym2action[i].action3;

      if(keysym2action[i].action3 != ACTION_NONE)
	e = event_create_action_multi(av, 3);
      else if(keysym2action[i].action2 != ACTION_NONE)
	e = event_create_action_multi(av, 2);
      else
          e = event_create_action_multi(av, 1);
      break;
    }
  }

  if(e == NULL && cim >= NSF1FunctionKey && cim <= NSF35FunctionKey)
    e = event_from_Fkey(cim - NSF1FunctionKey + 1,
			mod & NSShiftKeyMask ? 1 : 0);

  if(e == NULL)
    e = event_create_int(EVENT_UNICODE, c);

  e->e_flags |= EVENT_KEYPRESS;

  [self hideCursor];

  prop_send_ext_event(eventSink, e);
  event_release(e);
}



- (BOOL)acceptsFirstResponder
{
  return YES;
}

- (void)windowDidMiniaturize:(NSNotification *)notification
{
  minimized = YES;
}

- (void)windowDidDeminiaturize:(NSNotification *)notification
{
  minimized = NO;
}

/**
 *
 */
- (void)reshape
{
  if(stopped)
    return;

  NSRect bb = [self convertRectToBacking:[self bounds]];

  glw_lock(gr);
  gr->gr_width = bb.size.width;
  gr->gr_height = bb.size.height;
  glw_need_refresh(gr, 0);
  glw_unlock(gr);

  NSOpenGLContext *cc = [self openGLContext];
  [cc makeCurrentContext];

  CGLLockContext((CGLContextObj)[cc CGLContextObj]);
  [[self openGLContext] update];
  CGLUnlockContext((CGLContextObj)[cc CGLContextObj]);
}


/**
 *
 */
- (void)drawRect:(NSRect)rect
{
  if(stopped)
    return;
  [self drawFrame];
}


/**
 *
 */
- (void)drawFrame
{
  NSOpenGLContext *currentContext = [self openGLContext];
  [currentContext makeCurrentContext];
  CGLLockContext((CGLContextObj)[currentContext CGLContextObj]);

  glw_lock(gr);

  if(in_full_window)
    [self autoHideCursor];

  glw_prepare_frame(gr, GLW_NO_FRAMERATE_UPDATE);

  int refresh = gr->gr_need_refresh;
  gr->gr_need_refresh = 0;

  if(!minimized && gr->gr_width > 1 && gr->gr_height > 1 && gr->gr_universe) {

    if(refresh) {
      glw_rctx_t rc;
      int zmax = 0;
      glw_rctx_init(&rc, gr->gr_width, gr->gr_height, 1, &zmax);
      glw_layout0(gr->gr_universe, &rc);

      if(refresh & GLW_REFRESH_FLAG_RENDER) {
        glViewport(0, 0, gr->gr_width, gr->gr_height);
        glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        glw_render0(gr->gr_universe, &rc);
      }
    }

    glw_unlock(gr);
    if(refresh & GLW_REFRESH_FLAG_RENDER)
      glw_post_scene(gr);

  } else {
    glw_unlock(gr);
  }

  if(refresh & GLW_REFRESH_FLAG_RENDER)
    [currentContext flushBuffer];

  CGLUnlockContext((CGLContextObj)[currentContext CGLContextObj]);
}


/**
 *
 */
- (void)stop
{
  stopped = YES;

  [self showCursor];

  CVDisplayLinkStop(m_displayLink);

  NSOpenGLContext *currentContext = [self openGLContext];
  [currentContext makeCurrentContext];

  CGLLockContext((CGLContextObj)[currentContext CGLContextObj]);

  glw_lock(gr);
  glw_flush(gr);
  glw_unlock(gr);

  CGLUnlockContext((CGLContextObj)[currentContext CGLContextObj]);

  [self showCursor];
  prop_unsubscribe(fullWindow);
  fullWindow = NULL;
}


/**
 *
 */
- (void)dealloc
{
  CVDisplayLinkRelease(m_displayLink);
  [super dealloc];
}


/**
 *
 */
- (void)becomeKeyWindow {
  is_key_window = YES;
  [[self window] setAcceptsMouseMovedEvents:YES];
}

- (void) resignKeyWindow {
  is_key_window = NO;
  [self showCursor];

  glw_pointer_event_t gpe = {0};
  gpe.type = GLW_POINTER_GONE;
  glw_pointer_event(gr, &gpe);
}

/**
 *
 */
- (id)initWithFrame:(NSRect)frameRect :(struct glw_root *)root :(bool)fs
{
  NSOpenGLPixelFormat *wpf;
  NSOpenGLPixelFormatAttribute attribs_windowed[] = {
    NSOpenGLPFAWindow,
    NSOpenGLPFAColorSize, 32,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFASingleRenderer,
    0 };

  NSOpenGLPixelFormatAttribute attribs_fs[] = {
    NSOpenGLPFADoubleBuffer,
    0 };

  wpf = [[NSOpenGLPixelFormat alloc]
	  initWithAttributes: fs ? attribs_fs : attribs_windowed];

  if(wpf == nil) {
    NSLog(@"Unable to create windowed pixel format.");
    exit(0);
  }
  self = [super initWithFrame:frameRect pixelFormat:wpf];
  if(self == nil) {
    NSLog(@"Unable to create a windowed OpenGL context.");
    exit(0);
  }

  [self setWantsBestResolutionOpenGLSurface:YES];

  [wpf release];

  gr = root;
  minimized = NO;
  eventSink = prop_create(gr->gr_prop_ui, "eventSink");

  fullWindow =
    prop_subscribe(0,
		   PROP_TAG_NAME("ui", "fullwindow"),
		   PROP_TAG_COURIER, gr->gr_courier,
		   PROP_TAG_CALLBACK_INT, glw_in_fullwindow, self,
		   PROP_TAG_ROOT, gr->gr_prop_ui,
		   NULL);

  GLint one = 1;
  [[self openGLContext] setValues:&one forParameter:NSOpenGLCPSwapInterval];

  CVDisplayLinkCreateWithActiveCGDisplays(&m_displayLink);
  CVDisplayLinkSetOutputCallback(m_displayLink, newframe, self);
  m_cgl_context = (CGLContextObj)[[self openGLContext] CGLContextObj];
  m_cgl_pixel_format =
    (CGLPixelFormatObj)[[self pixelFormat] CGLPixelFormatObj];

  CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext(m_displayLink,
                                                    m_cgl_context,
                                                    m_cgl_pixel_format);

  NSSize s = [self bounds].size;
  gr->gr_private = self;
  gr->gr_width = s.width;
  gr->gr_height = s.height;

  glw_opengl_init_context(gr);
  stopped = NO;

  CVDisplayLinkStart(m_displayLink);

  [self registerForDraggedTypes:[NSArray arrayWithObjects:NSFilenamesPboardType,NSURLPboardType,nil]];

  return self;
}



- (NSDragOperation)draggingEntered:(id <NSDraggingInfo>)sender
{
  return NSDragOperationLink;
}

- (BOOL)performDragOperation:(id)sender {
  NSPasteboard *pb = [sender draggingPasteboard];

  const char *u = NULL;

  NSArray *filenames = [pb propertyListForType:@"NSFilenamesPboardType"];
  if(filenames) {
    NSString *path = [filenames objectAtIndex:0];
    u = [path cStringUsingEncoding:NSUTF8StringEncoding];

  } else if([[pb types] containsObject:NSURLPboardType]) {
    NSArray *urls = [pb readObjectsForClasses:@[[NSURL class]] options:nil];
    NSURL *url = [urls objectAtIndex:0];
    u = [url.absoluteString cStringUsingEncoding:NSUTF8StringEncoding];

  } else {
    return NO;
  }

  event_t *e = event_create_openurl(u);
  prop_send_ext_event(eventSink, e);
  event_release(e);
  return YES;
}

/**
 *
 */
- (CGLContextObj)getCglContext {
  return m_cgl_context;
}


/**
 *
 */
- (CGLPixelFormatObj)getCglPixelFormat {
  return m_cgl_pixel_format;
}



@end


CGLContextObj
osx_get_cgl_context(glw_root_t *gr)
{
  GLWView *v = gr->gr_private;
  return [v getCglContext];
}

CGLPixelFormatObj
osx_get_cgl_pixel_format(glw_root_t *gr)
{
  GLWView *v = gr->gr_private;
  return [v getCglPixelFormat];
}
