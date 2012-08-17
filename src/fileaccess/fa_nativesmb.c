/*
 *  Native SMB client
 *  Copyright (C) 2011 Andreas Öman
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
 */

#include <stdio.h>
#include <arpa/inet.h>
#include <assert.h>

#include "config.h"
#if ENABLE_OPENSSL
#include <openssl/md4.h>
#include <openssl/des.h>
#elif ENABLE_POLARSSL
#include "polarssl/md4.h"
#include "polarssl/des.h"
#else
#error No crypto
#endif

#include "showtime.h"
#include "fileaccess.h"
#include "fa_proto.h"
#include "keyring.h"
#include "networking/net.h"
#include "misc/string.h"
#include "misc/callout.h"

#define SAMBA_NEED_AUTH ((void *)-1)

#define SMB_ECHO_INTERVAL 30

#define SMBTRACE(x...) trace(0, TRACE_DEBUG, "SMB", x)

LIST_HEAD(cifs_connection_list, cifs_connection);
LIST_HEAD(nbt_req_list, nbt_req);
LIST_HEAD(cifs_tree_list, cifs_tree);

static struct cifs_connection_list cifs_connections;
static hts_mutex_t smb_global_mutex;


/**
 *
 */
typedef struct nbt_req {
  LIST_ENTRY(nbt_req) nr_link;
  uint16_t nr_mid;
  void *nr_response;
  int nr_response_len;
  int nr_result;
  LIST_ENTRY(nbt_req) nr_multi_link;
  int nr_offset;
  int nr_cnt;
  int nr_last;
} nbt_req_t;


/**
 *
 */
typedef struct cifs_connection {
  LIST_ENTRY(cifs_connection) cc_link;
  int cc_refcount;

  tcpcon_t *cc_tc;

  char *cc_hostname;
  int cc_port;
  int cc_as_guest;

  uint16_t cc_mid_generator;

  enum {
    CC_CONNECTING,
    CC_RUNNING,
    CC_ERROR,
    CC_ZOMBIE,
  } cc_status;
  
  hts_thread_t cc_thread;

  hts_cond_t cc_cond;

  struct nbt_req_list cc_pending_nbt_requests;

  char cc_broken;
  uint16_t cc_uid;
  uint16_t cc_max_buffer_size;
  uint16_t cc_max_mpx_count;

  uint32_t cc_session_key;

  uint8_t cc_unicode;
  uint8_t cc_bpc;   // Bytes per characters 1 or 2
  uint8_t cc_ntsmb;
  uint8_t cc_security_mode;

  uint8_t cc_challenge_key[8];
  uint8_t cc_domain[64];

  char cc_errbuf[256];

  struct cifs_tree_list cc_trees;

  callout_t cc_timer;

  int cc_auto_close;

} cifs_connection_t;



/**
 *
 */
typedef struct cifs_tree {
  cifs_connection_t *ct_cc;  // We hold a ref in the connection too
  LIST_ENTRY(cifs_tree) ct_link;
  int ct_tid;

  char *ct_share;
  int ct_refcount;

  hts_cond_t ct_cond;

  enum {
    CT_CONNECTING,
    CT_RUNNING,
    CT_ERROR,
  } ct_status;

  char ct_errbuf[256];

} cifs_tree_t;


/**
 * NBT Header
 */
typedef struct {
  uint8_t msg;
  uint8_t flags;
  uint16_t length;
} __attribute__((packed)) NBT_t;


/**
 * SMB Header (32 bytes)
 */
typedef struct {
  uint32_t proto;
  uint8_t cmd;
  uint32_t errorcode;
  uint8_t flags;
  uint16_t flags2;
  uint8_t extra[12];
  uint16_t tid;
  uint16_t pid;
  uint16_t uid;
  uint16_t mid;
} __attribute__((packed)) SMB_t;

#define SMB_FLAGS_CASELESS_PATHNAMES  0x08
#define SMB_FLAGS_CANONICAL_PATHNAMES 0x10

#define SMB_FLAGS2_KNOWS_LONG_NAMES 0x0001
#define SMB_FLAGS2_32BIT_STATUS	    0x4000
#define SMB_FLAGS2_UNICODE_STRING   0x8000


typedef struct {
  NBT_t nbt;
  SMB_t hdr;
  uint8_t wordcount;
  uint16_t bytecount;
  char protos[0];
} __attribute__((packed)) SMB_NEG_PROTOCOL_req_t;

typedef struct {
  SMB_t hdr;
  uint8_t wordcount;
  uint16_t dialectindex;
  uint8_t security_mode;
  uint16_t max_mpx_count;
  uint16_t max_number_vcs;
  uint32_t max_buffer_size;
  uint32_t max_raw_buffer;
  uint32_t session_key;
  uint32_t capabilities;
  uint64_t systemtime;
  uint16_t server_time_zone;
  uint8_t key_length;
  uint16_t bytecount;
  uint8_t data[0];
} __attribute__((packed)) SMB_NEG_PROTOCOL_reply_t;

#define SERVER_CAP_UNICODE 0x00000004
#define SERVER_CAP_NT_SMBS 0x00000010

#define SECURITY_SIGNATURES_REQUIRED	0x08
#define SECURITY_SIGNATURES_ENABLED	0x04
#define SECURITY_CHALLENGE_RESPONSE	0x02
#define SECURITY_USER_LEVEL		0x01


typedef struct {
  NBT_t nbt;
  SMB_t hdr;
  uint8_t wordcount;
  uint8_t andx_command;
  uint8_t andx_reserved;
  uint16_t andx_offset;
  uint16_t max_buffer_size;
  uint16_t max_mpx_count;
  uint16_t vc_number;
  uint32_t session_key;
  uint16_t ascii_password_length;
  uint16_t wide_password_length;
  uint32_t reserved;
  uint32_t capabilities;
  uint16_t bytecount;
  char data[0];

} __attribute__((packed)) SMB_SETUP_ANDX_req_t;


#define CLIENT_CAP_EXTENDED_SECURITY		0x80000000
#define CLIENT_CAP_LARGE_READX			0x00004000
#define CLIENT_CAP_NT_FIND			0x00000200
#define CLIENT_CAP_LEVEL_II_OPLOCKS		0x00000080
#define CLIENT_CAP_STATUS32			0x00000040
#define CLIENT_CAP_NT_SMBS			0x00000010
#define CLIENT_CAP_LARGE_FILES			0x00000008
#define CLIENT_CAP_UNICODE			0x00000004


typedef struct {
  SMB_t hdr;
  uint8_t wordcount;
  uint8_t andx_command;
  uint8_t andx_reserved;
  uint16_t andx_offset;
  uint16_t action;
  uint16_t bytecount;
  char data[0];

} __attribute__((packed)) SMB_SETUP_ANDX_reply_t;



typedef struct {
  NBT_t nbt;
  SMB_t hdr;
  uint8_t wordcount;
  uint8_t andx_command;
  uint8_t andx_reserved;
  uint16_t andx_offset;
  uint16_t flags;
  uint16_t password_length;
  uint16_t bytecount;
  char data[0];

} __attribute__((packed)) SMB_TREE_CONNECT_ANDX_req_t;




typedef struct {
  SMB_t hdr;
} __attribute__((packed)) SMB_TREE_CONNECT_ANDX_reply_t;

typedef struct {
  NBT_t nbt;
  SMB_t hdr;
  uint8_t wordcount;

  uint16_t total_param_count;
  uint16_t total_data_count;
  uint16_t max_param_count;
  uint16_t max_data_count;

  uint8_t max_setup_cnt;
  uint8_t reserved1;

  uint16_t flags;
  uint32_t timeout;
  uint16_t reserved2;

  uint16_t  param_count;
  uint16_t param_offset;
  uint16_t data_count;
  uint16_t data_offset;
  uint8_t setup_count;

  uint8_t reserved3;

  uint16_t byte_count;

  char payload[0];

} __attribute__((packed)) TRANS_req_t;



typedef struct {
  SMB_t hdr;
  uint8_t wordcount;

  uint16_t total_param_count;
  uint16_t total_data_count;
  uint16_t reserved1;
  uint16_t param_count;
  uint16_t param_offset;
  uint16_t param_displacement;
  uint16_t data_count;
  uint16_t data_offset;
  uint16_t data_displacement;
  uint8_t setup_count;
  uint8_t reserved2;
  uint16_t byte_count;
} __attribute__((packed)) TRANS_reply_t;






typedef struct {
  NBT_t nbt;
  SMB_t hdr;
  uint8_t wordcount;

  uint16_t total_param_count;
  uint16_t total_data_count;
  uint16_t max_param_count;
  uint16_t max_data_count;

  uint8_t max_setup_cnt;
  uint8_t reserved1;

  uint16_t flags;
  uint32_t timeout;
  uint16_t reserved2;

  uint16_t  param_count;
  uint16_t param_offset;
  uint16_t data_count;
  uint16_t data_offset;
  uint8_t setup_count;

  uint8_t reserved3;

  uint16_t sub_cmd;
  uint16_t byte_count;
} __attribute__((packed)) TRANS2_req_t;




