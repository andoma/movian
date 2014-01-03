/*
 *  Copyright 2014 (C) Spotify AB
 */

#include <unistd.h>

#include "arch/threads.h"
#include "showtime.h"
#include "fileaccess/fileaccess.h"
#include "htsmsg/htsmsg_json.h"
#include "media.h"
#include "backend/backend.h"

#define MAGNETO_LOG "magneto"

#define MAGNETO_WEBGATE "https://mwebgw.spotify.com/"

static char auth_header[512];
static hts_mutex_t auth_mutex;

static prop_t *magneto_prop_root;
static prop_t *magneto_prop_categories;

TAILQ_HEAD(magneto_category_queue, magneto_category);

typedef struct magneto_category {
  TAILQ_ENTRY(magneto_category) mc_link;
  char *mc_id;
  char *mc_endpoint;
  prop_t *mc_prop_root;
  prop_t *mc_prop_stories;

} magneto_category_t;

static struct magneto_category_queue magneto_categories;


/**
 * This callback takes care of authentication for magneto
 * JSON APIs (webgate)
 */
static int
magneto_auth(const char *url, struct http_auth_req *har)
{
  if(mystrbegins(url, MAGNETO_WEBGATE) == NULL)
    return 0;

  hts_mutex_lock(&auth_mutex);

  if(!auth_header[0]) {

    char errbuf[256];
    buf_t *result1, *result2;
    int r;

    r = http_req(MAGNETO_WEBGATE"login",
                 HTTP_FLAGS(FA_DISABLE_AUTH),
                 HTTP_ARG("username", "ludde"),
                 HTTP_ARG("password", "test1"),
                 HTTP_RESULT_PTR(&result1),
                 HTTP_ERRBUF(errbuf, sizeof(errbuf)),
                 NULL);

    if(r) {
      TRACE(TRACE_ERROR, MAGNETO_LOG, "/login failed -- %s", errbuf);
      http_client_fail_req(har, "Login request failed");
      goto done;
    }

    r = http_req(MAGNETO_WEBGATE"getToken",
                 HTTP_FLAGS(FA_DISABLE_AUTH),
                 HTTP_ARG("login", buf_cstr(result1)),
                 HTTP_RESULT_PTR(&result2),
                 HTTP_ERRBUF(errbuf, sizeof(errbuf)),
                 NULL);

    buf_release(result1);

    if(r) {
      TRACE(TRACE_ERROR, MAGNETO_LOG, "/getToken failed -- %s", errbuf);
      http_client_fail_req(har, "getToken request failed");
      goto done;
    }

    // Construct final HTTP 'Authorization' header

    snprintf(auth_header, sizeof(auth_header),
             "Oauth oauth_token=\"%s\"", buf_cstr(result2));
    buf_release(result2);
  }

  http_client_rawauth(har, auth_header);
 done:
  hts_mutex_unlock(&auth_mutex);
  return 1;
}

/**
 *
 */
static void
imageset_set(htsmsg_t *m, prop_t *p, const char *name)
{
  if(m == NULL)
    return;

  rstr_t *r = htsmsg_json_serialize_to_rstr(m, "imageset:");
  prop_set(p, name, PROP_SET_RSTRING, r);
  rstr_release(r);
}

/**
 *
 */
static int
make_hls_url(htsmsg_t *doc, char *out, size_t outlen)
{
  int64_t start_ms = 0, stop_ms = 0;
  if(htsmsg_get_s64(doc, "start_ms", &start_ms))
    return -1;

  if(htsmsg_get_s64(doc, "stop_ms", &stop_ms))
    return -1;

  htsmsg_t *channel = htsmsg_get_map(doc, "channel");
  if(channel == NULL)
    return -1;

  const char *playlist_url = htsmsg_get_str(channel, "playlist_url");
  if(playlist_url == NULL)
    return -1;

  snprintf(out, outlen,
           "hls:%s?start=%"PRId64"&duration=%"PRId64,
           playlist_url, start_ms, stop_ms - start_ms);
  return 0;
}


/**
 *
 */
static void
event_add(magneto_category_t *mc, htsmsg_t *story)
{
  prop_t *p = prop_create_root(NULL);

  htsmsg_t *episode = htsmsg_get_map(story, "episode");

  const char *title       = htsmsg_get_str(story, "title");
  const char *description = htsmsg_get_str(story, "description");

  char hls_url[512];
  if(!make_hls_url(story, hls_url, sizeof(hls_url)))
    prop_set(p, "videouri",    PROP_SET_STRING, hls_url);

  prop_set(p, "title",       PROP_SET_STRING, title);
  prop_set(p, "description", PROP_SET_STRING, description);

  if(episode != NULL)
    imageset_set(htsmsg_get_list(episode, "thumbnail"), p, "icon");

  if(prop_set_parent(p, mc->mc_prop_stories))
    abort();
}


