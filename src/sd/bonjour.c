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
#include <CoreServices/CoreServices.h>

#include <stdlib.h>
#include <arpa/inet.h>

#include "main.h"
#include "sd.h"
#include "bonjour.h"


static struct service_instance_list services;

typedef struct service_aux {
  CFNetServiceRef sa_service;
  service_class_t sa_class;
} service_aux_t;


static void
bonjour_resolve_callback(CFNetServiceRef theService,
                         CFStreamError* error,
                         void* info)
{   
  CFArrayRef addresses;
  CFDataRef txt;
  CFDictionaryRef dict;
  service_instance_t *si = info;
  service_aux_t *sa = si->si_opaque;
  struct sockaddr* addr;
  char name[256];
  int i;
  int has_ipv4 = 0;

  CFStringGetCString(CFNetServiceGetName(theService), name, sizeof(name),
                     kCFStringEncodingUTF8);

  addresses = CFNetServiceGetAddressing(theService);

  TRACE(TRACE_DEBUG, "Bonjour", "Resolve service \"%s\" with %d addresses",
        name, (int)CFArrayGetCount(addresses));

  for(i = 0; i < CFArrayGetCount(addresses); i++) {
    addr = (struct sockaddr* )
      CFDataGetBytePtr(CFArrayGetValueAtIndex(addresses, i));
    
    if(addr->sa_family == AF_INET) {
      has_ipv4 = 1;
      break;
    }
  }
  
  for(i = 0; i < CFArrayGetCount(addresses); i++) {
    char host[HOSTNAME_MAX];
    char pathbuf[URL_MAX];
    const char *path;
    char contentsbuf[512];
    const char *contents;
    int port;
    
    addr = (struct sockaddr* )
      CFDataGetBytePtr(CFArrayGetValueAtIndex(addresses, i));

    if(!addr ||
       (has_ipv4 && addr->sa_family != AF_INET) ||
       !(addr->sa_family == AF_INET || addr->sa_family == AF_INET6))
      continue;
    
    if(addr->sa_family == AF_INET) {
      struct sockaddr_in *addr_in = (struct sockaddr_in*)addr;
      inet_ntop(addr_in->sin_family, &addr_in->sin_addr, host, sizeof(host));
      port = ntohs(addr_in->sin_port);
    } else {
      struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6*)addr;
      inet_ntop(addr_in6->sin6_family, &addr_in6->sin6_addr, host, sizeof(host));
      port = ntohs(addr_in6->sin6_port);
    }
    
    switch(sa->sa_class) {
    case SERVICE_HTSP:
      TRACE(TRACE_DEBUG, "Bonjour", "Adding service htsp://%s:%d", host, port);
      sd_add_service_htsp(si, name, host, port);
      break;
    case SERVICE_WEBDAV:
      txt = CFNetServiceGetTXTData(theService);
      dict = CFNetServiceCreateDictionaryWithTXTData(kCFAllocatorDefault,
                                                     txt);
      path = NULL;
      contents = NULL;
      
      if(dict != NULL) {
        CFDataRef key;

	key = CFDictionaryGetValue(dict, CFSTR("path"));
	if(key != NULL) {
          snprintf(pathbuf, sizeof(pathbuf), "%.*s",
                   (int)CFDataGetLength(key),
                   (const char *)CFDataGetBytePtr(key));
          path = pathbuf;
        }

	key = CFDictionaryGetValue(dict, CFSTR("contents"));
        if(key != NULL) {
          snprintf(contentsbuf, sizeof(contentsbuf), "%.*s",
                   (int)CFDataGetLength(key),
                   (const char *)CFDataGetBytePtr(key));
          contents = contentsbuf;
        }

	CFRelease(dict);
      }
        
      TRACE(TRACE_DEBUG, "Bonjour", "Adding service webdav://%s:%d%s",
            host, port, path ? path : "");
      sd_add_service_webdav(si, name, host, port, path, contents);
        
      break;
    }
  }
}