typedef struct {
  SMB_t hdr;
  uint8_t wordcount;

  uint16_t total_param_count;
  uint16_t total_data_count;
  uint16_t reserved1;
  uint16_t param_count;
  uint16_t param_offset;
  uint16_t param_displacement;
  uint16_t data_count;
  uint16_t data_offset;
  uint16_t data_displacement;
  uint8_t setup_count;
  uint8_t reserved2;
  uint16_t byte_count;
} __attribute__((packed)) TRANS2_reply_t;






typedef struct {
  TRANS2_req_t t2;
  uint8_t pad[3];

  union {
    struct {
      uint16_t search_attribs;
      uint16_t search_count;
      uint16_t flags;
      uint16_t level_of_interest;
      uint32_t storage_type;
    } __attribute__((packed)) first;

    struct {
      uint16_t search_id;
      uint16_t search_count;
      uint16_t level_of_interest;
      uint32_t resume_key;
      uint16_t flags;
    } __attribute__((packed)) next;
  } __attribute__((packed));

  uint8_t data[0];

} __attribute__((packed)) SMB_TRANS2_FIND_req_t;

#define ATTR_READONLY		0x01
#define ATTR_HIDDEN		0x02
#define ATTR_SYSTEM		0x04
#define ATTR_VOLUME		0x08
#define ATTR_DIRECTORY		0x10
#define ATTR_ARCHIVE		0x20


typedef struct {
  uint16_t search_id;
  uint16_t search_count;
  uint16_t end_of_search;
  uint16_t ea_error_offset;
  uint16_t last_name_offset;
} __attribute__((packed)) SMB_FIND_PARAM_t;



typedef struct {
  uint32_t next_entry_offset;
  uint32_t file_index;
  int64_t created;
  int64_t last_access;
  int64_t last_write;
  int64_t change;
  uint64_t file_size;
  uint64_t allocation_size;
  uint32_t file_attributes;
  uint32_t file_name_len;
  uint32_t ea_list_len;
  uint16_t short_file_len;
  uint8_t short_file_name[24];
  uint8_t filename[0];
} __attribute__((packed)) SMB_FIND_DATA_t;




typedef struct {
  NBT_t nbt;
  SMB_t hdr;
  uint8_t wordcount;
  uint8_t andx_command;
  uint8_t andx_reserved;
  uint16_t andx_offset;
  uint8_t reseved;

  uint16_t name_len;
  uint32_t flags;
  uint32_t root_directory_fid;
  uint32_t access_mask;
  uint64_t allocation_size;
  uint32_t file_attributes;
  uint32_t share_access;
  uint32_t create_disposition;
  uint32_t create_options;
  uint32_t impersonation_level;
  uint8_t security_flags;

  uint16_t byte_count;
  uint8_t data[0];
} __attribute__((packed)) SMB_NTCREATE_ANDX_req_t;


typedef struct {
  SMB_t hdr;

  uint8_t wordcount;
  uint8_t andx_command;
  uint8_t andx_reserved;
  uint16_t andx_offset;

  uint8_t op_lock_level;
  uint16_t fid;
  uint32_t action;
  
  int64_t created;
  int64_t last_access;
  int64_t last_write;
  int64_t change;

  uint32_t file_attributes;
  uint64_t allocation_size;
  uint64_t file_size;
  uint16_t file_type;
  uint16_t ipc_state;
  uint8_t is_directory;
  uint16_t byte_count;
} __attribute__((packed)) SMB_NTCREATE_ANDX_resp_t;


typedef struct {
  NBT_t nbt;
  SMB_t hdr;
  uint8_t wordcount;
  uint16_t fid;
  uint32_t last_write;
  uint16_t byte_count;
} __attribute__((packed)) SMB_CLOSE_req_t;


typedef struct {
  NBT_t nbt;
  SMB_t hdr;

  uint8_t wordcount;
  uint8_t andx_command;
  uint8_t andx_reserved;
  uint16_t andx_offset;

  uint16_t fid;
  uint32_t offset_low;
  uint16_t max_count_low;
  uint16_t min_count;
  uint32_t max_count_high;
  uint16_t remaining;
  uint32_t offset_high;
  uint16_t byte_count;
} __attribute__((packed)) SMB_READ_ANDX_req_t;

typedef struct {
  SMB_t hdr;

  uint8_t wordcount;
  uint8_t andx_command;
  uint8_t andx_reserved;
  uint16_t andx_offset;

  uint16_t remaining;
  uint16_t data_compaction_mode;
  uint16_t reserved;
  uint16_t data_length_low;
  uint16_t data_offset;
  uint32_t data_length_high;
  uint8_t pad[6];
  uint16_t byte_count;
} __attribute__((packed)) SMB_READ_ANDX_resp_t;


typedef struct {
  TRANS2_req_t t2;
  uint8_t pad[3];
 
  uint16_t level_of_interest;
  uint32_t reserved;

  uint8_t data[0];

} __attribute__((packed)) SMB_TRANS2_PATH_QUERY_req_t;

typedef struct {
  int64_t created;
  int64_t last_access;
  int64_t last_write;
  int64_t change;
  uint32_t file_attributes;
} __attribute__((packed)) BasicFileInfo_t;


typedef struct {
  uint64_t allocation_size;
  uint64_t file_size;
  uint32_t link_count;
  uint8_t delete_pending;
  uint8_t is_dir;
} __attribute__((packed)) StandardFileInfo_t;

typedef struct {
  NBT_t nbt;
  SMB_t hdr;
  uint8_t wordcount;
  uint16_t echo_count;
  uint16_t byte_count;
  uint8_t data[0];
} __attribute__((packed)) EchoRequest_t;

typedef struct {
  SMB_t hdr;
  uint8_t wordcount;
  uint16_t sequence_counter;
  uint16_t byte_count;
  uint8_t data[0];
} __attribute__((packed)) EchoReply_t;


#define NBT_SESSION_MSG 0x00



#define SMB_PROTO 0x424d53ff

#define SMB_CLOSE          0x04
#define SMB_TRANSACTION    0x25
#define SMB_ECHO           0x2b
#define SMB_READ_ANDX      0x2e
#define SMB_NEG_PROTOCOL   0x72
#define SMB_SETUP_ANDX     0x73
#define SMB_TREEC_ANDX     0x75
#define SMB_TRANS2	   0x32
#define SMB_NT_CREATE_ANDX 0xa2

#define TRANS2_FIND_FIRST2	    1
#define TRANS2_FIND_NEXT2	    2
#define TRANS2_QUERY_PATH_INFORMATION 5


#if defined(__BIG_ENDIAN__)

#define htole_64(v) __builtin_bswap64(v)
#define htole_32(v) __builtin_bswap32(v)
#define htole_16(v) __builtin_bswap16(v)

#define letoh_16(v) __builtin_bswap16(v)
#define letoh_32(v) __builtin_bswap32(v)
#define letoh_64(v) __builtin_bswap64(v)

#elif defined(__LITTLE_ENDIAN__) || (__BYTE_ORDER == __LITTLE_ENDIAN)

#define htole_64(v) (v)
#define htole_32(v) (v)
#define htole_16(v) (v)

#define letoh_16(v) (v)
#define letoh_32(v) (v)
#define letoh_64(v) (v)

#else

#error Dont know endian

#endif



/**
 *
 */
static void
smberr_write(char *errbuf, size_t errlen, int code)
{
  rstr_t *r = NULL;
  switch(code) {
  case 0xc00000cc:
    r = _("Bad network share name");
    break;
  case 0xc000006d:
    r = _("Logon failure");
    break;
  case 0xc000006e:
    r = _("Account restricted");
    break;
  case 0xc0000022:
    r = _("Access denied");
    break;
  default:
    snprintf(errbuf, errlen, "NTStatus: 0x%08x", code);
    return;
  }

  snprintf(errbuf, errlen, "%s", rstr_get(r));
  rstr_release(r);
}




/**
 *
 */
static time_t
parsetime(int64_t v)
{
  v = letoh_64(v);
  return (time_t)((v/10000000LL) - 11644473600ll);
}


/**
 *
 */
static void
NTLM_hash(const char *password, uint8_t *digest)
{
  int len = strlen(password);
  uint8_t *d = alloca(len * 2);
  int i;

  for(i = 0; i < len; i++) {
    d[i*2]   = password[i];
    d[i*2+1] = 0;
  }
#if ENABLE_OPENSSL
  MD4(d, len*2, digest);
#else
  md4(d, len*2, digest);
#endif
}


#if ENABLE_OPENSSL

/**
 *
 */
