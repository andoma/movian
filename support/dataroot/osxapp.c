#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>

static int initialized;
static char buf[1024];
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;


const char *app_dataroot(void)
{
  if(!initialized) {
    CFBundleRef mainBundle;
    pthread_mutex_lock(&mtx);

    if(!initialized) {
      mainBundle = CFBundleGetMainBundle();
      if(mainBundle == NULL)
	abort();

      CFURLRef url = CFBundleCopyResourcesDirectoryURL(mainBundle);
      if(url == NULL)
	abort();
      //      CFRelease(mainBundle);
      
      CFURLGetFileSystemRepresentation(url, 1, (uint8_t *)buf, sizeof(buf));
      //      CFRelease(url);
    }
    initialized = 1;
    pthread_mutex_unlock(&mtx);
  }
  return buf;
}

