#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <malloc.h>
#include <arpa/inet.h>

#include "showtime.h"
#include "htsmsg/htsmsg_json.h"
#include "htsmsg/htsmsg_binary.h"
#include "htsmsg/htsmsg_store.h"
#include "htsmsg/htsbuf.h"
#include "arch/threads.h"
#include "networking/net.h"
#include "misc/str.h"
#include "event.h"
#include "miner.h"
#include "misc/buf.h"
#include "fileaccess/fileaccess.h"
#include "arch/halloc.h"
#include "settings.h"
#include "backend/backend.h"
#include "notifications.h"

#include <psl1ght/lv2/spu.h>
#include <lv2/spu.h>

#define MYURL "showtime:miner"

struct work {
  uint32_t data[32];
  uint32_t target[8];
  char job_id[128];
};


struct simple_job {
  char *job_id;
  int diff;
  uint32_t data[19];
};

typedef struct miner {
  int m_pending_job[16];

  tcpcon_t *m_tc;

  int m_running;

  struct simple_job m_simplejob;

  hts_mutex_t m_work_mutex;
  hts_cond_t m_work_cond;

  hts_mutex_t m_sock_mutex;

  struct work m_work;

} miner_t;

struct thr_info {
  int id;
  miner_t *m;
};


static sysSpuImage miner_image;

typedef struct {
  uint8_t data[128];
  uint8_t target[32];
  uint32_t next_nonce;
  uint32_t max_nonce;
  uint32_t hashes_done;
  uint32_t work_done_ptr;
  uint32_t hash_out[8];
} scanhash_spu_args_t;



/**
 *
 */
static void
simple_send(miner_t *m, htsmsg_t *msg)
{
  void *mem;
  size_t len;

  htsmsg_binary_serialize(msg, &mem, &len, 100000);

  hts_mutex_lock(&m->m_sock_mutex);
  if(m->m_tc != NULL)
    tcp_write_data(m->m_tc, mem, len);
  hts_mutex_unlock(&m->m_sock_mutex);
  free(mem);
  htsmsg_destroy(msg);
}


/**
 *
 */
static void
simple_ingest_work(miner_t *m, htsmsg_t *params)
{
  const char *job_id = htsmsg_get_str(params, "id");
  if(job_id == NULL)
    return;

  int diff = htsmsg_get_u32_or_default(params, "diff", 1);
  const void *data;
  size_t datasize;
  if(htsmsg_get_bin(params, "data", &data, &datasize))
    return;
  if(datasize != 76)
    return;

  hts_mutex_lock(&m->m_work_mutex);
  
  free(m->m_simplejob.job_id);
  m->m_simplejob.job_id = strdup(job_id);

  memcpy(m->m_simplejob.data, data, 76);

#if defined(__BIG_ENDIAN__)
#else
  for(int i = 0; i < 19; i++) 
    m->m_simplejob.data[i] = __builtin_bswap32(m->m_simplejob.data[i]);
#endif
  m->m_simplejob.diff = diff;

  for(int i = 0; i < 16; i++)
    m->m_pending_job[i] = 1;

  hts_cond_broadcast(&m->m_work_cond);
  hts_mutex_unlock(&m->m_work_mutex);
}


/**
 *
 */
static void
diff_to_target(uint32_t *target, double diff)
{
  uint64_t m;
  int k;

  for (k = 6; k > 0 && diff > 1.0; k--)
    diff /= 4294967296.0;

  m = 4294901760.0 / diff;

  if (m == 0 && k == 6)
    memset(target, 0xff, 32);
  else {
    memset(target, 0, 32);
    target[k] = m;
    target[k+1] = m >> 32;
  }
}


/**
 *
 */
static void
gen_work(miner_t *m, struct work *work)
{
  memcpy(work->data, m->m_simplejob.data, 76);
  strcpy(work->job_id, m->m_simplejob.job_id);
  diff_to_target(work->target, m->m_simplejob.diff / 65536.0);
}


/**
 *
 */
static void
submit_nonce(miner_t *m, const struct work *work)
{
  htsmsg_t *msg = htsmsg_create_map();
  htsmsg_add_u32(msg, "nonce", work->data[19]);
  htsmsg_add_str(msg, "id", work->job_id);
  htsmsg_add_u32(msg, "cmd", 2);
  simple_send(m, msg);
}