static void 
bonjour_browser_callback(CFNetServiceBrowserRef browser,
                         CFOptionFlags flags,
                         CFTypeRef domainOrService,
                         CFStreamError* error,
                         void* info)
{
  CFNetServiceRef bservice = (CFNetServiceRef)domainOrService;
  CFNetServiceRef rservice;
  char name[256];
  char type[256];
  char domain[256];
  char fullname[265];
  service_instance_t *si;
  
  if(flags & kCFNetServiceFlagIsDomain) {
    TRACE(TRACE_DEBUG, "Bonjour", "Browse domain, ignoring");
    return;
  }
  
  CFStringGetCString(CFNetServiceGetName(bservice), name, sizeof(name),
                     kCFStringEncodingUTF8);
  CFStringGetCString(CFNetServiceGetType(bservice), type, sizeof(type),
                     kCFStringEncodingUTF8);
  CFStringGetCString(CFNetServiceGetDomain(bservice), domain, sizeof(domain),
                     kCFStringEncodingUTF8);
  
  /* unique enough? avahi has proto and interface too */
  snprintf(fullname, sizeof(fullname), "%s.%s.%s", name, type, domain);
  
  TRACE(TRACE_DEBUG, "Bonjour", "Browse service \"%s\" %s",
        fullname, flags & kCFNetServiceFlagRemove ? "removed" : "added");

  /* if exist, remove previous instance and resolver */
  si = si_find(&services, fullname);
  if(si != NULL) {
    rservice = ((service_aux_t *)si->si_opaque)->sa_service;
    CFNetServiceUnscheduleFromRunLoop(rservice, CFRunLoopGetCurrent(),
                                      kCFRunLoopCommonModes);
    CFNetServiceSetClient(rservice, NULL, NULL);
    CFNetServiceCancel(rservice);
    CFRelease(rservice);
    si_destroy(si);
  }
  
  if(flags & kCFNetServiceFlagRemove) {
    /* nothing */
  } else {
    CFNetServiceClientContext context = {0, NULL, NULL, NULL, NULL};
    CFTimeInterval duration = 0;
    CFStreamError rerror;
    service_aux_t *sa = info;
    
    si = calloc(1, sizeof(*si));
    si->si_opaque = sa;
    si->si_id = strdup(fullname);
    LIST_INSERT_HEAD(&services, si, si_link);

    context.info = si;
    rservice = CFNetServiceCreate(kCFAllocatorDefault,
                                  CFNetServiceGetDomain(bservice),
                                  CFNetServiceGetType(bservice),
                                  CFNetServiceGetName(bservice),
                                  0);
    CFNetServiceSetClient(rservice, bonjour_resolve_callback, &context);
    CFNetServiceScheduleWithRunLoop(rservice, CFRunLoopGetCurrent(),
                                    kCFRunLoopCommonModes);
    
    sa->sa_service = rservice;
    
    if(!CFNetServiceResolveWithTimeout(rservice, duration, &rerror)) {
      TRACE(TRACE_ERROR, "Bonjour", 
            "CFNetServiceResolveWithTimeout (domain=%d, error=%ld)\n",
            (int)rerror.domain, (long)rerror.error);
      
      CFNetServiceUnscheduleFromRunLoop(rservice, CFRunLoopGetCurrent(),
                                        kCFRunLoopCommonModes);
      CFNetServiceSetClient(rservice, NULL, NULL);
      CFNetServiceCancel(rservice);
      CFRelease(rservice);
      si_destroy(si);
    }
  }
}

static void
bonjour_type_add(const char *typename, int type)
{
  CFNetServiceBrowserRef browser;
  CFNetServiceClientContext context = {0, NULL, NULL, NULL, NULL};
  CFStreamError error;
  CFStringRef cftype = CFStringCreateWithCString(NULL, typename, 
                                                 kCFStringEncodingUTF8);
  service_aux_t *sa;

  TRACE(TRACE_DEBUG, "Bonjour", "Starting search for type %s", typename);
  
  sa = calloc(1, sizeof(*sa));
  sa->sa_class = type;
  
  context.info = sa;
  browser = CFNetServiceBrowserCreate(kCFAllocatorDefault,
                                      bonjour_browser_callback,
                                      &context);
  CFNetServiceBrowserScheduleWithRunLoop(browser, CFRunLoopGetCurrent(),
                                         kCFRunLoopCommonModes);
  
  if(!CFNetServiceBrowserSearchForServices(browser, CFSTR(""), cftype,
                                           &error)) {
    TRACE(TRACE_ERROR, "Bonjour", 
          "CFNetServiceBrowserSearchForServices (domain=%d, error=%ld)\n",
          (int)error.domain, (long)error.error);

    CFNetServiceBrowserUnscheduleFromRunLoop(browser, CFRunLoopGetCurrent(), 
                                             kCFRunLoopCommonModes);
    CFNetServiceBrowserInvalidate(browser);
    CFNetServiceBrowserStopSearch(browser, &error);
    CFRelease(browser);
    free(sa);
  }
  
  CFRelease(cftype);
}

void
bonjour_init(void)
{
  TRACE(TRACE_DEBUG, "Bonjour", "Init");
  
  bonjour_type_add("_webdav._tcp", SERVICE_WEBDAV);
  bonjour_type_add("_htsp._tcp", SERVICE_HTSP);
}
