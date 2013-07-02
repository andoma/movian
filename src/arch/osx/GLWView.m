/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2012 Andreas Öman
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

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "showtime.h"
#include "osx.h"
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

  { NSPageUpFunctionKey,      0,                ACTION_PAGE_UP, ACTION_NEXT_CHANNEL },
  { NSPageDownFunctionKey,    0,                ACTION_PAGE_DOWN, ACTION_PREV_CHANNEL },
  { NSHomeFunctionKey,        0,                ACTION_TOP },
  { NSEndFunctionKey,         0,                ACTION_BOTTOM },
  
  { _NSShiftTabKey,           NSShiftKeyMask,   ACTION_FOCUS_PREV },
  
  { NSLeftArrowFunctionKey,   NSAlternateKeyMask, ACTION_NAV_BACK },
  { NSRightArrowFunctionKey,  NSAlternateKeyMask, ACTION_NAV_FWD },
  
  { NSLeftArrowFunctionKey,   NSCommandKeyMask, ACTION_SEEK_BACKWARD }, 
  { NSRightArrowFunctionKey,  NSCommandKeyMask, ACTION_SEEK_FORWARD }, 
  
  { NSLeftArrowFunctionKey,   NSShiftKeyMask|NSCommandKeyMask, ACTION_SKIP_BACKWARD }, 
  { NSRightArrowFunctionKey,  NSShiftKeyMask|NSCommandKeyMask, ACTION_SKIP_FORWARD }, 
  
  /* only used for fullscreen, in windowed mode we dont get events with
   * NSCommandKeyMask set */
  { '+',                      NSCommandKeyMask, ACTION_ZOOM_UI_INCR },
  { '-',                      NSCommandKeyMask, ACTION_ZOOM_UI_DECR },
  { 'f',                      NSCommandKeyMask, ACTION_FULLSCREEN_TOGGLE },
  
  { _NSBackspaceKey,          0,                ACTION_BS, ACTION_NAV_BACK },
  { _NSEnterKey,              0,                ACTION_ENTER, ACTION_ACTIVATE},
  { _NSEnterKey,              NSShiftKeyMask,   ACTION_ITEMMENU },
  { _NSEscapeKey,             0,                ACTION_CANCEL },
  { _NSTabKey,                0,                ACTION_FOCUS_NEXT },
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
  (NSShiftKeyMask | NSCommandKeyMask |
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
      if(keysym2action[i].action2 != ACTION_NONE)
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

  NSSize s = [self bounds].size;

  gr->gr_width = s.width;
  gr->gr_height = s.height;

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

  glViewport(0, 0, gr->gr_width, gr->gr_height);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  
  glw_prepare_frame(gr, GLW_NO_FRAMERATE_UPDATE);
  
  if(!minimized && gr->gr_width > 1 && gr->gr_height > 1 && gr->gr_universe) {

    glw_rctx_t rc;
    glw_rctx_init(&rc, gr->gr_width, gr->gr_height, 1);
    glw_layout0(gr->gr_universe, &rc);
    glw_render0(gr->gr_universe, &rc);

    glw_unlock(gr);
    glw_post_scene(gr);

  } else {
    glw_unlock(gr);
  }
  [currentContext flushBuffer];
    
  CGLUnlockContext((CGLContextObj)[currentContext CGLContextObj]);
}


/**
 *
 */
- (void)stop
{
  stopped = YES;

  CVDisplayLinkStop(m_displayLink);

  NSOpenGLContext *currentContext = [self openGLContext];
  [currentContext makeCurrentContext];
  
  CGLLockContext((CGLContextObj)[currentContext CGLContextObj]);

  glw_lock(gr);
  glw_flush(gr);
  glw_unlock(gr);

  CGLUnlockContext((CGLContextObj)[currentContext CGLContextObj]);
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
    NSOpenGLPFAAccelerated,
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

  [wpf release];
  
  gr = root;
  minimized = NO;
  eventSink = prop_create(gr->gr_prop_ui, "eventSink");

  GLint one = 1;
  [[self openGLContext] setValues:&one forParameter:NSOpenGLCPSwapInterval];
  
  CVDisplayLinkCreateWithActiveCGDisplays(&m_displayLink);
  CVDisplayLinkSetOutputCallback(m_displayLink, newframe, self);
  CGLContextObj cglc = (CGLContextObj)[[self openGLContext] CGLContextObj];
  CGLPixelFormatObj pf = 
    (CGLPixelFormatObj)[[self pixelFormat] CGLPixelFormatObj];
  CVDisplayLinkSetCurrentCGDisplayFromOpenGLContext(m_displayLink, cglc, pf);

  NSSize s = [self bounds].size;
  gr->gr_width = s.width;
  gr->gr_height = s.height;

  glw_opengl_init_context(gr);
  stopped = NO;

  CVDisplayLinkStart(m_displayLink);
  return self;
}


@end

