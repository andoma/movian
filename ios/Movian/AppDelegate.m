//
//  AppDelegate.m
//  Movian
//
//  Created by Andreas Öman on 03/06/15.
//  Copyright (c) 2015 Lonelycoder AB. All rights reserved.
//

#import "AppDelegate.h"

#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>

#include "arch/posix/posix.h"
#include "main.h"
#include "service.h"


const char *htsversion;
const char *htsversion_full;

uint32_t
parse_version_int(const char *str)
{
  int major = 0;
  int minor = 0;
  int commit = 0;
  sscanf(str, "%d.%d.%d", &major, &minor, &commit);
  
  return
  major * 10000000 +
  minor *   100000 +
  commit;
}

uint32_t
app_get_version_int(void)
{
  return parse_version_int(htsversion);
}



@interface AppDelegate ()

@end

@implementation AppDelegate


- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    // Override point for customization after application launch.

  char path[PATH_MAX];

  NSDictionary* infoDict = [[NSBundle mainBundle] infoDictionary];
  NSString* version = [infoDict objectForKey:@"CFBundleShortVersionString"];
  htsversion_full = htsversion = strdup([version UTF8String]);
  
  gconf.concurrency = (int)[[NSProcessInfo processInfo] activeProcessorCount];

#ifdef TARGET_OS_TV
  NSArray *paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
#else
  NSArray *paths = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
#endif
  NSString *libraryDirectory = [paths objectAtIndex:0];
  snprintf(path, sizeof(path), "%s/persistent", [libraryDirectory UTF8String]);
  mkdir(path, 0777);
  gconf.persistent_path = strdup(path);
  
  paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
  libraryDirectory = [paths objectAtIndex:0];
  snprintf(path, sizeof(path), "%s/cache", [libraryDirectory UTF8String]);
  mkdir(path, 0777);
  gconf.cache_path = strdup(path);
  
  posix_init();
  
  main_init();
  
  paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
  NSString *docsdir = [paths objectAtIndex:0];

  service_createp("Files", _p("Files"), [docsdir UTF8String],
                  "files", NULL, 0, 1, SVC_ORIGIN_SYSTEM);

  [UIApplication sharedApplication].idleTimerDisabled = YES;

  return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application {
    // Sent when the application is about to move from active to inactive state. This can occur for certain types of temporary interruptions (such as an incoming phone call or SMS message) or when the user quits the application and it begins the transition to the background state.
    // Use this method to pause ongoing tasks, disable timers, and throttle down OpenGL ES frame rates. Games should use this method to pause the game.
  printf("%s\n", __FUNCTION__);
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
    // Use this method to release shared resources, save user data, invalidate timers, and store enough application state information to restore your application to its current state in case it is terminated later.
    // If your application supports background execution, this method is called instead of applicationWillTerminate: when the user quits.
  printf("%s\n", __FUNCTION__);
}

- (void)applicationWillEnterForeground:(UIApplication *)application {
    // Called as part of the transition from the background to the inactive state; here you can undo many of the changes made on entering the background.
  printf("%s\n", __FUNCTION__);
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
    // Restart any tasks that were paused (or not yet started) while the application was inactive. If the application was previously in the background, optionally refresh the user interface.
  printf("%s\n", __FUNCTION__);
}

- (void)applicationWillTerminate:(UIApplication *)application {
    // Called when the application is about to terminate. Save data if appropriate. See also applicationDidEnterBackground:.
  printf("%s\n", __FUNCTION__);
}

@end
