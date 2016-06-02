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
#include "networking/asyncio.h"

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

  return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application {
  [[NSNotificationCenter defaultCenter] postNotificationName:@"appDidResignActive" object:nil];
}

- (void)applicationDidEnterBackground:(UIApplication *)application {
  asyncio_suspend();
}

- (void)applicationWillEnterForeground:(UIApplication *)application {
  asyncio_resume();
}

- (void)applicationDidBecomeActive:(UIApplication *)application {
  [[NSNotificationCenter defaultCenter] postNotificationName:@"appDidBecomeActive" object:nil];
}

- (void)applicationWillTerminate:(UIApplication *)application {
  printf("%s\n", __FUNCTION__);
}

@end
