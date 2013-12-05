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

/* 
 * This could be in glw_cocoa.m but interface builder seams to only read
 * interface definitions from header files
 */

#ifndef GLW_COCOA_H
#define GLW_COCOA_H

#import <Cocoa/Cocoa.h>


@interface GLWGLView: NSOpenGLView <NSTextInput>
{
  NSTimer *timer;
  NSTimer *timer_cursor;
  int mouse_down;
  bool compositeKey;
  NSString *compositeString;
  bool fullwindow;
}
- (void)glwResize:(int)width height:(int)height;
- (void)glwRender;
- (void)glwWindowedTimerStart;
- (void)glwWindowedTimerStop;
- (void)glwWindowedTimer;
- (void)glwMouseEvent:(int)type event:(NSEvent*)event;
- (void)glwDelayHideCursor;
- (void)glwUnHideCursor;

/* actions */
- (IBAction)clickIncreaseZoom:(id)sender;
- (IBAction)clickDecreaseZoom:(id)sender;
- (IBAction)clickFullscreen:(id)sender;
- (IBAction)clickAbout:(id)sender;

@end

#endif