#define ptr2ea(x) ((u64)(void *)(x))

static void *
miner_spu_ctrl(void *aux)
{
  const static uint32_t ranges[7] = 
    {0x00000000, 0x2aaaaaaa, 0x55555555, 0x7fffffff,
     0xaaaaaaaa, 0xd5555554, 0xffffffff};

  uint32_t group_id;
  miner_t *m = aux;
  int i, ret;
  Lv2SpuThreadAttributes attr = { ptr2ea("miner"), 5+1,
				  LV2_SPU_THREAD_ATTRIBUTE_NONE };
  Lv2SpuThreadGroupAttributes grpattr = { 5+1, ptr2ea("miner"), 0, 0 };
  uint32_t id[6];
  void *workmem[6];
  const int work_buf_size = 131583 * 8;
  scanhash_spu_args_t *args;
  struct work work = {};
  uint32_t cause, status;
  Lv2SpuThreadArguments arg[6] = {};

  int nonce_start_offset = arch_get_seed() % 0x2aaaaaaaULL;

  for(i = 0; i < 6; i++) {
    workmem[i] = halloc(work_buf_size);
    args = workmem[i];
    args->next_nonce = ranges[i] + nonce_start_offset;

    args->max_nonce = ranges[i + 1];
    args->work_done_ptr = ptr2ea(&m->m_pending_job[0]);
  }

  hts_mutex_lock(&m->m_work_mutex);

  while(m->m_running) {
    while((m->m_pending_job[0]) != 1)
      hts_cond_wait(&m->m_work_cond, &m->m_work_mutex);
  cont:
    m->m_pending_job[0] = 0;

    ret = lv2SpuThreadGroupCreate(&group_id, 6, 200, &grpattr);
    if(ret) {
      TRACE(TRACE_ERROR, "SPUMINER", "Unable to create SPU group: 0x%x", ret);
      goto out;
    }

    gen_work(m, &work);

    for(i = 0; i < 6; i++) {

      args = workmem[i];

      memcpy(args->data, work.data, sizeof(work.data));
      memcpy(args->target, work.target, sizeof(work.target));

      arg[i].argument1 = (uint64_t)args;

      ret = lv2SpuThreadInitialize(&id[i], group_id, i, &miner_image,
				   &attr, &arg[i]);
      if(ret) {
	TRACE(TRACE_DEBUG, "SPUMINER", "Failed to create thread: %x", ret);
	goto out;
      }
    }

    ret = lv2SpuThreadGroupStart(group_id);

    if(ret)
      TRACE(TRACE_ERROR, "SPUMINER", "Start returned: 0x%x", ret);

    hts_mutex_unlock(&m->m_work_mutex);
    int64_t ts = showtime_get_ts();
    ret = lv2SpuThreadGroupJoin(group_id, &cause, &status);
    ts = showtime_get_ts() - ts;

    if(ret)
      TRACE(TRACE_ERROR, "SPUMINER", "Group joined: 0x%x", ret);

    hts_mutex_lock(&m->m_work_mutex);

    unsigned long hashes_done = 0;
    int submitted = 0;
    for(i = 0; i < 6; i++) {
      args = workmem[i];
      hashes_done += args->hashes_done;
      int xstatus;
      lv2SpuThreadGetExitStatus(id[i], &xstatus);

      if(xstatus) {
	TRACE(TRACE_DEBUG, "SPUMINER", 
	      "SPU-%d found nonce 0x%08x for work %s", i, 
	      args->next_nonce, work.job_id);
	memcpy(work.data, args->data, sizeof(work.data));
	submit_nonce(m, &work);
	submitted = 1;
      }

      if(args->next_nonce >= ranges[i + 1])
	args->next_nonce = ranges[i];
    }

    lv2SpuThreadGroupDestroy(group_id);

    ts /= 1000000;
    if(ts == 0)
      ts = 1;

    if(!m->m_running)
      break;

    goto cont;
  }

 out:

  for(i = 0; i < 6; i++)
    hfree(workmem[i], work_buf_size);

  hts_mutex_unlock(&m->m_work_mutex);
  TRACE(TRACE_DEBUG, "SPUMINER", "SPU control thread exiting");
  return NULL;
}




/**
 *
 */