static void
des_key_spread(const uint8_t *normal, DES_key_schedule *sched)
{
  uint8_t spread[8];

  spread[ 0] = normal[ 0] & 0xfe;
  spread[ 1] = ((normal[ 0] << 7) | (normal[ 1] >> 1)) & 0xfe;
  spread[ 2] = ((normal[ 1] << 6) | (normal[ 2] >> 2)) & 0xfe;
  spread[ 3] = ((normal[ 2] << 5) | (normal[ 3] >> 3)) & 0xfe;
  spread[ 4] = ((normal[ 3] << 4) | (normal[ 4] >> 4)) & 0xfe;
  spread[ 5] = ((normal[ 4] << 3) | (normal[ 5] >> 5)) & 0xfe;
  spread[ 6] = ((normal[ 5] << 2) | (normal[ 6] >> 6)) & 0xfe;
  spread[ 7] = normal[ 6] << 1;
  DES_set_key_unchecked((DES_cblock *)spread, sched);
}

/**
 *
 */
static void
lmresponse(uint8_t *out, const uint8_t *hash, const uint8_t *challenge)
{
  const uint8_t tmp[7] = {hash[14], hash[15]};
  DES_key_schedule sched;

  des_key_spread(hash, &sched);
  DES_ecb_encrypt((DES_cblock *)challenge, (DES_cblock *)out, &sched, 1);

  des_key_spread(hash+7, &sched);
  DES_ecb_encrypt((DES_cblock *)challenge, (DES_cblock *)(out+8), &sched, 1);

  des_key_spread(tmp, &sched);
  DES_ecb_encrypt((DES_cblock *)challenge, (DES_cblock *)(out+16), &sched, 1);
}
#endif

#if ENABLE_POLARSSL


static void
des_key_spread(const uint8_t *normal, des_context *ctx)
{
  uint8_t spread[8];

  spread[ 0] = normal[ 0] & 0xfe;
  spread[ 1] = ((normal[ 0] << 7) | (normal[ 1] >> 1)) & 0xfe;
  spread[ 2] = ((normal[ 1] << 6) | (normal[ 2] >> 2)) & 0xfe;
  spread[ 3] = ((normal[ 2] << 5) | (normal[ 3] >> 3)) & 0xfe;
  spread[ 4] = ((normal[ 3] << 4) | (normal[ 4] >> 4)) & 0xfe;
  spread[ 5] = ((normal[ 4] << 3) | (normal[ 5] >> 5)) & 0xfe;
  spread[ 6] = ((normal[ 5] << 2) | (normal[ 6] >> 6)) & 0xfe;
  spread[ 7] = normal[ 6] << 1;
  des_setkey_enc(ctx, spread);
}



static void
lmresponse(uint8_t *out, const uint8_t *hash, const uint8_t *challenge)
{
  const uint8_t tmp[7] = {hash[14], hash[15]};
  des_context ctx;

  des_key_spread(hash, &ctx);
  des_crypt_ecb(&ctx, challenge, out);

  des_key_spread(hash+7, &ctx);
  des_crypt_ecb(&ctx, challenge, out+8);

  des_key_spread(tmp, &ctx);
  des_crypt_ecb(&ctx, challenge, out+16);
}

#endif


/**
 *
 */
static void
smb_init_header(const cifs_connection_t *cc, SMB_t *h, int cmd, int flags, 
		int flags2, uint16_t tid, int uc)
{
  h->proto = htole_32(SMB_PROTO);
  h->cmd = cmd;
  h->flags = flags;

  flags2 |= SMB_FLAGS2_KNOWS_LONG_NAMES;

  if(cc->cc_unicode && uc)
    flags2 |= SMB_FLAGS2_UNICODE_STRING;

  h->flags2 = htole_16(flags2);
  h->pid = htole_16(1);
  h->mid = htole_16(0);

  h->uid = htole_16(cc->cc_uid);
  h->tid = htole_16(tid);
}

/**
 *
 */
static void
smb_init_t2_header(cifs_connection_t *cc, TRANS2_req_t *t2, int cmd,
		   int param_count, int data_count, int tid)
{
  smb_init_header(cc, &t2->hdr, SMB_TRANS2, SMB_FLAGS_CASELESS_PATHNAMES, 0,
		  tid, 1);

  t2->wordcount = 15;
  t2->max_param_count = htole_16(256);
  t2->max_data_count = htole_16(cc->cc_max_buffer_size);
  t2->setup_count = 1;
  t2->sub_cmd = htole_16(cmd);

  t2->param_offset      = htole_16(68);
  t2->total_param_count = htole_16(param_count);
  t2->param_count       = htole_16(param_count);

  t2->data_offset       = htole_16(68 + param_count);
  t2->data_count        = htole_16(data_count);

  t2->byte_count        = htole_16(3 + param_count + data_count);
}


/**
 *
 */
static int
nbt_read(cifs_connection_t *cc, void **bufp, int *lenp)
{
  char data[4];
  int len = 0;
  char *buf;

  do {
    if(tcp_read_data(cc->cc_tc, data, 4, NULL, 0))
      return -1;
    
    if(data[0] == 0x85)
      continue; // Keep alive

    if(data[0] != 0)
      return -1;

    len = data[1] << 16 | data[2] << 8 | data[3];
  } while(len == 0);


  buf = malloc(len);
  if(tcp_read_data(cc->cc_tc, buf, len, NULL, 0)) {
    free(buf);
    return -1;
  }

  *bufp = buf;
  *lenp = len;
  return 0;
}

/**
 *
 */
static int
nbt_write(cifs_connection_t *cc, void *buf, int len)
{
  NBT_t *nbt = buf;

  nbt->msg = NBT_SESSION_MSG;
  nbt->flags = 0;
  nbt->length = htons(len - 4);
  tcp_write_data(cc->cc_tc, buf, len);
  return 0;
}


/**
 *
 */
static size_t
utf8_to_smb(const cifs_connection_t *cc, uint8_t *dst, const char *src)
{
  size_t r;
  if(cc->cc_unicode)
    r = utf8_to_ucs2(dst, src, 1);
  else
    r = utf8_to_ascii(dst, src);
  return r;
}

/**
 *
 */
static void
cifs_maybe_destroy(cifs_connection_t *cc)
{

  if(cc->cc_refcount > 0) {
    hts_mutex_unlock(&smb_global_mutex);
    return;
  }

  LIST_REMOVE(cc, cc_link);

  // As we are unlinked noone can find us anymore, so it's safe to unlock now
  hts_mutex_unlock(&smb_global_mutex);

  if(cc->cc_tc != NULL) {
    SMBTRACE("Disconnecting from %s:%d", cc->cc_hostname, cc->cc_port);
    tcp_shutdown(cc->cc_tc);
  }

  if(cc->cc_thread)
    hts_thread_join(&cc->cc_thread);

  if(cc->cc_tc != NULL)
    tcp_close(cc->cc_tc);

  callout_disarm(&cc->cc_timer);

  hts_cond_destroy(&cc->cc_cond);
  free(cc->cc_hostname);
  free(cc);
}


/**
 *
 */
static void
cifs_release_connection(cifs_connection_t *cc)
{
  cc->cc_refcount--;
  cifs_maybe_destroy(cc);
}



/**
 *
 */
static int
smb_neg_proto(cifs_connection_t *cc, char *errbuf, size_t errlen)
{
  const char *dialect = "NT LM 0.12";
  SMB_NEG_PROTOCOL_req_t *req;
  SMB_NEG_PROTOCOL_reply_t *reply;
  void *rbuf;

  int len = strlen(dialect) + 1;
  int tlen = sizeof(SMB_NEG_PROTOCOL_req_t) + 1 + len;

  req = alloca(tlen);
  memset(req, 0, tlen);

  smb_init_header(cc, &req->hdr, SMB_NEG_PROTOCOL,
		  SMB_FLAGS_CASELESS_PATHNAMES, SMB_FLAGS2_32BIT_STATUS,
		  0, 1);

  req->wordcount = 0;
  req->bytecount = htole_16(len + 1);
  req->protos[0] = 2;
  memcpy(req->protos + 1, dialect, len);

  nbt_write(cc, req, tlen);

  if(nbt_read(cc, &rbuf, &len)) {
    snprintf(errbuf, errlen, "Socket read error during negotiation");
    return -1;
  }
  reply = rbuf;

  if(len < sizeof(SMB_NEG_PROTOCOL_reply_t) || reply->wordcount != 17) {
    snprintf(errbuf, errlen, "Malformed response %d bytes during negotiation",
	     len);
    free(rbuf);
    return -1;
  }

  if(reply->hdr.errorcode) {
    snprintf(errbuf, errlen, "Negotiation error 0x%08x",
	     (int)letoh_32(reply->hdr.errorcode));
    free(rbuf);
    return -1;
  }

  if(letoh_32(reply->capabilities) & SERVER_CAP_UNICODE)
    cc->cc_unicode = 1;
  else
    cc->cc_unicode = 0;

  cc->cc_bpc = cc->cc_unicode + 1;

  if(letoh_32(reply->capabilities) & SERVER_CAP_NT_SMBS)
    cc->cc_ntsmb = 1;
  else {
    snprintf(errbuf, errlen, "Server does not support NTSMB");
    free(rbuf);
    return -1;
  }

  cc->cc_session_key = reply->session_key;
  cc->cc_security_mode = reply->security_mode;

  cc->cc_max_buffer_size = MIN(65000, letoh_32(reply->max_buffer_size));
  cc->cc_max_mpx_count   = letoh_16(reply->max_mpx_count);

  len -= sizeof(SMB_NEG_PROTOCOL_reply_t);

  memcpy(cc->cc_challenge_key, reply->data, 8);
  len -= 8;

  ucs2_to_utf8(cc->cc_domain, sizeof(cc->cc_domain), reply->data + 8, len, 1);
  free(rbuf);
  return 0;
}


