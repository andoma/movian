#pragma once

#ifdef __cplusplus
extern "C"
{
#endif
#if 0
}
#endif


#define NP_METADATA_CONTENT_TYPE 1
#define NP_METADATA_TITLE        2
#define NP_METADATA_ALBUM        3
#define NP_METADATA_ARTIST       4
#define NP_METADATA_DURATION     5
#define NP_METADATA_REDIRECT     6

#define NP_CONTENT_UNKNOWN      0
#define NP_CONTENT_DIR          1
#define NP_CONTENT_FILE         2
#define NP_CONTENT_ARCHIVE      3
#define NP_CONTENT_AUDIO        4
#define NP_CONTENT_VIDEO        5
#define NP_CONTENT_PLAYLIST     6
#define NP_CONTENT_DVD          7
#define NP_CONTENT_IMAGE        8
#define NP_CONTENT_ALBUM        9
#define NP_CONTENT_PLUGIN       10
#define NP_CONTENT_FONT         11
#define NP_CONTENT_SHARE        12
#define NP_CONTENT_DOCUMENT     13



typedef struct {
  int64_t timestamp;
  int64_t duration;
  int samples;
  int channels;
  int samplerate;

} np_audiobuffer_t;


#define NP_PROP_SET_STRING 1
#define NP_PROP_SET_INT    2
#define NP_PROP_SET_FLOAT  3


#ifndef NATIVEPLUGIN_HOST

// ------------------------------------------------------------
// Host methods
// ------------------------------------------------------------

void np_metadata_set(int metadata_handle, int which, ...);

void np_register_uri_prefix(const char *prefix);

typedef int prop_t;


prop_t np_prop_create(prop_t p, const char *name);

prop_t np_prop_create_root(void);

void np_prop_set(prop_t p, const char *key, int how, ...);

#define np_prop_release(p) close(p)

void np_prop_append(prop_t parent, prop_t child);

// ------------------------------------------------------------
// Plugin methods
// ------------------------------------------------------------

int np_file_probe(int fd, const uint8_t *buf0, size_t len,
                  int metadata_handle, const char *url);

void *np_audio_open(const char *url, prop_t mp_prop_root);

void np_audio_close(void *ctx);

void *np_audio_play(void *ctx, np_audiobuffer_t *nab);


#endif

#ifdef __cplusplus
}
#endif