static void *
miner_main_thread(void *aux)
{
  const char *hostname = "m1.lonelycoder.com";
  int port = 9898;
  char errbuf[256];
  miner_t *m = aux;
  int reconnect_timeout = 1;
  hts_thread_t spuctrl;

  hts_thread_create_joinable("spuminer", &spuctrl, miner_spu_ctrl, m,
			     THREAD_PRIO_BGTASK);

  hts_mutex_lock(&m->m_sock_mutex);

  while(m->m_running) {
    m->m_tc = tcp_connect(hostname, port, errbuf, sizeof(errbuf),
			  20000, 0, NULL);

    reconnect_timeout = MIN(reconnect_timeout * 2, 120);

    if(m->m_tc == NULL) {
      TRACE(TRACE_ERROR, "SPUMINER", "Connect failed -- %s", errbuf);
      hts_mutex_unlock(&m->m_sock_mutex);
      sleep(reconnect_timeout);
      hts_mutex_lock(&m->m_sock_mutex);
      
      continue;
    }

    hts_mutex_unlock(&m->m_sock_mutex);

    TRACE(TRACE_DEBUG, "SPUMINER", "Connected to %s:%d", hostname, port);

    htsmsg_t *out = htsmsg_create_map();
    htsmsg_add_u32(out, "cmd", 1);
    htsmsg_add_str(out, "arch", showtime_get_system_type());
    htsmsg_add_str(out, "ver", htsversion);
    simple_send(m, out);
    while(m->m_running) {
      uint32_t len;

      if(tcp_read_data(m->m_tc, &len, sizeof(len), NULL, NULL) < 0) {
	TRACE(TRACE_ERROR, "SPUMINER", "Read error");
	break;
      }
      len = ntohl(len);
      if(len > 10000) {
	TRACE(TRACE_ERROR, "SPUMINER", "Invalid packet size %d", len);
	break;
      }
      char *mem = malloc(len);
      if(tcp_read_data(m->m_tc, mem, len, NULL, NULL)) {
	TRACE(TRACE_ERROR, "SPUMINER", "Read error (payload)");
	break;
      }
      htsmsg_t *hm = htsmsg_binary_deserialize(mem, len, mem);

      if(hm == NULL) {
	TRACE(TRACE_ERROR, "SPUMINER", "Protocol error (encoding)");
	break;
      }

      reconnect_timeout = 1;
    
      int cmd = htsmsg_get_u32_or_default(hm, "cmd", 0);
      switch(cmd) {
      case 3:
	simple_ingest_work(m, hm);
	break;
      default:
	TRACE(TRACE_ERROR, "SPUMINER", "Protocol error (command %d)", cmd);
	break;
      }
      htsmsg_destroy(hm);
    }


    TRACE(TRACE_DEBUG, "SPUMINER", "Disconnected from %s:%d", hostname, port);
    hts_mutex_lock(&m->m_sock_mutex);
    tcp_close(m->m_tc);
    m->m_tc = NULL;

    if(!m->m_running)
      break;

    sleep(reconnect_timeout);
  }

  hts_mutex_unlock(&m->m_sock_mutex);

  hts_mutex_lock(&m->m_work_mutex);
  for(int i = 0; i < 16; i++)
    m->m_pending_job[i] = 1;
  hts_cond_broadcast(&m->m_work_cond);
  hts_mutex_unlock(&m->m_work_mutex);

  TRACE(TRACE_DEBUG, "SPUMINER", "Waiting for ctrl thread");
  hts_thread_join(&spuctrl);
  TRACE(TRACE_DEBUG, "SPUMINER", "ctrl thread returned");

  return NULL;
}

static prop_t *miner_model;
static htsmsg_t *miner_store;
static prop_t *miner_enabled_prop;

/**
 *
 */
static void
setup_setting_page(void)
{
  miner_model = prop_create_root(NULL);

  prop_set(miner_model, "type", PROP_SET_STRING, "raw");
  prop_t *metadata = prop_create(miner_model, "metadata");

  prop_set(metadata, "glwview", PROP_SET_STRING,
	   "dataroot://resources/spuminer/settings.view");
}

static hts_thread_t sockthread;

/**
 *
 */