/**
 *
 */
static int
smb_setup_andX(cifs_connection_t *cc, char *errbuf, size_t errlen,
	       int non_interactive, int as_guest)
{
  SMB_SETUP_ANDX_req_t *req;
  SMB_SETUP_ANDX_reply_t *reply;

  char *username = NULL;
  const char *os = "Unix";
  const char *lanmgr = "Showtime";

  char *domain = NULL;


  size_t ulen = 0;
  size_t olen = utf8_to_smb(cc, NULL, os);
  size_t llen = utf8_to_smb(cc, NULL, lanmgr);

  void *rbuf;
  int rlen;

  const char *retry_reason = NULL;
  char reason[256];

  uint8_t password[24];
  int password_len;

 again:
  password[0] = 0;
  password_len = 1;
  free(domain);
  domain = strdup(cc->cc_domain[0] ? (char *)cc->cc_domain : "WORKGROUP");
  char *password_cleartext;

  if(cc->cc_security_mode & SECURITY_USER_LEVEL && !as_guest) {
    char id[256];
    char name[256];

    if(retry_reason && non_interactive)
      return -2;

    snprintf(id, sizeof(id), "smb:connection:%s:%d",
	     cc->cc_hostname, cc->cc_port);

    snprintf(name, sizeof(name), "Samba server '%s'", cc->cc_hostname);

    int r = keyring_lookup(id, &username, &password_cleartext, &domain, NULL,
			   name, retry_reason,
			   (retry_reason ? KEYRING_QUERY_USER : 0) |
			   KEYRING_SHOW_REMEMBER_ME | KEYRING_REMEMBER_ME_SET);

    if(r == 1) {
      retry_reason = "Login required";
      goto again;
    }

    if(r == -1) {
      /* Rejected */
      snprintf(errbuf, errlen, "Authentication rejected by user");
      free(domain);
      return -1;
    }

    // Reset domain if it was cleared by the keyring handler
    if(domain == NULL)
      domain = strdup(cc->cc_domain[0] ? (char *)cc->cc_domain : "WORKGROUP");

    assert(r == 0);

  } else {
    username = strdup("guest");
    password_cleartext = strdup("");
  }

  uint8_t pwdigest[16];
  NTLM_hash(password_cleartext, pwdigest);
  lmresponse(password, pwdigest, cc->cc_challenge_key);
  password_len = 24;

  SMBTRACE("SETUP %s:%s:%s", username ?: "<unset>",
	   *password_cleartext ? "<hidden>" : "<unset>", domain); 
  
  free(password_cleartext);

  ulen = utf8_to_smb(cc, NULL, username);
  size_t dlen = utf8_to_smb(cc, NULL, domain);
  int password_pad = cc->cc_unicode && (password_len & 1) == 0;
  int bytecount = password_len + password_pad + ulen + dlen + olen + llen;
  int tlen = bytecount + sizeof(SMB_SETUP_ANDX_req_t);


  req = alloca(tlen);
  memset(req, 0, tlen);

  smb_init_header(cc, &req->hdr, SMB_SETUP_ANDX,
		  SMB_FLAGS_CASELESS_PATHNAMES, SMB_FLAGS2_32BIT_STATUS,
		  0, 1);

  req->wordcount = 13;
  req->andx_command = 0xff;
  req->max_buffer_size = htole_16(cc->cc_max_buffer_size);
  req->max_mpx_count = htole_16(cc->cc_max_mpx_count);
  req->vc_number = 1;
  req->session_key = cc->cc_session_key;
  req->capabilities = htole_32(CLIENT_CAP_LARGE_READX | CLIENT_CAP_UNICODE | CLIENT_CAP_LARGE_FILES | CLIENT_CAP_NT_SMBS | CLIENT_CAP_STATUS32);


  req->wide_password_length = htole_16(password_len);
  req->bytecount = htole_16(bytecount);

  void *ptr = req->data;
  memcpy(ptr, password,password_len);
  ptr += password_len + password_pad;

  if(username != NULL) {
    ptr += utf8_to_smb(cc, ptr, username);
  } else {
    *((char *)ptr) = 0;
    ptr += 1;
    *((char *)ptr) = 0;
    ptr += 1;
  }
  free(username);
  username = NULL;
  ptr += utf8_to_smb(cc, ptr, domain);
  ptr += utf8_to_smb(cc, ptr, os);
  ptr += utf8_to_smb(cc, ptr, lanmgr);
  assert((ptr - (void *)req) == tlen);

  free(domain);
  domain = NULL;

  nbt_write(cc, req, tlen);

  if(nbt_read(cc, &rbuf, &rlen)) {
    snprintf(errbuf, errlen, "Socket read error during setup");
    return -1;
  }

  reply = rbuf;
  int errcode = letoh_32(reply->hdr.errorcode);

  SMBTRACE("SETUP errorcode=0x%08x", errcode);

  if(reply->hdr.errorcode) {
    
    smberr_write(reason, sizeof(reason), errcode);
    retry_reason = reason;
    free(rbuf);
    if(as_guest) {
      snprintf(errbuf, errlen, "Guest login failed");
      return -1;
    }
    goto again;
  }

  if(rlen < sizeof(SMB_SETUP_ANDX_reply_t)) {
    snprintf(errbuf, errlen, "Malformed response %d bytes during setup",
	     rlen);
    free(rbuf);
    return -1;
  }

  int guest = letoh_16(reply->action) & 1;
  cc->cc_uid = letoh_16(reply->hdr.uid);
  free(rbuf);

  SMBTRACE("Logged in as UID:%d guest=%s", cc->cc_uid, guest ? "yes" : "no");

  if(guest && !as_guest && cc->cc_security_mode & SECURITY_USER_LEVEL) {
    retry_reason = "Login attempt failed";
    goto again;
  }

  return 0;
}


/**
 *
 */
static void *
smb_dispatch(void *aux)
{
  cifs_connection_t *cc = aux;
  void *buf;
  int len;
  uint16_t mid;
  SMB_t *h;
  nbt_req_t *nr;

  SMBTRACE("%s:%d Read thread running %lx",
	   cc->cc_hostname, cc->cc_port, hts_thread_current());

  while(1) {
    if(nbt_read(cc, &buf, &len))
      break;
    
    if(len < sizeof(SMB_t)) {
      TRACE(TRACE_ERROR, "SMB", "%s:%d malformed packet smbhdrlen %d",
	    cc->cc_hostname, cc->cc_port, len);
      free(buf);
      break;
    }

    h = buf;
    mid = letoh_16(h->mid);

    if(h->pid == htole_16(1)) {
      free(buf);
      continue;
    }

    hts_mutex_lock(&smb_global_mutex);

    LIST_FOREACH(nr, &cc->cc_pending_nbt_requests, nr_link)
      if(nr->nr_mid == mid)
	break;

    if(nr != NULL) {

      nr->nr_result = 0;
      nr->nr_response = buf;
      nr->nr_response_len = len;
      hts_cond_broadcast(&cc->cc_cond);

    } else {
      SMBTRACE("%s:%d unexpected response pid=%d mid=%d",
	       cc->cc_hostname, cc->cc_port, letoh_16(h->pid), mid);
      free(buf);
    }
    hts_mutex_unlock(&smb_global_mutex);
  }

  hts_mutex_lock(&smb_global_mutex);

  LIST_FOREACH(nr, &cc->cc_pending_nbt_requests, nr_link) {
    nr->nr_result = 1;
    free(nr->nr_response);
  }

  hts_cond_broadcast(&cc->cc_cond);
  hts_mutex_unlock(&smb_global_mutex);
  return NULL;
}


static void cifs_periodic(struct callout *c, void *opaque);


/**
 *
 */
static cifs_tree_t *
get_tree_no_create(const char *hostname, int port, const char *share)
{
  cifs_connection_t *cc;
  cifs_tree_t *ct;

  hts_mutex_lock(&smb_global_mutex);

  LIST_FOREACH(cc, &cifs_connections, cc_link)
    if(!strcmp(cc->cc_hostname, hostname) && cc->cc_port == port &&
       !cc->cc_broken && cc->cc_status < CC_ERROR)
      break;
  
  if(cc == NULL) {
    hts_mutex_unlock(&smb_global_mutex);
    return NULL;
  }

  LIST_FOREACH(ct, &cc->cc_trees, ct_link)
    if(!strcmp(ct->ct_share, share))
      break;
  
  if(ct != NULL)
    ct->ct_refcount++;
  else
    hts_mutex_unlock(&smb_global_mutex);
  return ct;
}