/**
 *
 */
static void
story_add(magneto_category_t *mc, htsmsg_t *story)
{
  prop_t *p = prop_create_root(NULL);

  htsmsg_t *ri = htsmsg_get_map(story, "recommended_item");
  if(ri == NULL)
    return;

  htsmsg_t *metadata = htsmsg_get_map(story, "metadata");
  if(metadata == NULL)
    return;

  const char *uri         = htsmsg_get_str(ri, "uri");
  const char *title       = htsmsg_get_str(ri, "display_name");
  const char *subtitle    = htsmsg_get_str(metadata, "subtitle");
  const char *description = htsmsg_get_str(metadata, "description");

  prop_set(p, "videouri",    PROP_SET_STRING, uri);
  prop_set(p, "title",       PROP_SET_STRING, title);
  prop_set(p, "subtitle",    PROP_SET_STRING, subtitle);
  prop_set(p, "description", PROP_SET_STRING, description);

  imageset_set(htsmsg_get_list(story, "hero_image"), p, "icon");

  if(prop_set_parent(p, mc->mc_prop_stories))
    abort();
}


/**
 *
 */
static void
category_load(magneto_category_t *mc)
{
  char url[512];
  snprintf(url, sizeof(url), MAGNETO_WEBGATE"%s", mc->mc_endpoint);

  int r;
  char errbuf[512];
  buf_t *result;

  r = http_req(url,
               HTTP_RESULT_PTR(&result),
               HTTP_ERRBUF(errbuf, sizeof(errbuf)),
               NULL);

  if(r) {
    TRACE(TRACE_ERROR, MAGNETO_LOG, "Failed to load %s -- %s", url, errbuf);
    return;
  }

  htsmsg_t *doc = htsmsg_json_deserialize(buf_cstr(result));
  buf_release(result);

  if(doc == NULL) {
    TRACE(TRACE_ERROR, MAGNETO_LOG, "Failed to decode JSON for %s", url);
    return;
  }

  htsmsg_t *stories = htsmsg_get_list(doc, "stories");
  htsmsg_t *events  = htsmsg_get_list(doc, "events");
  htsmsg_field_t *f;

  if(stories != NULL) {
    HTSMSG_FOREACH(f, stories) {
      htsmsg_t *story = htsmsg_get_map_by_field(f);
      if(story == NULL)
        continue; // field was not a map
      story_add(mc, story);
    }
  }

  if(events != NULL) {
    HTSMSG_FOREACH(f, events) {
      htsmsg_t *event = htsmsg_get_map_by_field(f);
      if(event == NULL)
        continue; // field was not a map
      event_add(mc, event);
    }
  }
  htsmsg_destroy(doc);
}


/**
 *
 */
static void
category_add(const char *id, const char *type, const char *title,
             const char *endpoint)
{
  endpoint = mystrbegins(endpoint, "sp://webgate/");
  if(endpoint == NULL) {
    TRACE(TRACE_ERROR, MAGNETO_LOG,
          "Category %s -- Invalid endpoint", id, endpoint);
    return;
  }

  magneto_category_t *mc = calloc(1, sizeof(magneto_category_t));
  mc->mc_id = strdup(id);
  mc->mc_endpoint = strdup(endpoint);

  prop_t *p = mc->mc_prop_root = prop_create_root(NULL);

  prop_set(p, "type",  PROP_SET_STRING, type);
  prop_set(p, "title", PROP_SET_STRING, title);

  mc->mc_prop_stories = prop_create(p, "stories");

  if(prop_set_parent(p, magneto_prop_categories))
    abort();

  TAILQ_INSERT_TAIL(&magneto_categories, mc, mc_link);

  category_load(mc);
}


/**
 *
 */