static void
stop_miner(miner_t *m)
{
  if(!m->m_running)
    return;

  m->m_running = 0;
  if(m->m_tc)
    tcp_shutdown(m->m_tc);
  hts_mutex_unlock(&m->m_sock_mutex);
  TRACE(TRACE_DEBUG, "SPUMINER", "Waiting for socket thread");
  hts_thread_join(&sockthread);
  TRACE(TRACE_DEBUG, "SPUMINER", "socket thread returned");
  hts_mutex_lock(&m->m_sock_mutex);
}


/**
 *
 */
static void
start_miner(miner_t *m)
{
  if(m->m_running)
    return;

  m->m_running = 1;

  hts_thread_create_joinable("miner", &sockthread, miner_main_thread, m,
			     THREAD_PRIO_BGTASK);
}


/**
 *
 */
static void
set_miner_enabled(void *opaque, int v)
{
  miner_t *m = opaque;
  htsmsg_delete_field(miner_store, "enabled");
  htsmsg_add_u32(miner_store, "enabled", v);
  htsmsg_store_save(miner_store, "spuminer");

  if(v) {
    start_miner(m);
  } else {
    stop_miner(m);
  }
}

static prop_t *news_ref;

/**
 *
 */
static void
miner_init(void)
{
  char url[128];
  char errmsg[256];
  buf_t *b;
  snprintf(url, sizeof(url), "%s/resources/spuminer/a.out", 
          showtime_dataroot());

  if((b = fa_load(url,
                  FA_LOAD_ERRBUF(errmsg, sizeof(errmsg)),
                  NULL)) == NULL) {
    TRACE(TRACE_ERROR, "SPUMINER", "Unable to load SPU image %s -- %s",
         url, errmsg);
    return;
  }

  int x = sysSpuImageImport(&miner_image, b->b_ptr, 0);
  buf_release(b);

  if(x) {
    TRACE(TRACE_ERROR, "SPUMINER", "Unable to import SPU image %s -- %s",
         url, errmsg);
    return;
  }

  miner_t *m;

  m = memalign(16, sizeof(miner_t));
  memset(m, 0, sizeof(miner_t));

  hts_mutex_init(&m->m_work_mutex);
  hts_cond_init(&m->m_work_cond, &m->m_work_mutex);
  hts_mutex_init(&m->m_sock_mutex);

  settings_add_url(NULL,
		   _p("Donations"), NULL,
		   "dataroot://resources/spuminer/icon_16_litecoin.png",
		   _p("Donate money to the Showtime project by mining litecoints"), MYURL, 0);

  miner_store = htsmsg_store_load("spuminer") ?: htsmsg_create_map();

  int runs = htsmsg_get_u32_or_default(miner_store, "runcnt", 0);
  htsmsg_delete_field(miner_store, "runcnt");
  htsmsg_add_u32(miner_store, "runcnt", runs+1);
  htsmsg_store_save(miner_store, "spuminer");

  int enabled = htsmsg_get_u32_or_default(miner_store, "enabled", 0);

  if((runs % 5) == 1 && !enabled) {
    rstr_t *r = _("Do you like Showtime? You can donate money to the project by mining Litecoins on your PS3");
    rstr_t *s = _("Show me more");
    news_ref = add_news("miningpitch", rstr_get(r), MYURL, rstr_get(s));
    rstr_release(r);
    rstr_release(s);
  }


  prop_t *sm = prop_create(prop_get_global(), "spuminer");
  miner_enabled_prop = prop_create(sm, "enabled");
  prop_set_int(miner_enabled_prop, enabled);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "spuminer", "enabled"),
		 PROP_TAG_CALLBACK_INT, set_miner_enabled, m,
		 PROP_TAG_MUTEX, &m->m_sock_mutex,
		 NULL);

  setup_setting_page();
}

INITME(INIT_GROUP_API, miner_init);


/**
 *
 */
static int
be_miner_open(prop_t *page, const char *url0, int sync)
{
  prop_link(miner_model, prop_create(page, "model"));
  prop_destroy(news_ref);
  prop_ref_dec(news_ref);
  return 0;
}


/**
 *
 */
static int
be_miner_canhandle(const char *url)
{
  return !strncmp(url, MYURL, strlen(MYURL));
}


/**
 *
 */
static backend_t be_miner = {
  .be_canhandle = be_miner_canhandle,
  .be_open = be_miner_open,
};

BE_REGISTER(miner);