/**
 *
 */
static cifs_connection_t *
cifs_get_connection(const char *hostname, int port, char *errbuf, size_t errlen,
		    int non_interactive, int as_guest)
{
  cifs_connection_t *cc;

  hts_mutex_lock(&smb_global_mutex);

  if(!non_interactive) {
    LIST_FOREACH(cc, &cifs_connections, cc_link)
      if(!strcmp(cc->cc_hostname, hostname) && cc->cc_port == port &&
	 cc->cc_as_guest == as_guest && 
	 !cc->cc_broken && cc->cc_status < CC_ERROR)
	break;
  } else {
    cc = NULL;
  }

  if(cc == NULL) {
    cc = calloc(1, sizeof(cifs_connection_t));
    cc->cc_uid = 1;
    cc->cc_refcount = 1;
    cc->cc_status = CC_CONNECTING;
    cc->cc_port = port;
    cc->cc_hostname = strdup(hostname);
    cc->cc_as_guest = as_guest;

    hts_cond_init(&cc->cc_cond, &smb_global_mutex);

    LIST_INSERT_HEAD(&cifs_connections, cc, cc_link);
    hts_mutex_unlock(&smb_global_mutex);

    cc->cc_tc = tcp_connect(hostname, port,
			    cc->cc_errbuf, sizeof(cc->cc_errbuf), 3000, 0);

    hts_mutex_lock(&smb_global_mutex);

    if(cc->cc_tc == NULL) {
      SMBTRACE("Unable to connect to %s:%d - %s",
	    hostname, port, cc->cc_errbuf);
      cc->cc_status = CC_ERROR;
    } else {
      SMBTRACE("Connected to %s:%d", hostname, port);

      if(smb_neg_proto(cc, cc->cc_errbuf, sizeof(cc->cc_errbuf))) {
	cc->cc_status = CC_ERROR;
      } else {
	SMBTRACE("%s:%d Protocol negotiated", hostname, port);

	int r;
	r = smb_setup_andX(cc, cc->cc_errbuf, sizeof(cc->cc_errbuf),
			   non_interactive, as_guest);

	if(r) {
	  if(r == -2) {
	    cifs_release_connection(cc);
	    return SAMBA_NEED_AUTH;
	  }
	  cc->cc_status = CC_ERROR;
	} else {
	  SMBTRACE("%s:%d Session setup", hostname, port);
	  cc->cc_status = CC_RUNNING;
	  hts_thread_create_joinable("SMB", &cc->cc_thread, smb_dispatch,
				     cc, THREAD_PRIO_NORMAL);

	  callout_arm(&cc->cc_timer, cifs_periodic, cc, SMB_ECHO_INTERVAL);
	}
      }
    }

    hts_cond_broadcast(&cc->cc_cond);


  } else {

    cc->cc_refcount++;

    while(cc->cc_status == CC_CONNECTING)
      hts_cond_wait(&cc->cc_cond, &smb_global_mutex);
  }


  if(cc->cc_status == CC_ERROR) {
    snprintf(errbuf, errlen, "%s", cc->cc_errbuf);
    cifs_release_connection(cc);
    return NULL;
  }
  cc->cc_auto_close = 0;
  return cc;
}


/**
 *
 */
static nbt_req_t *
nbt_async_req(cifs_connection_t *cc, void *request, int request_len)
{
  SMB_t *h = request + 4;
  nbt_req_t *nr = malloc(sizeof(nbt_req_t));

  nr->nr_result = -1;
  nr->nr_response = NULL;
  nr->nr_mid = cc->cc_mid_generator++;
  h->pid = htole_16(2);
  h->mid = htole_16(nr->nr_mid);
  nbt_write(cc, request, request_len);

  LIST_INSERT_HEAD(&cc->cc_pending_nbt_requests, nr, nr_link);
  return nr;
}


/**
 *
 */
static int
nbt_async_req_reply(cifs_connection_t *cc,
		    void *request, int request_len,
		    void **responsep, int *response_lenp)
{
  nbt_req_t *nr = nbt_async_req(cc, request, request_len);

  while(nr->nr_result == -1) {
    if(hts_cond_wait_timeout(&cc->cc_cond, &smb_global_mutex, 5000)) {
      TRACE(TRACE_ERROR, "SMB", "%s:%d request timeout",
	    cc->cc_hostname, cc->cc_port);
      cc->cc_broken = 1;
      break;
    }
  }
  LIST_REMOVE(nr, nr_link);

  int r = nr->nr_result;
  *responsep = nr->nr_response;
  *response_lenp = nr->nr_response_len;

  free(nr);
  return r;
}




/**
 *
 */
static void
cifs_release_tree(cifs_tree_t *ct)
{
  ct->ct_cc->cc_auto_close = 0;

  ct->ct_refcount--;
  hts_mutex_unlock(&smb_global_mutex);

  // We never disconnect from trees anyway
  /*
  if(ct->ct_refcount > 0) {
    hts_mutex_unlock(&smb_global_mutex);
    return;
  }
  cifs_release_connection(ct->ct_cc);
  LIST_REMOVE(ct, ct_link);
  free(ct->ct_share);
  free(ct);
  */
}

/**
 *
 */
static void
cifs_disconnect(cifs_connection_t *cc)
{
  cifs_tree_t *ct;

  while((ct = LIST_FIRST(&cc->cc_trees)) != NULL) {
    if(ct->ct_refcount != 0) {
      hts_mutex_unlock(&smb_global_mutex);
      return;
    }
    LIST_REMOVE(ct, ct_link);
    free(ct->ct_share);
    hts_cond_destroy(&ct->ct_cond);
    free(ct);
    cc->cc_refcount--;
  }
  cifs_maybe_destroy(cc);
}

		    

/**
 *
 */
static cifs_tree_t *
smb_tree_connect_andX(cifs_connection_t *cc, const char *share,
		      char *errbuf, size_t errlen, int non_interactive)
{
  SMB_TREE_CONNECT_ANDX_req_t *req;
  SMB_TREE_CONNECT_ANDX_reply_t *reply;
  char resource[256];
  const char *service = "?????";
  uint8_t password[24];
  void *rbuf;
  int rlen;
  cifs_tree_t *ct;

  const char *retry_reason = NULL;

  cc->cc_auto_close = 0;

  if(!non_interactive) {
    LIST_FOREACH(ct, &cc->cc_trees, ct_link) {
      if(!strcmp(ct->ct_share, share)) {
	ct->ct_refcount++;
	cc->cc_refcount--;
	break;
      }
    }
  } else {
    ct = NULL;
  }
  if(ct == NULL) {

    int password_len;

    snprintf(resource, sizeof(resource), "\\\\%s\\%s", "127.0.0.1", share);
    
  again:
    password[0] = 0;
    password_len = 1;

    if(!(cc->cc_security_mode & SECURITY_USER_LEVEL)) {
      char id[256];
      char name[256];
      int r;
      char *password_cleartext = NULL;
      snprintf(id, sizeof(id), "smb:share:%s:%d:share",
	       cc->cc_hostname, cc->cc_port);
      snprintf(name, sizeof(name), "Samba share '\\%s' on '%s'",
	       share, cc->cc_hostname);

      if(non_interactive && retry_reason) {
	cifs_release_tree(ct);
	return SAMBA_NEED_AUTH;
      }

      r = keyring_lookup(id, NULL, &password_cleartext, NULL, NULL,
			 name, retry_reason,
			 (retry_reason ? KEYRING_QUERY_USER : 0) |
			 KEYRING_SHOW_REMEMBER_ME | KEYRING_REMEMBER_ME_SET);

      if(r == -1) {
	/* Rejected */
	snprintf(ct->ct_errbuf, sizeof(ct->ct_errbuf),
		 "Authentication rejected by user");
	ct->ct_status = CT_ERROR;
	hts_cond_broadcast(&ct->ct_cond);
	goto out;
      }
      

      if(r == 0) {
	uint8_t pwdigest[16];
	NTLM_hash(password_cleartext, pwdigest);
	lmresponse(password, pwdigest, cc->cc_challenge_key);
	password_len = 24;
	free(password_cleartext);
      }
    }

    int password_pad = cc->cc_unicode && (password_len & 1) == 0;

    int resource_len =  utf8_to_smb(cc, NULL, resource);
    int service_len = strlen(service) + 1;
    int bytecount = password_len + password_pad + resource_len + service_len;

    int tlen = sizeof(SMB_TREE_CONNECT_ANDX_req_t) + bytecount;

    req = alloca(tlen);
    memset(req, 0, tlen);

    smb_init_header(cc, &req->hdr, SMB_TREEC_ANDX,
		    SMB_FLAGS_CASELESS_PATHNAMES, SMB_FLAGS2_32BIT_STATUS,
		    0, 1);

    req->wordcount = 4;
    req->andx_command = 0xff;
    req->password_length = htole_16(password_len);
  
    req->bytecount = htole_16(bytecount);

    void *ptr = req->data;
    memcpy(ptr, password, password_len);
    ptr += password_len + password_pad;

    ptr += utf8_to_smb(cc, ptr, resource);
    memcpy(ptr, service, service_len);
    ptr += service_len;

    assert((ptr - (void *)req) == tlen);


    ct = calloc(1, sizeof(cifs_tree_t));
    ct->ct_cc = cc;
    LIST_INSERT_HEAD(&cc->cc_trees, ct, ct_link);
    ct->ct_share = strdup(share);
    ct->ct_refcount = 1;
    hts_cond_init(&ct->ct_cond, &smb_global_mutex);
    ct->ct_status = CT_CONNECTING;

    if(nbt_async_req_reply(cc, req, tlen, &rbuf, &rlen)) {
      ct->ct_status = CT_ERROR;
      snprintf(ct->ct_errbuf, sizeof(ct->ct_errbuf), "Connection lost");
    } else {
      reply = rbuf;

      uint32_t err = letoh_32(reply->hdr.errorcode);
      SMBTRACE("Tree connect errorcode:0x%08x (%s)", err, share);
      if(err != 0) {

	if(!(cc->cc_security_mode & SECURITY_USER_LEVEL) &&
	   retry_reason == NULL) {
	  free(rbuf);
	  retry_reason = "Authentication failed";
	  goto again;
	}
	
	ct->ct_status = CT_ERROR;
	smberr_write(ct->ct_errbuf, sizeof(ct->ct_errbuf), err);

      } else {
	ct->ct_tid = htole_16(reply->hdr.tid);
	ct->ct_status = CT_RUNNING;
      }
    }
    free(rbuf);
    hts_cond_broadcast(&ct->ct_cond);
  }

  while(ct->ct_status == CT_CONNECTING)
    hts_cond_wait(&ct->ct_cond, &smb_global_mutex);
 out:
  if(ct->ct_status == CT_ERROR) {
    snprintf(errbuf, errlen, "%s", ct->ct_errbuf);
    cifs_release_tree(ct);
    return NULL;
  }
  return ct;
}