static void
categories_load(void)
{
  int r;
  char errbuf[512];
  buf_t *result;

  r = http_req(MAGNETO_WEBGATE"magneto-categories-view/v1/categories/ipad",
               HTTP_ARG("f", "json"),
               HTTP_RESULT_PTR(&result),
               HTTP_ERRBUF(errbuf, sizeof(errbuf)),
               NULL);

  if(r) {
    TRACE(TRACE_ERROR, MAGNETO_LOG, "categories failed -- %s", errbuf);
    return;
  }

  htsmsg_t *doc = htsmsg_json_deserialize(buf_cstr(result));
  buf_release(result);

  if(doc == NULL) {
    TRACE(TRACE_ERROR, MAGNETO_LOG, "categories failed to decode JSON");
    return;
  }

  htsmsg_t *categories = htsmsg_get_list(doc, "categories");
  if(categories != NULL) {
    htsmsg_field_t *f;
    HTSMSG_FOREACH(f, categories) {
      htsmsg_t *category = htsmsg_get_map_by_field(f);
      if(category == NULL)
        continue; // field was not a map

      // We 'abuse' context as our ID field

      const char *id = htsmsg_get_str(category, "context");
      if(id == NULL) {
        TRACE(TRACE_ERROR, MAGNETO_LOG,
              "A category is missing 'context' field");
        continue;
      }
      const char *type = htsmsg_get_str(category, "type");
      if(type == NULL) {
        TRACE(TRACE_ERROR, MAGNETO_LOG,
              "Category '%s' is missing type field", id);
        continue;
      }

      const char *title = htsmsg_get_str(category, "title");
      if(title == NULL) {
        TRACE(TRACE_ERROR, MAGNETO_LOG,
              "Category '%s' is missing title field", id);
        continue;
      }

      const char *endpoint = htsmsg_get_str(category, "endpoint");
      if(endpoint == NULL) {
        TRACE(TRACE_ERROR, MAGNETO_LOG,
              "Category '%s' is missing endpoint field", id);
        continue;
      }

      category_add(id, type, title, endpoint);
    }
  }
  htsmsg_destroy(doc);
}




/**
 *
 */
static void *
magneto_model_thread(void *aux)
{
  categories_load();
  //   prop_print_tree(magneto_prop_root, 1);
  while(1) {
    sleep(1);

  }
  return NULL;
}


/**
 *
 */
static int
magneto_model_init(void)
{
  TAILQ_INIT(&magneto_categories);

  hts_mutex_init(&auth_mutex);
  http_auth_req_register(&magneto_auth);

  magneto_prop_root = prop_create(prop_get_global(), "magneto");
  magneto_prop_categories = prop_create(magneto_prop_root, "categories");

  hts_thread_create_detached("magnetomodel", magneto_model_thread, NULL,
                             THREAD_PRIO_MODEL);
  return 0;
}


/**
 *
 */
static event_t *
magneto_model_playvideo(const char *uri, media_pipe_t *mp,
                        char *errbuf, size_t errlen,
                        video_queue_t *vq, struct vsource_list *vsl,
                        const video_args_t *va0)
{
  const char *gid = mystrbegins(uri, "projectxx:epgevent:");
  if(gid == NULL) {
    snprintf(errbuf, errlen, "Invalid URL");
    return NULL;
  }

  char url[512];
  snprintf(url, sizeof(url), MAGNETO_WEBGATE"vmetadata/event/%s", gid);

  int r;
  buf_t *result;

  r = http_req(url,
               HTTP_RESULT_PTR(&result),
               HTTP_ARG("f", "json"),
               HTTP_ERRBUF(errbuf, errlen),
               NULL);

  if(r) {
    TRACE(TRACE_ERROR, MAGNETO_LOG, "Failed to load %s -- %s",
          url, errbuf);
    return NULL;
  }

  htsmsg_t *doc = htsmsg_json_deserialize(buf_cstr(result));
  buf_release(result);

  if(doc == NULL) {
    snprintf(errbuf, errlen, "Invalid JSON");
    return NULL;
  }

  char hls_url[256];
  r = make_hls_url(doc, hls_url, sizeof(hls_url));

  htsmsg_destroy(doc);

  if(r) {
    snprintf(errbuf, errlen,
             "Unable to construt HLS URL -- missing params");
    return NULL;
  }

  TRACE(TRACE_DEBUG, MAGNETO_LOG, "Redirecting %s -> %s", uri, hls_url);

  return backend_play_video(hls_url, mp, errbuf, errlen, vq, vsl, va0);
}


/**
 *
 */
static int
magneto_model_canhandle(const char *url)
{
  return !strncmp(url, "projectxx:epgevent:",
                  strlen("projectxx:epgevent:"));
}


/**
 *
 */
static backend_t be_hls = {
  .be_init        = magneto_model_init,
  .be_canhandle   = magneto_model_canhandle,
  .be_play_video  = magneto_model_playvideo,
};

BE_REGISTER(hls);

