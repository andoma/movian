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
#pragma once
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "prop/prop.h"

extern prop_courier_t *mainloop_courier;

void webpopup_init(void);

/**
 *
 */
@interface GLWView : NSOpenGLView  <NSWindowDelegate>
{
  struct glw_root *gr;
  struct prop *eventSink;
  struct prop_sub *fullWindow;
  bool minimized;
  bool compositeKey;
  bool stopped;
  bool cursor_hidden;
  bool in_full_window;
  bool is_key_window;
  int mouse_down;
  int autohide_counter;
  CVDisplayLinkRef m_displayLink;
  CGLContextObj m_cgl_context;
  CGLPixelFormatObj m_cgl_pixel_format;
  int64_t hide_cursor_at;
}
- (id)initWithFrame:(NSRect)frameRect :(struct glw_root *)gr;
- (void)stop;

@end


/**
 *
 */
@interface GLWUI : NSObject <NSWindowDelegate>
{
  struct glw_root *gr;
  NSWindow *window;
  GLWView *view;
  bool minimized;

  CFRunLoopTimerRef timer;

  prop_sub_t *evsub;  // Even sink

  bool fullwindow;
  prop_sub_t *fwsub;  // Full window
  NSString *title;
}

- (void)openWin;
- (void)toggleFullscreen;
- (void)setFullWindow:(BOOL)on;
- (void)windowWillEnterFullScreen:(NSNotification *)notification;
- (void)windowWillExitFullScreen:(NSNotification *)notification;

@end