#define CIFS_RESOLVE_NEED_AUTH -1
#define CIFS_RESOLVE_ERROR 0
#define CIFS_RESOLVE_TREE 1
#define CIFS_RESOLVE_CONNECTION 2

/**
 *
 */
static int
cifs_resolve(const char *url, char *filename, size_t filenamesize,
	     char *errbuf, size_t errlen, int non_interactive,
	     cifs_tree_t **p_ct, cifs_connection_t **p_cc)
{
  char hostname[128];
  int port;
  char path[512];
  cifs_connection_t *cc;
  cifs_tree_t *ct;
  char *fn = NULL, *p;

  assert(p_ct != NULL);

  url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port,
	    path, sizeof(path), url);

  p = path;
  if(*p == '/') {
    p++;
    fn = strchr(p, '/');
    if(fn != NULL)
      *fn++ = 0;
    
  }
  if(port < 0)
    port = 445;

  if(*p == 0) {
    
    if(p_cc == NULL) {
      // Resolved into a host but caller won't deal with it, error out
      snprintf(errbuf, errlen, "Invalid URL for operation");
      return CIFS_RESOLVE_ERROR;
    }

    if((cc = cifs_get_connection(hostname, port, errbuf, errlen,
				 non_interactive, 1)) != NULL) {
      assert(cc != SAMBA_NEED_AUTH);
      *p_cc = cc;
      return CIFS_RESOLVE_CONNECTION;
    }

    cc = cifs_get_connection(hostname, port, errbuf, errlen,
			     non_interactive, 0);

    if(cc == SAMBA_NEED_AUTH)
      return CIFS_RESOLVE_NEED_AUTH;

    if(cc != NULL) {
      *p_cc = cc;
      return CIFS_RESOLVE_CONNECTION;
    }
    return CIFS_RESOLVE_ERROR;
  }

  snprintf(filename, filenamesize, "%s", fn ?: "");

  ct = get_tree_no_create(hostname, port, p);

  if(ct != NULL) {
    *p_ct = ct;
    return CIFS_RESOLVE_TREE;
  }

  if((cc = cifs_get_connection(hostname, port, errbuf, errlen,
			       non_interactive, 1)) != NULL) {
    assert(cc != SAMBA_NEED_AUTH); /* Should not happen if we just try to
				      login as guest */

    ct = smb_tree_connect_andX(cc, p, errbuf, errlen, non_interactive);
    if(!(cc->cc_security_mode & SECURITY_USER_LEVEL) || ct != NULL) {
      *p_ct = ct;
      return CIFS_RESOLVE_TREE;
    }
  }

  if((cc = cifs_get_connection(hostname, port, errbuf, errlen,
			       non_interactive, 0)) == NULL) {
    return CIFS_RESOLVE_ERROR;
  }

  if(cc == SAMBA_NEED_AUTH)
    return CIFS_RESOLVE_NEED_AUTH;

  ct = smb_tree_connect_andX(cc, p, errbuf, errlen, non_interactive);
  if(ct == NULL)
    return CIFS_RESOLVE_ERROR;
  
  *p_ct = ct;
  return CIFS_RESOLVE_TREE;
}




/**
 *
 */
static int
release_tree_io_error(cifs_tree_t *ct, char *errbuf, size_t errlen)
{
  snprintf(errbuf, errlen, "I/O error");
  cifs_release_tree(ct);
  return -1;
}



/**
 *
 */
static int
release_tree_protocol_error(cifs_tree_t *ct, char *errbuf, size_t errlen)
{
  snprintf(errbuf, errlen, "Protocol error");
  cifs_release_tree(ct);
  return -1;
}


static int
check_smb_error(cifs_tree_t *ct, void *rbuf, size_t rlen, size_t runt_lim,
		char *errbuf, size_t errlen)
{
  const SMB_t *smb = rbuf;
  uint32_t errcode = letoh_32(smb->errorcode);

  if(errcode) {
    snprintf(errbuf, errlen, "SMB Error 0x%08x", errcode);
  bad:
    free(rbuf);
    cifs_release_tree(ct);
    return -1;
  }
  
  if(rlen < runt_lim) {
    snprintf(errbuf, errlen, "Short packet");
    goto bad;
  }
  
  return 0;
}


/**
 *
 */
static void
backslashify(char *str)
{
  while(*str) {
    if(*str == '/')
      *str = '\\';
    str++;
  }
}


/**
 *
 */
static int
cifs_enum_shares(cifs_connection_t *cc, fa_dir_t *fd, 
		 char *errbuf, size_t errlen)
{
  fa_dir_entry_t *fde;
  TRANS_req_t *req;
  const TRANS_reply_t *resp;
  cifs_tree_t *ct;
  void *rbuf;
  int rlen;
  char url[512];
  int tlen = sizeof(TRANS_req_t) + 32;

  req = alloca(tlen);

  memset(req, 0, tlen);

  ct = smb_tree_connect_andX(cc, "IPC$", errbuf, errlen, 0);
  if(ct == NULL)
    return -1;

  smb_init_header(ct->ct_cc, &req->hdr, SMB_TRANSACTION,
		  SMB_FLAGS_CASELESS_PATHNAMES, 0, ct->ct_tid, 0);

  req->wordcount = htole_16(14);
  req->total_param_count = htole_16(19);
  req->param_count = htole_16(19);
  req->max_param_count = htole_16(1024);
  req->max_data_count = htole_16(8096);
  req->param_offset = htole_16(76);
  req->data_offset = htole_16(95);
  req->byte_count = htole_16(32);

  memcpy(&req->payload[0], "\\PIPE\\LANMAN\0\0\0WrLeh\0B13BWz\0\x01\0\xa0\x1f", 32);

  if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen))
    return release_tree_io_error(ct, errbuf, errlen);
  
  if(check_smb_error(ct, rbuf, rlen, sizeof(TRANS_reply_t), errbuf, errlen))
    return -1;

  resp = rbuf;
  
  int poff = letoh_16(resp->param_offset);
  int doff = letoh_16(resp->data_offset);
  
  if(poff + 8 > rlen) {
    free(rbuf);
    return release_tree_protocol_error(ct, errbuf, errlen);
  }


  const uint16_t *params = rbuf + poff;
  if(letoh_16(params[0]) != 0) {
    free(rbuf);
    cifs_release_tree(ct);
    snprintf(errbuf, errlen, "RPC status %d", letoh_16(params[0]));
    return -1;
  }
  
  snprintf(url, sizeof(url), "smb://%s", ct->ct_cc->cc_hostname);
  if(ct->ct_cc->cc_port != 445)
    snprintf(url + strlen(url), sizeof(url) - strlen(url), ":%d",
	     ct->ct_cc->cc_port);
  int ul = strlen(url);

  const char *p = rbuf + doff;
  int i, entries = letoh_16(params[2]);
  for(i = 0; i < entries; i++) {
    int padding = (strlen(p)+1+2) % 16 ? 16-((strlen(p)+1) % 16) : 0;
    if (*((uint16_t *)&p[strlen(p)+1+padding-2]) == 0) {
      snprintf(url + ul, sizeof(url) - ul, "/%s", p);

      fde = fa_dir_add(fd, url, p, CONTENT_DIR);
      if(fde != NULL)
	fde->fde_statdone = 1;
    }
    p += strlen(p)+1+padding+4;
  }
  cifs_release_tree(ct);

  free(rbuf);
  return 0;  

}



