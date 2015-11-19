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
#import <AppKit/AppKit.h>
#import <CoreAudio/HostTime.h>

#include <sys/types.h>
#include <sys/sysctl.h>

#include "main.h"
#include "navigator.h"
#include "arch/arch.h"
#include "arch/posix/posix.h"
#include "osx.h"
#include "ui/webpopup.h"
#include "htsmsg/htsmsg_store.h"
#include "misc/md5.h"
#include "misc/str.h"

prop_courier_t *mainloop_courier;

static int get_system_concurrency(void);
static void mainloop_courier_init(void);


/**
 *
 */
int64_t
arch_get_avtime(void)
{
  return AudioConvertHostTimeToNanos(AudioGetCurrentHostTime()) / 1000LL;
}

/**
 *
 */
@interface App : NSObject <NSFileManagerDelegate>
{
  NSMenu *m_menubar;
}
- (void) applicationWillTerminate: (NSNotification *)not;
- (void) newWindow: (NSNotification *)not;
- (void) initMenues;
@end


/**
 *
 */
static void
get_device_id(void)
{
  char buf[512] = {0};
  io_registry_entry_t ioRegistryRoot = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/");
 CFStringRef uuidCf = (CFStringRef) IORegistryEntryCreateCFProperty(ioRegistryRoot, CFSTR(kIOPlatformUUIDKey), kCFAllocatorDefault, 0);
 IOObjectRelease(ioRegistryRoot);
 CFStringGetCString(uuidCf, buf, sizeof(buf), kCFStringEncodingMacRoman);
 CFRelease(uuidCf);


  uint8_t digest[16];

  md5_decl(ctx);
  md5_init(ctx);

  md5_update(ctx, (const void *)buf, strlen(buf));

  md5_final(ctx, digest);
  bin2hex(gconf.device_id, sizeof(gconf.device_id), digest, sizeof(digest));
}


/**
 *
 */
int
main(int argc, char **argv)
{
  NSApplication *app = [NSApplication sharedApplication];
  App *s = [App new];

  [NSApp setDelegate: s];

  gconf.binary = argv[0];

  get_device_id();

  NSFileManager* fileManager = [NSFileManager defaultManager];
 
  NSArray *urls = [fileManager URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask];
  if ([urls count] > 0) {
    NSURL *cachedir = [[urls objectAtIndex:0] URLByAppendingPathComponent:@APPNAMEUSER];
    const char *p = [cachedir.path cStringUsingEncoding:NSUTF8StringEncoding];
    gconf.cache_path = strdup(p);
}

  posix_init();

  parse_opts(argc, argv);

  gconf.concurrency = get_system_concurrency();

  main_init();

#if ENABLE_WEBPOPUP
  webpopup_init();
#endif

  mainloop_courier_init();

  [s initMenues];

  if(!gconf.noui)
    [[GLWUI alloc] init];

  [app run];
  return 0;
}


/**
 *
 */
void
arch_exit(void)
{
  exit(gconf.exit_code);
}


/**
 *
 */
int
arch_stop_req(void)
{
  [[NSApplication sharedApplication] terminate:nil];
  return 0;
}


/**
 *
 */
const char *
arch_get_system_type(void)
{
  return "Apple";
}


/**
 *
 */
static int
get_system_concurrency(void)
{
  int mib[2];
  int ncpu;
  size_t len;

  mib[0] = CTL_HW;
  mib[1] = HW_NCPU;
  len = sizeof(ncpu);
  sysctl(mib, 2, &ncpu, &len, NULL, 0);

  return ncpu;
}



static CFRunLoopSourceRef run_loop_source;


/**
 *
 */
static void
mainloop_courier_notify(void *aux)
{
  CFRunLoopSourceSignal(run_loop_source);
  CFRunLoopWakeUp(CFRunLoopGetMain());
}


/**
 *
 */
static void
runloop_perform(void *aux)
{
  prop_courier_poll(mainloop_courier);
}


/**
 *
 */
static void
mainloop_courier_init(void)
{
  CFRunLoopSourceContext context = {0, NULL, NULL, NULL, NULL, NULL, NULL,
				    NULL, NULL, runloop_perform};

  run_loop_source = CFRunLoopSourceCreate(NULL, 0, &context);

  CFRunLoopAddSource(CFRunLoopGetCurrent(), run_loop_source,
		     kCFRunLoopDefaultMode);

  mainloop_courier = prop_courier_create_notify(mainloop_courier_notify, NULL);
}


@implementation App

/**
 *
 */
