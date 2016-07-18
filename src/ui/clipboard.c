#include "main.h"
#include "event.h"
#include "prop/prop.h"
#include "clipboard.h"
#include "fileaccess/fileaccess.h"
#include "task.h"

static hts_mutex_t clipboard_mutex;
static rstr_t *clipboard_content;

/**
 *
 */
static void
clipboard_setFromItem(void *opaque, event_t *e)
{
  if(event_is_type(e, EVENT_PROPREF)) {
    event_prop_t *ep = (event_prop_t *)e;
    rstr_t *url = prop_get_string(ep->p, "url", NULL);
    if(gconf.clipboard_set) {
      gconf.clipboard_set(rstr_get(url));
    } else {
      hts_mutex_lock(&clipboard_mutex);
      rstr_set(&clipboard_content, url);
      hts_mutex_unlock(&clipboard_mutex);
    }
    rstr_release(url);
    clipboard_validate_contents();
  }
}


typedef struct clipboard_copy_job {

  int64_t total_bytes;
  int64_t completed;
  int total_files;
  prop_t *prop;

  prop_t *total_prop;
  prop_t *completed_prop;
  prop_t *files_prop;

} clipboard_copy_job_t;

static int
clipboard_copy_file(const char *src, const char *dst, clipboard_copy_job_t *j)
{
  fa_handle_t *sfh = fa_open(src, NULL, 0);
  if(sfh == NULL)
    return 1;

  char filename[512];
  char dstpath[2048];
  fa_url_get_last_component(filename, sizeof(filename), src);
  fa_pathjoin(dstpath, sizeof(dstpath), dst, filename);

  fa_handle_t *dfh = fa_open_ex(dstpath, NULL, 0, FA_WRITE, NULL);
  if(dst == NULL) {
    fa_close(sfh);
    return 1;
  }

  const size_t bufsize = 32768;
  char *buf = malloc(bufsize);
  int r;
  int rcode = 0;
  while((r = fa_read(sfh, buf, bufsize)) > 0) {

    if(fa_write(dfh, buf, r) != r) {
      rcode = 1;
      break;
    }
    j->completed += r;
    prop_set_float(j->completed_prop, j->completed);
  }

  free(buf);
  fa_close(dfh);
  fa_close(sfh);
  if(rcode)
    fa_unlink(dstpath, NULL, 0);
  return rcode;
}

static int
clipboard_copy_files0(const char *src, const char *dst, clipboard_copy_job_t *j)
{
  fa_stat_t st;
  char dstpath[1024];

  if(fa_stat(src, &st, NULL, 0))
    return 1;

  if(st.fs_type == CONTENT_FILE) {
    if(dst == NULL) {
      j->total_bytes += st.fs_size;
      j->total_files++;
      prop_set_float(j->total_prop, j->total_bytes);
      prop_set_int(j->files_prop, j->total_files);
      return 0;
    } else {
      return clipboard_copy_file(src, dst, j);
    }
  }

  if(dst != NULL) {
    char filename[512];
    fa_url_get_last_component(filename, sizeof(filename), src);
    fa_pathjoin(dstpath, sizeof(dstpath), dst, filename);
    dst = dstpath;
    if(fa_makedirs(dst, NULL, 0))
      return 1;
  }

  fa_dir_t *fd = fa_scandir(src, NULL, 0);
  if(fd == NULL)
    return 1;
  int rcode = 0;
  fa_dir_entry_t *fde;
  RB_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(fa_dir_entry_stat(fde))
      continue;
    if(fde->fde_stat.fs_type == CONTENT_FILE) {
      if(dst == NULL) {
        j->total_bytes += fde->fde_stat.fs_size;
        prop_set_float(j->total_prop, j->total_bytes);
        j->total_files++;
        prop_set_int(j->files_prop, j->total_files);
      } else {
        if(clipboard_copy_files0(rstr_get(fde->fde_url), dst, j)) {
          rcode = 1;
          break;
        }
      }
    } else {
      if(clipboard_copy_files0(rstr_get(fde->fde_url), dst, j)) {
        rcode = 1;
        break;
      }
    }
  }

  fa_dir_free(fd);
  return rcode;
}



static void
clipboard_copy_files(const char *src, const char *dst)
{
  clipboard_copy_job_t j = {};
  TRACE(TRACE_DEBUG, "COPY", "Copy from %s to %s", src, dst);


  prop_t *p = prop_create(prop_create(prop_get_global(), "clipboard"),
                          "copyprogress");

  prop_t *n = prop_create_root(NULL);
  if(prop_set_parent(n, p))
    abort();
  j.total_prop = prop_create(n, "total");
  j.completed_prop = prop_create(n, "completed");
  j.files_prop = prop_create(n, "files");

  if(clipboard_copy_files0(src, NULL, &j))
    return;

  TRACE(TRACE_DEBUG, "COPY", "Copy from %s total %"PRId64" bytes",
        src, j.total_bytes);

  clipboard_copy_files0(src, dst, &j);
  prop_destroy(n);
}


/**
 *
 */
static void
clipboard_pasteToModel(void *opaque, event_t *e)
{
  if(event_is_type(e, EVENT_PROPREF)) {
    event_prop_t *ep = (event_prop_t *)e;
    rstr_t *dst = prop_get_string(ep->p, "url", NULL);
    rstr_t *src = clipboard_get();
    clipboard_copy_files(rstr_get(src), rstr_get(dst));
    rstr_release(src);
    rstr_release(dst);

    event_t *r = event_create_action(ACTION_RELOAD_DATA);
    prop_t *es = prop_create_r(ep->p, "eventSink");
    prop_send_ext_event(es, r);
    event_release(r);
    prop_ref_dec(es);
  }
}



static void
clipboard_validate_task(void *aux)
{
  rstr_t *path = clipboard_get();
  const char *contents = NULL;
  if(path != NULL) {
    contents = "string";
    struct fa_stat st;
    if(!fa_stat(rstr_get(path), &st, NULL, 0)) {
      contents = "path";
    }
  }
  prop_setv(prop_get_global(), "clipboard", "contents", NULL,
            PROP_SET_STRING, contents);
  rstr_release(path);
}


/**
 *
 */
void
clipboard_validate_contents(void)
{
  task_run(clipboard_validate_task, NULL);
}

/**
 *
 */
rstr_t *
clipboard_get(void)
{
  rstr_t *r;
  if(gconf.clipboard_get) {
    r = gconf.clipboard_get();
  } else {
    hts_mutex_lock(&clipboard_mutex);
    r = rstr_dup(clipboard_content);
    hts_mutex_unlock(&clipboard_mutex);
  }
  return r;
}


/**
 *
 */
static void
clipboard_init(void)
{
  hts_mutex_init(&clipboard_mutex);
  

  
  prop_subscribe(0,
                 PROP_TAG_CALLBACK_EVENT, clipboard_setFromItem, NULL,
                 PROP_TAG_NAME("global", "clipboard", "setFromItem"),
                 NULL);

  prop_subscribe(0,
                 PROP_TAG_CALLBACK_EVENT, clipboard_pasteToModel, NULL,
                 PROP_TAG_NAME("global", "clipboard", "pasteToModel"),
                 NULL);
  clipboard_validate_contents();
}

INITME(INIT_GROUP_IPC, clipboard_init, NULL, 10);