/**
 *
 */
static int
cifs_stat(cifs_tree_t *ct, const char *filename, fa_stat_t *fs,
	  char *errbuf, size_t errlen)
{
  char *fname = mystrdupa(filename);
  backslashify(fname);
  int plen = utf8_to_smb(ct->ct_cc, NULL, fname);
  int tlen = sizeof(SMB_TRANS2_PATH_QUERY_req_t) + plen;
  void *rbuf;
  int rlen;
  const TRANS2_reply_t *t2resp;
  SMB_TRANS2_PATH_QUERY_req_t *req = alloca(tlen);
  int i;

  for(i = 0; i < 2; i++) {
    memset(req, 0, tlen);
    smb_init_t2_header(ct->ct_cc, &req->t2, TRANS2_QUERY_PATH_INFORMATION,
		       6 + plen, 0, ct->ct_tid);
  
    req->level_of_interest = htole_16(0x101+i);
    utf8_to_smb(ct->ct_cc, req->data, fname);

    if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen))
      return release_tree_io_error(ct, errbuf, errlen);

    if(check_smb_error(ct, rbuf, rlen, sizeof(TRANS2_reply_t), errbuf, errlen))
      return -1;

    t2resp = rbuf;

    if(i == 0) {
      const BasicFileInfo_t *bfi = rbuf + letoh_16(t2resp->data_offset);
      uint32_t fa = letoh_32(bfi->file_attributes);
      
      fs->fs_mtime = parsetime(bfi->change);
      if(fa & 0x10) {
	fs->fs_type = CONTENT_DIR;
	fs->fs_size = 0;
	i = 1; // Skip StandardFileInfo Query
      } else {
	fs->fs_type = CONTENT_FILE;
      }
    } else {
      const StandardFileInfo_t *sfi = rbuf + letoh_16(t2resp->data_offset);
      fs->fs_size = letoh_64(sfi->file_size);
    }

    free(rbuf);
  }
  return 0;
}


/**
 *
 */
static int
cifs_scandir(cifs_tree_t *ct, const char *path, fa_dir_t *fd,
	     char *errbuf, size_t errlen)
{
  fa_dir_entry_t *fde;
  SMB_TRANS2_FIND_req_t *req;
  const TRANS2_reply_t *t2resp;
  const SMB_FIND_PARAM_t *respparam;
  const SMB_FIND_DATA_t *data;

  void *rbuf;
  int rlen;
  int search_id = -1;
  int search_count = 100;
  uint8_t fname[512];
  char url[1024];
  char *urlbase;
  size_t urlspace;

  snprintf((char *)fname, sizeof(fname), "%s/*", path);

  backslashify((char *)fname);

  int plen = utf8_to_smb(ct->ct_cc, NULL, (char *)fname);
  int tlen = sizeof(SMB_TRANS2_FIND_req_t) + plen;

  req = alloca(tlen);

  snprintf(url, sizeof(url), "smb://%s", ct->ct_cc->cc_hostname);
  if(ct->ct_cc->cc_port != 445)
    snprintf(url + strlen(url), sizeof(url) - strlen(url), ":%d",
	     ct->ct_cc->cc_port);
  snprintf(url + strlen(url), sizeof(url) - strlen(url),
	   "/%s/%s%s", ct->ct_share, path, *path ? "/" : "");
  urlbase = url + strlen(url);
  urlspace = sizeof(url) - strlen(url);

  while(1) {

    memset(req, 0, tlen);

    if(search_id == -1) {

      smb_init_t2_header(ct->ct_cc, &req->t2, TRANS2_FIND_FIRST2,
			 12 + plen, 0, ct->ct_tid);
  
      req->first.search_attribs    = htole_16(ATTR_READONLY |
					      ATTR_DIRECTORY |
					      ATTR_ARCHIVE);
      req->first.search_count      = htole_16(search_count);
      req->first.flags             = htole_16(6);
      req->first.level_of_interest = htole_16(260);
      utf8_to_smb(ct->ct_cc, req->data, (char *)fname);

    } else {

      smb_init_t2_header(ct->ct_cc, &req->t2, TRANS2_FIND_NEXT2,
			 12 + 1, 0, ct->ct_tid);
    
      req->next.search_id = search_id;

      req->next.search_count      = htole_16(search_count);
      req->next.flags             = htole_16(0xe);
      req->next.level_of_interest = htole_16(260);
      
      req->data[0] = 0;
      tlen = sizeof(SMB_TRANS2_FIND_req_t) + 1;
    }


    if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen))
      return release_tree_io_error(ct, errbuf, errlen);

    if(check_smb_error(ct, rbuf, rlen, sizeof(TRANS2_reply_t), errbuf, errlen))
      return -1;

    t2resp = (const TRANS2_reply_t *)rbuf;
    int poff = letoh_16(t2resp->param_offset);
    if(search_id == -1) {
      respparam = rbuf + poff;
      search_id = respparam->search_id;
    } else {
      if(poff < 2) {
	free(rbuf);
	return -1;
      }
      respparam = rbuf + poff-2;
    }

    int eos = respparam->end_of_search;
    unsigned int off = letoh_16(t2resp->data_offset);

    for(off = letoh_16(t2resp->data_offset) ;
	off + sizeof(SMB_FIND_DATA_t) < rlen ;) {
      data = rbuf + off;

      ucs2_to_utf8(fname, sizeof(fname),
		   data->filename, htole_32(data->file_name_len), 1);

      snprintf(urlbase, urlspace, "%s", fname);

      int isdir = letoh_32(data->file_attributes) & 0x10;

      fde = fa_dir_add(fd, url, (char *)fname,
		       isdir ? CONTENT_DIR : CONTENT_FILE);
      if(fde != NULL) {
	fde->fde_stat.fs_size = letoh_64(data->file_size);
	fde->fde_stat.fs_mtime = parsetime(data->change);
	fde->fde_statdone = 1;
      }

      int neo = letoh_32(data->next_entry_offset);
      if(neo == 0)
	break;
      off += neo;
    }

    free(rbuf);
    if(eos)
      break;
  }
  return 0;
}


static int
smb_scandir(fa_dir_t *fa, const char *url, char *errbuf, size_t errlen)
{
  char filename[512];
  cifs_tree_t *ct;
  cifs_connection_t *cc;
  int r;
  r = cifs_resolve(url, filename, sizeof(filename), errbuf, errlen, 0, 
		   &ct, &cc);
  switch(r) {
  default:
    return -1;

  case CIFS_RESOLVE_TREE:
    if(cifs_scandir(ct, filename, fa, errbuf, errlen))
      return -1;

    cifs_release_tree(ct);
    return 0;

  case CIFS_RESOLVE_CONNECTION:
    return cifs_enum_shares(cc, fa, errbuf, errlen);
  }
}

/**
 *
 */
typedef struct smb_file {
  fa_handle_t h;
  cifs_tree_t *sf_ct;
  uint16_t sf_fid;
  uint64_t sf_pos;
  uint64_t sf_file_size;
} smb_file_t;



/**
 *
 */
static fa_handle_t *
smb_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	 int flags, struct prop *stats)
{
  char filename[512];
  cifs_tree_t *ct;
  SMB_NTCREATE_ANDX_req_t *req;
  const SMB_NTCREATE_ANDX_resp_t *resp;
  smb_file_t *sf;

  int r = cifs_resolve(url, filename, sizeof(filename), errbuf, errlen, 0,
		       &ct, NULL);
  if(r != CIFS_RESOLVE_TREE)
    return NULL;

  cifs_connection_t *cc = ct->ct_cc;

  backslashify(filename);

  int plen = utf8_to_smb(cc, NULL, filename);
  int tlen = sizeof(SMB_NTCREATE_ANDX_req_t) + plen + cc->cc_unicode;

  void *rbuf;
  int rlen;
  
  req = alloca(tlen);
  memset(req, 0, tlen);

  smb_init_header(cc, &req->hdr, SMB_NT_CREATE_ANDX,
		  SMB_FLAGS_CANONICAL_PATHNAMES, 0, ct->ct_tid, 1);
  
  req->wordcount=24;
  req->andx_command = 0xff;
  req->access_mask = htole_32(0x20089);
  req->file_attributes = htole_32(1);
  req->create_disposition = htole_32(1);
  req->share_access = htole_32(1);
  req->impersonation_level = htole_32(2);
  req->security_flags = 3;

  utf8_to_smb(cc, req->data + cc->cc_unicode, filename);
  req->name_len = htole_16(plen - cc->cc_unicode - 1);
  req->byte_count = htole_16(plen + cc->cc_unicode);
  
  if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen)) {
    snprintf(errbuf, errlen, "I/O error");
    cifs_release_tree(ct);
    return NULL;
  }

  if(check_smb_error(ct, rbuf, rlen, sizeof(SMB_NTCREATE_ANDX_resp_t),
		     errbuf, errlen))
    return NULL;

  hts_mutex_unlock(&smb_global_mutex);

  sf = calloc(1, sizeof(smb_file_t));
  sf->sf_ct = ct;  // transfer reference of 'sf' to smb_file_t

  resp = rbuf;
  sf->sf_fid = resp->fid;
  sf->sf_file_size = letoh_64(resp->file_size);
  sf->h.fh_proto = fap;
  free(rbuf);
  return &sf->h;
}


