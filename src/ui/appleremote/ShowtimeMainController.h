//
//  MainController.h
//  RemoteControlWrapper
//
//  Created by Martin Kahr on 16.03.06.
//  Copyright 2006 martinkahr.com. All rights reserved.
//

#import <Cocoa/Cocoa.h>

#include "event.h"
#include "showtime.h"
#include "ui/keymapper.h"
#include "settings.h"


@class RemoteControl;
@class MultiClickRemoteBehavior;

@interface ShowtimeMainController : NSObject {
  RemoteControl* remoteControl;
  MultiClickRemoteBehavior* remoteControlBehavior;
  uii_t *uii;
}

@end
