#ifndef SVFS_H__
#define SVFS_H__

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>

struct svfs_ops {
  void *(*open)(const char *url);
  void (*close)(void *handle);
  int (*read)(void *handle, void *buf, size_t size);
  int64_t (*seek)(void *handle, int64_t pos, int whence);
  int (*stat)(const char *url, struct stat *buf);
  int (*findfile)(const char *path, const char *file, 
		  char *fullpath, size_t fullpathlen);
};

#endif /* SVFS_H__ */