/**
 * Close file
 */
static void
smb_close(fa_handle_t *fh)
{
  smb_file_t *sf = (smb_file_t *)fh;
  SMB_CLOSE_req_t *req;
  cifs_tree_t *ct = sf->sf_ct;

  hts_mutex_lock(&smb_global_mutex);

  req = alloca(sizeof(SMB_CLOSE_req_t));
  memset(req, 0, sizeof(SMB_CLOSE_req_t));

  smb_init_header(ct->ct_cc, &req->hdr, SMB_CLOSE,
		  SMB_FLAGS_CANONICAL_PATHNAMES, 0, ct->ct_tid, 1);
  
  req->fid = sf->sf_fid;
  req->wordcount = 3;
  nbt_write(ct->ct_cc, req, sizeof(SMB_CLOSE_req_t));
  cifs_release_tree(sf->sf_ct);
  free(sf);
}


/**
 *
 */
static int
smb_read(fa_handle_t *fh, void *buf, size_t size)
{
  smb_file_t *sf = (smb_file_t *)fh;
  SMB_READ_ANDX_req_t *req;
  const SMB_READ_ANDX_resp_t *resp;
  size_t cnt, rcnt;
  size_t total = 0;
  cifs_tree_t *ct = sf->sf_ct;
  nbt_req_t *nr = NULL;
  struct nbt_req_list reqs;


  if(sf->sf_pos + size > sf->sf_file_size)
    size = sf->sf_file_size - sf->sf_pos;

  if(size == 0)
    return 0;

  LIST_INIT(&reqs);

  req = alloca(sizeof(SMB_READ_ANDX_req_t));
  memset(req, 0, sizeof(SMB_READ_ANDX_req_t));

  hts_mutex_lock(&smb_global_mutex);

  while(size > 0) {
    cnt = MIN(size, 57344); // 14 * 4096 is max according to spec
    smb_init_header(ct->ct_cc, &req->hdr, SMB_READ_ANDX,
		    SMB_FLAGS_CANONICAL_PATHNAMES, 0, ct->ct_tid, 1);
    
    req->fid = sf->sf_fid;
    int64_t pos = sf->sf_pos + total;
    req->offset_low = htole_32((uint32_t)pos);
    req->offset_high = htole_32((uint32_t)(pos >> 32));
    req->max_count_low = htole_16(cnt & 0xffff);
    req->max_count_high = htole_32(cnt >> 16);
    req->wordcount = 12;
    req->andx_command = 0xff;

    nr = nbt_async_req(ct->ct_cc, req, sizeof(SMB_READ_ANDX_req_t));
    LIST_INSERT_HEAD(&reqs, nr, nr_multi_link);

    nr->nr_offset = total;
    nr->nr_cnt = cnt;
    total += cnt;
    size -= cnt;
    nr->nr_last = 0;
  }
  nr->nr_last = 1;

  while(1) {
    LIST_FOREACH(nr, &reqs, nr_multi_link)
      if(nr->nr_result == -1)
	break;

    if(nr != NULL) {
      if(hts_cond_wait_timeout(&ct->ct_cc->cc_cond, &smb_global_mutex, 5000)) {
	break;
      }
    } else {
      break;
    }
  }
  
  total = 0;
  while((nr = LIST_FIRST(&reqs)) != NULL) {
    if(nr->nr_result)
      goto fail;

    resp = nr->nr_response;
    uint32_t errcode = letoh_32(resp->hdr.errorcode);
    if(errcode)
      goto fail;

    LIST_REMOVE(nr, nr_link);
    LIST_REMOVE(nr, nr_multi_link);

    rcnt = letoh_16(resp->data_length_low);
    rcnt += letoh_32(resp->data_length_high) << 16;
    memcpy(buf + nr->nr_offset, 
	   nr->nr_response + letoh_16(resp->data_offset), rcnt);
    free(nr->nr_response);

    sf->sf_pos += rcnt;
    total += rcnt;

    if(nr->nr_last && rcnt < nr->nr_cnt) {
      free(nr);
      break;
    }
    free(nr);
  }
  hts_mutex_unlock(&smb_global_mutex);
  return total;

 fail:

  while((nr = LIST_FIRST(&reqs)) != NULL) {
    LIST_REMOVE(nr, nr_link);
    LIST_REMOVE(nr, nr_multi_link);
    free(nr->nr_response);
    free(nr);
  }
  hts_mutex_unlock(&smb_global_mutex);
  return -1;

}


/**
 *
 */
static int64_t
smb_seek(fa_handle_t *fh, int64_t pos, int whence)
{
  smb_file_t *sf = (smb_file_t *)fh;
  int64_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = sf->sf_pos + pos;
    break;

  case SEEK_END:
    np = sf->sf_file_size + pos;
    break;

  default:
    return -1;
  }

  if(np < 0)
    return -1;

  sf->sf_pos = np;
  return np;
}

static int64_t
smb_fsize(fa_handle_t *fh)
{
  smb_file_t *sf = (smb_file_t *)fh;
  return sf->sf_file_size;
}

/**
 *
 */
static int
smb_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	 char *errbuf, size_t errlen, int non_interactive)
{
  char filename[512];
  int r;
  cifs_tree_t *ct;
  cifs_connection_t *cc;
  r = cifs_resolve(url, filename, sizeof(filename), errbuf, errlen,
		   non_interactive, &ct, &cc);
  
  switch(r) {
  default:
    return FAP_STAT_ERR;

  case CIFS_RESOLVE_NEED_AUTH:
    return FAP_STAT_NEED_AUTH;

  case CIFS_RESOLVE_TREE:
    if(cifs_stat(ct, filename, fs, errbuf, errlen))
      return FAP_STAT_ERR;
    cifs_release_tree(ct);
    return FAP_STAT_OK;

  case CIFS_RESOLVE_CONNECTION:
    memset(fs, 0, sizeof(struct fa_stat));
    fs->fs_size = 0;
    fs->fs_mtime = 0;
    fs->fs_type = CONTENT_DIR;
    cc->cc_refcount--;
    hts_mutex_unlock(&smb_global_mutex);
    return FAP_STAT_OK;
  }
}


/**
 *
 */
static void
cifs_periodic(struct callout *c, void *opaque)
{
  cifs_connection_t *cc = opaque;
  void *rbuf;
  int rlen;

  EchoRequest_t *req = alloca(sizeof(EchoRequest_t) + 2);
  memset(req, 0, sizeof(EchoRequest_t) + 2);

  hts_mutex_lock(&smb_global_mutex);

  smb_init_header(cc, &req->hdr, SMB_ECHO, 0, 0, 0, 1);
  req->wordcount = 1;
  req->echo_count = htole_16(1);
  req->byte_count = htole_16(2);
  req->data[0] = 0x13;
  req->data[1] = 0x37;
  
  if(!nbt_async_req_reply(cc, req, sizeof(EchoRequest_t) + 2, &rbuf, &rlen)) {
    //  EchoReply_t *resp = rbuf;
    //uint32_t errcode = letoh_32(resp->hdr.errorcode);
    free(rbuf);
  }

  callout_arm(&cc->cc_timer, cifs_periodic, cc, SMB_ECHO_INTERVAL);
  cc->cc_auto_close++;
  if(cc->cc_auto_close > 5) {
    cifs_disconnect(cc);
  } else {
    hts_mutex_unlock(&smb_global_mutex);
  }
}



/**
 *
 */
static void
smb_init(void)
{
  hts_mutex_init(&smb_global_mutex);
}


/**
 * Main SMB protocol dispatch
 */
static fa_protocol_t fa_protocol_smb = {
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL | FAP_ALLOW_CACHE,
  .fap_init  = smb_init,
  .fap_name  = "smb",
  .fap_scan  = smb_scandir,
  .fap_open  = smb_open,
  .fap_close = smb_close,
  .fap_read  = smb_read,
  .fap_seek  = smb_seek,
  .fap_fsize = smb_fsize,
  .fap_stat  = smb_stat,
};
FAP_REGISTER(smb);