- (void) applicationWillTerminate: (NSNotification *)not;
{
  app_flush_caches();
  shutdown_hook_run(1);
  main_fini();
}


/**
 *
 */
- (BOOL)applicationShouldHandleReopen:(NSApplication *)theApplication hasVisibleWindows:(BOOL)flag
{
  if(!flag) {
    [[GLWUI alloc] init];
    return NO;
  }
  return YES;
}


/**
 *
 */
- (void)initAppleMenu
{
  NSMenuItem *menuitem;
  // Create the application (Apple) menu.
  NSMenu *menuApp = [[NSMenu alloc] initWithTitle: @"Apple Menu"];

  menuitem = [[NSMenuItem alloc] initWithTitle:@"About " APPNAMEUSER
					action:@selector(about:)
				 keyEquivalent:@""];
  [menuitem setTarget: self];
  [menuApp addItem: menuitem];
  [menuitem release];

  // -----------------------------------------------------------
  [menuApp addItem: [NSMenuItem separatorItem]];
  // -----------------------------------------------------------
  menuitem = [[NSMenuItem alloc] initWithTitle:@"Preferences..."
					action:@selector(settings:)
				 keyEquivalent:@","];

  [menuitem setTarget: self];
  [menuApp addItem: menuitem];
  [menuitem release];

  // -----------------------------------------------------------
  [menuApp addItem: [NSMenuItem separatorItem]];
  // -----------------------------------------------------------
  menuitem = [[NSMenuItem alloc] initWithTitle:@"Hide " APPNAMEUSER
					action:@selector(hide:)
				 keyEquivalent:@"h"];
  [menuitem setTarget: NSApp];
  [menuApp addItem: menuitem];
  [menuitem release];

  // -----------------------------------------------------------
  menuitem = [[NSMenuItem alloc] initWithTitle:@"Hide Others"
					action:@selector(hideOtherApplications:)
				 keyEquivalent:@"h"];
  [menuitem setKeyEquivalentModifierMask: NSAlternateKeyMask | NSCommandKeyMask];
  [menuitem setTarget: NSApp];
  [menuApp addItem: menuitem];
  [menuitem release];

  // -----------------------------------------------------------
  [menuApp addItem: [NSMenuItem separatorItem]];
  // -----------------------------------------------------------

  menuitem = [[NSMenuItem alloc] initWithTitle:@"Quit"
					action:@selector(terminate:)
				 keyEquivalent:@"q"];
  [menuitem setTarget: NSApp];
  [menuApp addItem: menuitem];
  [menuitem release];

  // -----------------------------------------------------------
  [NSApp performSelector:NSSelectorFromString(@"setAppleMenu:") withObject:menuApp];

  NSMenuItem *dummyItem = [[NSMenuItem alloc] initWithTitle:@""
						     action:nil
					      keyEquivalent:@""];
  [dummyItem setSubmenu:menuApp];
  [m_menubar addItem:dummyItem];
  [dummyItem release];

  [menuApp release];
}


/**
 *
 */
- (void)initFileMenu
{
  NSMenuItem *menuitem;
  NSMenu *menuApp = [[NSMenu alloc] initWithTitle: @"File"];

  // -----------------------------------------------------------
  menuitem = [[NSMenuItem alloc] initWithTitle:@"New Window"
					action:@selector(newWindow:)
				 keyEquivalent:@"n"];
  [menuitem setTarget: self];
  [menuApp addItem: menuitem];
  [menuitem release];


  // -----------------------------------------------------------
  menuitem = [[NSMenuItem alloc] initWithTitle:@"Close Window"
					action:@selector(closeWindow:)
				 keyEquivalent:@"w"];
  [menuApp addItem: menuitem];
  [menuitem release];

  // -----------------------------------------------------------

  NSMenuItem *dummyItem = [[NSMenuItem alloc] initWithTitle:@""
						     action:nil
					      keyEquivalent:@""];
  [dummyItem setSubmenu:menuApp];
  [m_menubar addItem:dummyItem];
  [dummyItem release];

  [menuApp release];
}


/**
 *
 */
- (void) initMenues
{
  m_menubar = [[NSMenu alloc] initWithTitle: @""];
  [NSApp setMainMenu: m_menubar];

  [self initAppleMenu];
  [self initFileMenu];
}


/**
 *
 */
- (void) newWindow: (NSNotification *)not
{
  [[GLWUI alloc] init];
}

/**
 *
 */
- (void) about: (NSNotification *)not
{
  nav_open("page:about", NULL);
}


/**
 *
 */
- (void) settings: (NSNotification *)not
{
  nav_open("settings:", NULL);
}

@end
