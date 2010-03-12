/*
 *  Apple remote and keyspan front row remote support
 *  Copyright (C) 2009 Mattias Wadman
 *
 *  Showtime bindings only tested with apple remote.
 *
 *  Thanks to Martin Kahr for Remote Control Wrapper, for more info
 *  see http://martinkahr.com/source-code/
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

//  MainController.m
//  RemoteControlWrapper
//
//  Created by Martin Kahr on 16.03.06.
//  Copyright 2006 martinkahr.com. All rights reserved.
//

#import "AppleRemote.h"
#import "KeyspanFrontRowControl.h"
#import "GlobalKeyboardDevice.h"
#import "RemoteControlContainer.h"
#import "MultiClickRemoteBehavior.h"

#include "showtime.h"
#include "event.h"
#include "ui/glw/glw.h"


@interface ShowtimeMainController : NSObject {
  RemoteControl* remoteControl;
  MultiClickRemoteBehavior* remoteControlBehavior;
  uii_t *uii;
}

@end

@implementation ShowtimeMainController

- (id) initWithUi:(ui_t *)ui primary:(int)primary {
  self = [super init];
  
  uii = calloc(1, sizeof(uii_t));
  uii->uii_ui = ui;
  uii_register(uii, primary);
  
  remoteControlBehavior = [[MultiClickRemoteBehavior alloc] init];
  [remoteControlBehavior setDelegate: self];
  
  RemoteControlContainer* container =
  [[RemoteControlContainer alloc] initWithDelegate: remoteControlBehavior];
  [container instantiateAndAddRemoteControlDeviceWithClass:
   [AppleRemote class]];
  [container instantiateAndAddRemoteControlDeviceWithClass:
   [KeyspanFrontRowControl class]];
  [container instantiateAndAddRemoteControlDeviceWithClass:
   [GlobalKeyboardDevice class]];
  
  [[NSNotificationCenter defaultCenter]
   addObserver:self
   selector:@selector(applicationDidBecomeActive:)
   name:NSApplicationDidBecomeActiveNotification
   object:NSApp];
  [[NSNotificationCenter defaultCenter]
   addObserver:self
   selector:@selector(applicationWillResignActive:)
   name:NSApplicationWillResignActiveNotification
   object:NSApp];
  
  remoteControl = container;
  
  return self;
}

// delegate method for the MultiClickRemoteBehavior
- (void) remoteButton:(RemoteControlEventIdentifier)buttonIdentifier
          pressedDown:(BOOL)pressedDown
           clickCount:(unsigned int)clickCount
{
  NSString* buttonName=nil;
  NSString* pressed=@"";
  
  if (pressedDown) pressed = @"(pressed)"; else pressed = @"(released)";
  
  switch(buttonIdentifier) {
    case kRemoteButtonPlus:
      if(pressedDown)
        glw_dispatch_event(uii, event_create_action(ACTION_UP));
      buttonName = @"Volume up";			
      break;
    case kRemoteButtonMinus:
      if(pressedDown)
        glw_dispatch_event(uii, event_create_action(ACTION_DOWN));
      buttonName = @"Volume down";
      break;			
    case kRemoteButtonMenu:
      if(pressedDown)
        glw_dispatch_event(uii, event_create_action(ACTION_NAV_BACK));
      buttonName = @"Menu";
      break;			
    case kRemoteButtonPlay:
      if(pressedDown)
        glw_dispatch_event(uii, event_create_action(ACTION_ENTER));
      buttonName = @"Play";
      break;			
    case kRemoteButtonRight:
      if(pressedDown)
        glw_dispatch_event(uii, event_create_action(ACTION_RIGHT));
      buttonName = @"Right";
      break;			
    case kRemoteButtonLeft:
      if(pressedDown)
        glw_dispatch_event(uii, event_create_action(ACTION_LEFT));
      buttonName = @"Left";
      break;			
    case kRemoteButtonRight_Hold:
      buttonName = @"Right holding";	
      break;	
    case kRemoteButtonLeft_Hold:
      buttonName = @"Left holding";		
      break;			
    case kRemoteButtonPlus_Hold:
      buttonName = @"Volume up holding";	
      break;				
    case kRemoteButtonMinus_Hold:			
      buttonName = @"Volume down holding";	
      break;				
    case kRemoteButtonPlay_Hold:
      buttonName = @"Play (sleep mode)";
      break;			
    case kRemoteButtonMenu_Hold:
      buttonName = @"Menu (long)";
      break;
    case kRemoteControl_Switched:
      buttonName = @"Remote Control Switched";
      break;
    default:
      NSLog(@"Unmapped event for button %d", buttonIdentifier); 
      break;
  }
  
  NSLog(@"Button %@ pressed %@ %d clicks", buttonName, pressed, clickCount);  
}

- (void) dealloc {
  [remoteControl autorelease];
  [remoteControlBehavior autorelease];
  [super dealloc];
}

- (void)applicationDidBecomeActive:(NSNotification *)aNotification {
  [remoteControl startListening:self];
}
- (void)applicationWillResignActive:(NSNotification *)aNotification {
  [remoteControl stopListening:self];
}

@end

static int
appleremote_start(ui_t *ui, int argc, char *argv[], int primary)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  
  [[ShowtimeMainController alloc] initWithUi:ui primary:primary];
  
  [pool release];
  
  return 0;
}

ui_t appleremote_ui = {
  .ui_title = "appleremote",
  .ui_start = appleremote_start,
};

