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
#include <stdio.h>
#include <assert.h>


#include "config.h"
#include "misc/md4.h"


// We don't have wrappers for DES
#if ENABLE_OPENSSL
#include <openssl/des.h>
#elif ENABLE_POLARSSL
#include "polarssl/des.h"
#elif ENABLE_COMMONCRYPTO
#include <CommonCrypto/CommonCrypto.h>
#else
#error No crypto
#endif

#include "main.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_proto.h"
#include "keyring.h"
#include "networking/net.h"
#include "misc/str.h"
#include "misc/callout.h"
#include "usage.h"
#include "misc/minmax.h"
#include "misc/bytestream.h"
#include "misc/endian.h"

// http://msdn.microsoft.com/en-us/library/ee442092.aspx

#define SAMBA_NEED_AUTH ((void *)-1)

#define SMB_ECHO_INTERVAL 30

#define SMBTRACE(x, ...) do {                                  \
    if(gconf.enable_smb_debug)                                 \
      tracelog(0, TRACE_DEBUG, "SMB", x, ##__VA_ARGS__);          \
  } while(0)

LIST_HEAD(cifs_connection_list, cifs_connection);
LIST_HEAD(nbt_req_list, nbt_req);
LIST_HEAD(cifs_tree_list, cifs_tree);

static struct cifs_connection_list cifs_connections;
static hts_mutex_t smb_global_mutex;

#define NBT_TIMEOUT 30000

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
  int nr_is_trans2;
  int nr_data_count;
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
  char cc_wait_for_ping;
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

  char *cc_native_os;
  char *cc_native_lanman;
  char *cc_primary_domain;

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

#include "nbt.h"
#include "smbv1.h"

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
  case 0xc0000034:
    r = _("Object name not found");
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
static char *
readstring(uint8_t **pp, int *lp, int unicode)
{
  if(unicode == 0)
    return NULL;

  int remain = *lp;
  uint8_t *p = *pp;
  int outbytes = 0;
  int c = 0;
  if(remain < 2)
    return NULL;

  while(remain >= 2) {
    if(p[0] == 0 && p[1] == 0) {
      remain -= 2;
      break;
    }
    outbytes += utf8_put(NULL, p[0] | p[1] << 8);
    p+=2;
    remain -= 2;
    c++;
  }

  *lp = remain;

  char *str = malloc(outbytes + 1);
  str[outbytes] = 0;

  outbytes = 0;
  p = *pp; // start over
  for(; c > 0; c--) {
    outbytes += utf8_put(str + outbytes, p[0] | p[1] << 8);
    p+=2;
  }

  *pp = p + 2;
  return str;
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
  md4_decl(ctx);
  int len = strlen(password);
  uint8_t *d = alloca(len * 2);
  int i;

  for(i = 0; i < len; i++) {
    d[i*2]   = password[i];
    d[i*2+1] = 0;
  }
  md4_init(ctx);
  md4_update(ctx, d, len * 2);
  md4_final(ctx, digest);
}


/**
 *
 */
static void
des_key_spread(const uint8_t *normal, uint8_t spread[8])
{
  spread[ 0] = normal[ 0] & 0xfe;
  spread[ 1] = ((normal[ 0] << 7) | (normal[ 1] >> 1)) & 0xfe;
  spread[ 2] = ((normal[ 1] << 6) | (normal[ 2] >> 2)) & 0xfe;
  spread[ 3] = ((normal[ 2] << 5) | (normal[ 3] >> 3)) & 0xfe;
  spread[ 4] = ((normal[ 3] << 4) | (normal[ 4] >> 4)) & 0xfe;
  spread[ 5] = ((normal[ 4] << 3) | (normal[ 5] >> 5)) & 0xfe;
  spread[ 6] = ((normal[ 5] << 2) | (normal[ 6] >> 6)) & 0xfe;
  spread[ 7] = normal[ 6] << 1;
}


/**
 *
 */
static void
lmresponse_round(uint8_t *out, const uint8_t *challenge, const uint8_t *hash)
{
  uint8_t spread[8];
  des_key_spread(hash, spread);

#if ENABLE_OPENSSL
  DES_key_schedule sched;
  DES_set_key_unchecked((DES_cblock *)spread, &sched);
  DES_ecb_encrypt((DES_cblock *)challenge, (DES_cblock *)out, &sched, 1);
#elif ENABLE_POLARSSL

  des_context ctx;
  des_setkey_enc(&ctx, spread);
  des_crypt_ecb(&ctx, challenge, out);

#elif ENABLE_COMMONCRYPTO
  CCCryptorRef ref;
  CCCryptorCreate(kCCEncrypt, kCCAlgorithmDES, 0, spread, 8, NULL, &ref);
  size_t written;
  CCCryptorUpdate(ref, challenge, 8, out, 8, &written);
  CCCryptorRelease(ref);
#else
#error No crypto
#endif
}


/**
 *
 */
static void
lmresponse(uint8_t *out, const uint8_t *hash, const uint8_t *challenge)
{
  const uint8_t tmp[7] = {hash[14], hash[15]};

  lmresponse_round(out,      challenge, hash);
  lmresponse_round(out + 8,  challenge, hash + 7);
  lmresponse_round(out + 16, challenge, tmp);
}


/**
 *
 */
static void
smbv1_init_header(const cifs_connection_t *cc, SMB_t *h, int cmd, int flags,
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
smbv1_init_t2_header(cifs_connection_t *cc, TRANS2_req_t *t2, int cmd,
                     int param_count, int data_count, int tid)
{
  smbv1_init_header(cc, &t2->hdr, SMB_TRANS2, SMB_FLAGS_CASELESS_PATHNAMES, 0,
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
  t2->total_data_count  = htole_16(data_count);

  t2->byte_count        = htole_16(3 + param_count + data_count);
}


/**
 *
 */
static int
nbt_read(cifs_connection_t *cc, void **bufp, int *lenp)
{
  uint8_t data[4];
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
  wr16_be((void *)&nbt->length, len - 4);
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
  free(cc->cc_native_os);
  free(cc->cc_native_lanman);
  free(cc->cc_primary_domain);
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

  smbv1_init_header(cc, &req->hdr, SMB_NEG_PROTOCOL,
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
  const char *lanmgr = APPNAMEUSER;

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

    if(retry_reason && non_interactive) {
      free(domain);
      return -2;
    }

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

  } else if(as_guest == 2) {
    // Anonymous
    username = NULL;
    password_cleartext = NULL;
    free(domain);
    domain = NULL;

  } else {
    username = strdup("guest");
    password_cleartext = strdup("");
  }

  int password_pad = 0;

  if(password_cleartext != NULL) {
    uint8_t pwdigest[16];
    NTLM_hash(password_cleartext, pwdigest);
    lmresponse(password, pwdigest, cc->cc_challenge_key);
    password_len = 24;

    SMBTRACE("SETUP %s:%s:%s", username ?: "<unset>",
             *password_cleartext ? "<hidden>" : "<unset>", domain);
    free(password_cleartext);

  } else {
    SMBTRACE("SETUP anonymous");
    password_len = 0;
  }
  password_pad = cc->cc_unicode && (password_len & 1) == 0;


  ulen = username ? utf8_to_smb(cc, NULL, username) : 2;
  size_t dlen = domain ? utf8_to_smb(cc, NULL, domain) : 2;
  int bytecount = password_len + password_pad + ulen + dlen + olen + llen;
  int tlen = bytecount + sizeof(SMB_SETUP_ANDX_req_t);

  req = alloca(tlen);
  memset(req, 0, tlen);

  smbv1_init_header(cc, &req->hdr, SMB_SETUP_ANDX,
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

  memcpy(ptr, password, password_len);
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


  if(domain != NULL) {
    ptr += utf8_to_smb(cc, ptr, domain);
  } else {
    *((char *)ptr) = 0;
    ptr += 1;
    *((char *)ptr) = 0;
    ptr += 1;
  }
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


  int bc = letoh_16(reply->bytecount) - 1;

  uint8_t *data = reply->data + 1; // 1 byte pad

  cc->cc_native_os = readstring(&data, &bc, cc->cc_unicode);
  if(cc->cc_native_os != NULL) {
    cc->cc_native_lanman = readstring(&data, &bc, cc->cc_unicode);
    if(cc->cc_native_lanman != NULL) {
      cc->cc_primary_domain = readstring(&data, &bc, cc->cc_unicode);
    }
  }

  free(rbuf);

  SMBTRACE("Logged in as UID:%d guest=%s os='%s' lanman='%s' PD='%s'",
           cc->cc_uid, guest ? "yes" : "no",
           cc->cc_native_os      ? cc->cc_native_os      : "<unset>",
           cc->cc_native_lanman  ? cc->cc_native_lanman  : "<unset>",
           cc->cc_primary_domain ? cc->cc_primary_domain : "<unset>");


  if(guest && !as_guest && cc->cc_security_mode & SECURITY_USER_LEVEL) {
    retry_reason = "Login attempt failed";
    goto again;
  }

  if(as_guest != 2)
    usage_event("SMB connect", 1, NULL);

  return 0;
}


/**
 *
 */
static void
dump_request_list(cifs_connection_t *cc)
{
  nbt_req_t *nr;
  SMBTRACE("List of pending reuqests");
  LIST_FOREACH(nr, &cc->cc_pending_nbt_requests, nr_link) {
    SMBTRACE("  Pending request %d", nr->nr_mid);
  }
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
	   cc->cc_hostname, cc->cc_port, (long)hts_thread_current());

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

    if(h->pid == htole_16(3)) {
      SMBTRACE("%s:%d got echo reply", cc->cc_hostname, cc->cc_port);
      // SMB_ECHO is always transfered on PID 3
      cc->cc_wait_for_ping = 0;
    }

    // We run all requests on PID 2, so if it's not 2, free data
    if(h->pid != htole_16(2)) {
      free(buf);
      continue;
    }

    hts_mutex_lock(&smb_global_mutex);

    LIST_FOREACH(nr, &cc->cc_pending_nbt_requests, nr_link)
      if(nr->nr_mid == mid)
	break;

    if(nr != NULL) {
      SMBTRACE("%s:%d Got response for mid=%d (err:0x%08x len:%d%s)",
               cc->cc_hostname, cc->cc_port, mid,
               (int)letoh_32(h->errorcode), len,
               nr->nr_is_trans2 ? ", TRANS2" : "");

      if(nr->nr_is_trans2 && h->errorcode == 0 &&
         len >= sizeof(TRANS2_reply_t)) {

        // We do reassembly of TRANS2 here
        const TRANS2_reply_t *tr = (const TRANS2_reply_t *)buf;

        int total_count = letoh_16(tr->total_data_count);
        int seg_count = letoh_16(tr->param_count) + letoh_16(tr->data_count);
        SMBTRACE("trans2 segment: "
                 "total=%d param_count=%d data_count=%d poff=%d",
                 total_count,
                 letoh_16(tr->param_count),
                 letoh_16(tr->data_count),
                 letoh_16(tr->param_offset));

        if(seg_count > len - sizeof(TRANS2_reply_t)) {
          TRACE(TRACE_ERROR, "SMB",
                "%s:%d malformed trans2, %d > %zd",
                cc->cc_hostname, cc->cc_port,
                seg_count, (size_t)len - sizeof(TRANS2_reply_t));
          goto bad_trans2;
        }

        nr->nr_data_count += letoh_16(tr->data_count);

        if(nr->nr_response == NULL) {

          // We can't deal with any parameters that's not sent in
          // first packet
          if(tr->total_param_count != tr->param_count) {
            TRACE(TRACE_ERROR, "SMB",
                  "%s:%d Unable to reassemble trans2, param count err:%d,%d",
                  cc->cc_hostname, cc->cc_port,
                  letoh_16(tr->total_param_count),
                  letoh_16(tr->param_count));

          bad_trans2:
            nr->nr_result = 1;
            free(buf);
            free(nr->nr_response);
            nr->nr_response = NULL;
            nr->nr_response_len = 0;
            hts_cond_broadcast(&cc->cc_cond);
            hts_mutex_unlock(&smb_global_mutex);
            continue;
          }


          nr->nr_response = buf;
          nr->nr_response_len = len;
        } else {
          void *payload = buf + letoh_16(tr->param_offset);
          if(seg_count < 0) {
            TRACE(TRACE_ERROR, "SMB",
                  "%s:%d Unable to reassemble trans2, seg_count=%d",
                  cc->cc_hostname, cc->cc_port, seg_count);
            goto bad_trans2;
          }

          nr->nr_response = realloc(nr->nr_response,
                                    nr->nr_response_len + seg_count);

          memcpy(nr->nr_response + nr->nr_response_len,
                 payload, seg_count);
          nr->nr_response_len += seg_count;
          free(buf);
        }

        if(nr->nr_data_count < total_count) {
          hts_mutex_unlock(&smb_global_mutex);
          continue; // Not complete yet
        }

      } else {
        nr->nr_response = buf;
        nr->nr_response_len = len;
      }

      nr->nr_result = 0;
      hts_cond_broadcast(&cc->cc_cond);

    } else {
      SMBTRACE("%s:%d unexpected response pid=%d mid=%d on %p",
	       cc->cc_hostname, cc->cc_port, letoh_16(h->pid), mid, cc);
      dump_request_list(cc);

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

  if(ct != NULL) {
    assert(ct->ct_cc == cc);
    ct->ct_refcount++;
  } else {
    hts_mutex_unlock(&smb_global_mutex);
  }
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

    cc->cc_tc = tcp_connect(hostname, port, cc->cc_errbuf,
                            sizeof(cc->cc_errbuf), 3000, 0, NULL);

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
				     cc, THREAD_PRIO_FILESYSTEM);

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
nbt_async_req(cifs_connection_t *cc, void *request, int request_len,
              int is_trans2, const char *info)
{
  SMB_t *h = request + 4;
  nbt_req_t *nr = calloc(1, sizeof(nbt_req_t));
  nr->nr_result = -1;
  nr->nr_mid = cc->cc_mid_generator++;
  nr->nr_is_trans2 = is_trans2;
  h->pid = htole_16(2);
  h->mid = htole_16(nr->nr_mid);
  nbt_write(cc, request, request_len);

  LIST_INSERT_HEAD(&cc->cc_pending_nbt_requests, nr, nr_link);
  SMBTRACE("%s:%d %s sent mid=%d on thread %lx", cc->cc_hostname, cc->cc_port,
           info, nr->nr_mid, (long)hts_thread_current());
  return nr;
}


/**
 *
 */
static int
nbt_async_req_reply_ex(cifs_connection_t *cc,
                       void *request, int request_len,
                       void **responsep, int *response_lenp,
                       int is_trans2, const char *info)
{
  nbt_req_t *nr = nbt_async_req(cc, request, request_len, is_trans2, info);

  while(nr->nr_result == -1) {
    if(hts_cond_wait_timeout(&cc->cc_cond, &smb_global_mutex, NBT_TIMEOUT)) {
      TRACE(TRACE_ERROR, "SMB", "%s:%d request timeout (%d) on %p",
	    cc->cc_hostname, cc->cc_port, nr->nr_mid, cc);


      dump_request_list(cc);

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

#define nbt_async_req_reply(a, b, c, d, e, f) \
  nbt_async_req_reply_ex(a, b, c, d, e, f, __FUNCTION__)


/**
 *
 */
static void
cifs_release_tree(cifs_tree_t *ct, int full)
{
  ct->ct_cc->cc_auto_close = 0;
  assert(ct->ct_refcount > 0);
  ct->ct_refcount--;
  if(ct->ct_refcount > 0 || !full) {
    hts_mutex_unlock(&smb_global_mutex);
    return;
  }
  LIST_REMOVE(ct, ct_link);
  cifs_release_connection(ct->ct_cc);
  free(ct->ct_share);
  free(ct);
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
	cifs_release_tree(ct, 1);
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

    smbv1_init_header(cc, &req->hdr, SMB_TREEC_ANDX,
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

    if(nbt_async_req_reply(cc, req, tlen, &rbuf, &rlen, 0)) {
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
    cifs_release_tree(ct, 1);
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
	     char *errbuf, size_t errlen, int fa_flags,
	     cifs_tree_t **p_ct, cifs_connection_t **p_cc,
             int need_file)
{
  char hostname[128];
  int port;
  char path[512];
  cifs_connection_t *cc;
  cifs_tree_t *ct;
  char *fn = NULL, *p;

  const int non_interactive = !!(fa_flags & FA_NON_INTERACTIVE);

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

  if(need_file && strlen(filename) == 0) {
      snprintf(errbuf, errlen, "Invalid URL for operation");
      return CIFS_RESOLVE_ERROR;
  }

  ct = get_tree_no_create(hostname, port, p);

  if(ct != NULL) {

    while(ct->ct_status == CT_CONNECTING)
      hts_cond_wait(&ct->ct_cond, &smb_global_mutex);

    if(ct->ct_status == CT_RUNNING) {
      *p_ct = ct;
      return CIFS_RESOLVE_TREE;
    }
    cifs_release_tree(ct, 0);
  }

  if((cc = cifs_get_connection(hostname, port, errbuf, errlen,
			       non_interactive, 1)) != NULL) {
    assert(cc != SAMBA_NEED_AUTH); /* Should not happen if we just try to
				      login as guest */

    int security_mode = cc->cc_security_mode;

    ct = smb_tree_connect_andX(cc, p, errbuf, errlen, non_interactive);
    if(!(security_mode & SECURITY_USER_LEVEL) || ct != NULL) {
      if(ct == SAMBA_NEED_AUTH)
        return CIFS_RESOLVE_NEED_AUTH;

      if(ct == NULL)
        return CIFS_RESOLVE_ERROR;

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
  assert(ct->ct_cc == cc);
  return CIFS_RESOLVE_TREE;
}




/**
 *
 */
static int
release_tree_io_error(cifs_tree_t *ct, char *errbuf, size_t errlen)
{
  snprintf(errbuf, errlen, "I/O error");
  cifs_release_tree(ct, 1);
  return -1;
}



/**
 *
 */
static int __attribute__((unused))
release_tree_protocol_error(cifs_tree_t *ct, char *errbuf, size_t errlen)
{
  snprintf(errbuf, errlen, "Protocol error");
  cifs_release_tree(ct, 1);
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
    SMBTRACE("Error: 0x%08x", errcode);
    free(rbuf);
    cifs_release_tree(ct, 0);
    return -1;
  }

  if(rlen < runt_lim) {
    snprintf(errbuf, errlen, "Short packet");
    free(rbuf);
    cifs_release_tree(ct, 1);
    return -1;
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


static void
close_srvsvc(cifs_tree_t *ct, int fid)
{
  SMB_CLOSE_req_t *req = alloca(sizeof(SMB_CLOSE_req_t));
  memset(req, 0, sizeof(SMB_CLOSE_req_t));

  smbv1_init_header(ct->ct_cc, &req->hdr, SMB_CLOSE,
                    SMB_FLAGS_CANONICAL_PATHNAMES, 0, ct->ct_tid, 1);

  req->fid = fid;
  req->wordcount = 3;
  nbt_write(ct->ct_cc, req, sizeof(SMB_CLOSE_req_t));
}

/**
 *
 */
static int
open_srvsvc(cifs_tree_t *ct, char *errbuf, size_t errlen)
{
  cifs_connection_t *cc = ct->ct_cc;
  const char *filename = "\\srvsvc";

  int plen = utf8_to_smb(cc, NULL, filename);
  int tlen = sizeof(SMB_NTCREATE_ANDX_req_t) + plen + cc->cc_unicode;

  SMB_NTCREATE_ANDX_req_t *req = alloca(tlen);
  memset(req, 0, tlen);

  smbv1_init_header(cc, &req->hdr, SMB_NT_CREATE_ANDX,
                    SMB_FLAGS_CANONICAL_PATHNAMES |
                    SMB_FLAGS_CASELESS_PATHNAMES, 0, ct->ct_tid, 1);

  req->wordcount=24;
  req->andx_command = 0xff;
  req->access_mask = htole_32(0x2019f);
  req->file_attributes = htole_32(0);
  req->create_disposition = htole_32(1);
  req->share_access = htole_32(3);
  req->impersonation_level = htole_32(2);
  req->security_flags = 0;

  utf8_to_smb(cc, req->data + cc->cc_unicode, filename);
  req->name_len = htole_16(plen - cc->cc_unicode - 1);
  req->byte_count = htole_16(plen + cc->cc_unicode);

  void *rbuf;
  int rlen;

  if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen, 0)) {
    snprintf(errbuf, errlen, "I/O error");
    return -1;
  }

  if(check_smb_error(ct, rbuf, rlen, sizeof(SMB_NTCREATE_ANDX_resp_t),
		     errbuf, errlen))
    return -1;

  const SMB_NTCREATE_ANDX_resp_t *resp = rbuf;
  int fid = resp->fid;
  free(rbuf);
  return fid;
}


static const uint8_t bind_args[] = {
  0x00, 0x00, 0x01, 0x00, 0xc8, 0x4f, 0x32, 0x4b,
  0x70, 0x16, 0xd3, 0x01, 0x12, 0x78, 0x5a, 0x47,
  0xbf, 0x6e, 0xe1, 0x88, 0x03, 0x00, 0x00, 0x00,
  0x04, 0x5d, 0x88, 0x8a, 0xeb, 0x1c, 0xc9, 0x11,
  0x9f, 0xe8, 0x08, 0x00, 0x2b, 0x10, 0x48, 0x60,
  0x02, 0x00, 0x00, 0x00};


/**
 *
 */
static int
dcerpc_bind(cifs_tree_t *ct, int fid, char *errbuf, size_t errlen)
{
  cifs_connection_t *cc = ct->ct_cc;
  int tlen = sizeof(DCERPC_bind_req_t) + sizeof(bind_args);
  DCERPC_bind_req_t *req = alloca(tlen);
  TRANS_req_t *treq = &req->h.trans;

  memset(req, 0, sizeof(DCERPC_bind_req_t));

  smbv1_init_header(cc, &treq->hdr, SMB_TRANSACTION,
                    SMB_FLAGS_CANONICAL_PATHNAMES |
                    SMB_FLAGS_CASELESS_PATHNAMES, 0, ct->ct_tid, 1);

  treq->wordcount = 16;
  treq->total_data_count = htole_16(72);
  treq->max_data_count = htole_16(cc->cc_max_buffer_size);
  treq->param_offset = htole_16(84);
  treq->data_count = htole_16(72);
  treq->data_offset = htole_16(84);
  treq->setup_count = 2;

  req->h.function = htole_16(0x26); // TransactNmPipe
  req->h.fid = fid;
  req->h.byte_count = htole_16(89);
  memcpy(req->h.name, "\\\000P\000I\000P\000E\x00\\\000\000", 14);

  req->h.rpc.major_version = 5;
  req->h.rpc.type = 0xb; // Bind
  req->h.rpc.flags = 0x3;
  req->h.rpc.data_representation = htole_32(0x10);
  req->h.rpc.frag_length = htole_16(72);
  req->h.rpc.callid = htole_32(1);

  req->max_xmit_frag = treq->max_data_count;
  req->max_recv_frag = treq->max_data_count;
  req->num_ctx_items = 1;
  memcpy(req->payload, bind_args, sizeof(bind_args));

  void *rbuf;
  int rlen;

  if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen, 0)) {
    snprintf(errbuf, errlen, "I/O error");
    return -1;
  }
  return 0;
}


/**
 *
 */
static const uint8_t enumargs[] = {
  0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x04, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
  0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};

static int
parse_enum_shares(const uint8_t *data, int len, cifs_connection_t *cc,
                  fa_dir_t *fd)
{
  int count;
  char sharename[310];
  char comment[310];
  char url[512];

  if(len < 24)
    return -1;
  data += 20;
  len -= 20;
  int num_shares = rd32_le(data);
  data += 4;
  len -= 4;

  if(num_shares > 256)
    return -1;

  snprintf(url, sizeof(url), "smb://%s", cc->cc_hostname);
  if(cc->cc_port != 445)
    snprintf(url + strlen(url), sizeof(url) - strlen(url), ":%d", cc->cc_port);

  char *urlbase = url + strlen(url);
  int urlspace = sizeof(url) - strlen(url);

  uint32_t *typearray = alloca(sizeof(int) * num_shares);

  for(int i = 0; i < num_shares; i++) {
    // Each entry consumes 12 bytes
    if(len < 12)
      return -1;
    typearray[i] = rd32_le(data + 4);
    len -= 12;
    data += 12;
  }

  for(int i = 0; i < num_shares; i++) {
    if(len < 12)
      return -1;

    count = rd32_le(data + 8);

    if(count < 1 || count > 64)
      return -1;

    len -= 12;
    data += 12;

    ucs2_to_utf8((uint8_t *)sharename, sizeof(sharename), data, (count-1)*2, 1);

    count = (count + 1) & ~1;

    len -=  count * 2;
    data += count * 2;

    if(len < 12)
      return -1;

    count = rd32_le(data + 8);

    if(count < 1 || count > 100)
      return -1;

    len -= 12;
    data += 12;

    ucs2_to_utf8((uint8_t *)comment, sizeof(comment), data, (count-1)*2, 1);

    count = (count + 1) & ~1;
    len -=  count * 2;
    data += count * 2;


    snprintf(urlbase, urlspace, "/%s", sharename);
    SMBTRACE("Enumerated share %s (%s) -> %s type=%x",
             sharename, comment, url, typearray[i]);

    if(typearray[i] == 0) {
      // 0 for DISKTREE shares
      fa_dir_add(fd, url, sharename, CONTENT_SHARE);
    }
  }
  return 0;
}

/**
 *
 */
static int
dcerpc_enum_shares(cifs_tree_t *ct, int fid, char *errbuf, size_t errlen,
                   fa_dir_t *fd)
{
  cifs_connection_t *cc = ct->ct_cc;
  const char *servername = cc->cc_hostname;

  int servernamechars = strlen(servername) + 1;
  int snlen = utf8_to_smb(cc, NULL, servername);
  snlen = (snlen + 3) & ~3;
  int arglen = 16 + snlen + 32;
  int tlen = sizeof(DCERPC_enum_shares_req_t) + arglen;
  DCERPC_enum_shares_req_t *req = alloca(tlen);
  TRANS_req_t *treq = &req->h.trans;

  memset(req, 0, tlen);

  smbv1_init_header(cc, &treq->hdr, SMB_TRANSACTION,
                    SMB_FLAGS_CANONICAL_PATHNAMES |
                    SMB_FLAGS_CASELESS_PATHNAMES, 0, ct->ct_tid, 1);

  int frag_len = arglen + 24;

  treq->wordcount = 16;
  treq->total_data_count = htole_16(frag_len);
  treq->max_data_count = htole_16(cc->cc_max_buffer_size);
  treq->param_offset = htole_16(84);
  treq->data_count = htole_16(frag_len);
  treq->data_offset = htole_16(84);
  treq->setup_count = 2;

  req->h.function = htole_16(0x26); // TransactNmPipe
  req->h.fid = fid;
  req->h.byte_count = htole_16(frag_len + 17);
  memcpy(req->h.name, "\\\000P\000I\000P\000E\x00\\\000\000", 14);

  req->h.rpc.major_version = 5;
  req->h.rpc.type = 0x0; // Request
  req->h.rpc.flags = 0x3;
  req->h.rpc.data_representation = htole_32(0x10);
  req->h.rpc.frag_length = htole_16(frag_len);
  req->h.rpc.callid = htole_32(2);

  req->alloc_hint = htole_32(68);
  req->context_id = 0;
  req->opnum = htole_16(15); // NetShareEnumAll

  uint8_t *p = req->payload;

  wr32_le(p + 0, 0x20000);
  wr32_le(p + 4, servernamechars);
  wr32_le(p + 12, servernamechars);
  p += 16;
  utf8_to_smb(cc, p, servername);
  p += snlen;
  memcpy(p, enumargs, 32);

  void *rbuf;
  int rlen;

  if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen, 0)) {
    snprintf(errbuf, errlen, "I/O error");
    return -1;
  }

  if(rlen <  sizeof(TRANS_reply_t)) {
    snprintf(errbuf, errlen, "Short packet");
    goto bad;
  }

  const TRANS_reply_t *treply = rbuf;
  int data_offset = letoh_16(treply->data_offset);
  int data_len = rlen - data_offset;
  if(data_len < sizeof(DCERPC_enum_shares_reply_t)) {
    snprintf(errbuf, errlen, "Short enumshare reply");
    goto bad;
  }

  const DCERPC_enum_shares_reply_t *reply = rbuf + data_offset;

  if(reply->hdr.flags != 3) {
    snprintf(errbuf, errlen, "Fragmented enumshare replies not supported");
    goto bad;
  }


  parse_enum_shares(reply->payload,
                    data_len - sizeof(DCERPC_enum_shares_reply_t), cc, fd);

  free(rbuf);
  close_srvsvc(ct, fid),
  cifs_release_tree(ct, 0);
  return 0;


 bad:
  free(rbuf);
  close_srvsvc(ct, fid),
  cifs_release_tree(ct, 0);
  return -1;
}



/**
 *
 */
static int
cifs_enum_shares(cifs_connection_t *cc, fa_dir_t *fd,
		 char *errbuf, size_t errlen)
{
  cifs_tree_t *ct;

  ct = smb_tree_connect_andX(cc, "IPC$", errbuf, errlen, 0);
  if(ct == NULL)
    return -1;

  int fid = open_srvsvc(ct, errbuf, errlen);
  if(fid < 0)
    return -1;

  if(dcerpc_bind(ct, fid, errbuf, errlen) < 0)
    return -1;

  return dcerpc_enum_shares(ct, fid, errbuf, errlen, fd);
}


/**
 *
 */
static int
cifs_delete(const char *url, char *errbuf, size_t errlen, int dir)
{
  char filename[512];
  int r;
  cifs_tree_t *ct;
  cifs_connection_t *cc;

  r = cifs_resolve(url, filename, sizeof(filename), errbuf, errlen,
		   0, &ct, &cc, 1);

  if(r != CIFS_RESOLVE_TREE)
    return -1;

  backslashify(filename);
  cc = ct->ct_cc;

  int plen = utf8_to_smb(cc, NULL, filename);
  int tlen;
  void *reqbuf;
  if(dir) {

    tlen = sizeof(SMB_DELETE_DIR_req_t) + plen;
    SMB_DELETE_DIR_req_t *req = reqbuf = alloca(tlen);
    memset(req, 0, tlen);
    smbv1_init_header(cc, &req->hdr, SMB_DELETE_DIR,
                      SMB_FLAGS_CANONICAL_PATHNAMES, 0, ct->ct_tid, 1);
    utf8_to_smb(cc, req->data, filename);
    req->byte_count = htole_16(plen);

  } else {

    tlen = sizeof(SMB_DELETE_FILE_req_t) + plen;
    SMB_DELETE_FILE_req_t *req = reqbuf = alloca(tlen);
    memset(req, 0, tlen);
    smbv1_init_header(cc, &req->hdr, SMB_DELETE_FILE,
                      SMB_FLAGS_CANONICAL_PATHNAMES, 0, ct->ct_tid, 1);
    req->word_count = 1;
    req->buffer_format = 4;
    utf8_to_smb(cc, req->data, filename);
    req->byte_count = htole_16(plen);
  }

  void *rbuf;
  int rlen;

  if(nbt_async_req_reply(ct->ct_cc, reqbuf, tlen, &rbuf, &rlen, 0)) {
    snprintf(errbuf, errlen, "I/O error");
    cifs_release_tree(ct, 1);
    return -1;
  }

  if(check_smb_error(ct, rbuf, rlen, sizeof(SMB_t), errbuf, errlen))
    return -1;

  cifs_release_tree(ct, 0);
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
    smbv1_init_t2_header(ct->ct_cc, &req->t2, TRANS2_QUERY_PATH_INFORMATION,
                         6 + plen, 0, ct->ct_tid);

    req->level_of_interest = htole_16(0x101+i);
    utf8_to_smb(ct->ct_cc, req->data, fname);

    if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen, 1))
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

      smbv1_init_t2_header(ct->ct_cc, &req->t2, TRANS2_FIND_FIRST2,
                           12 + plen, 0, ct->ct_tid);

      req->first.search_attribs    = htole_16(ATTR_READONLY |
					      ATTR_DIRECTORY |
					      ATTR_ARCHIVE);
      req->first.search_count      = htole_16(search_count);
      req->first.flags             = htole_16(6);
      req->first.level_of_interest = htole_16(260);
      utf8_to_smb(ct->ct_cc, req->data, (char *)fname);

    } else {

      smbv1_init_t2_header(ct->ct_cc, &req->t2, TRANS2_FIND_NEXT2,
                           12 + 1, 0, ct->ct_tid);

      req->next.search_id = search_id;

      req->next.search_count      = htole_16(search_count);
      req->next.flags             = htole_16(0xe);
      req->next.level_of_interest = htole_16(260);

      req->data[0] = 0;
      tlen = sizeof(SMB_TRANS2_FIND_req_t) + 1;
    }

    if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen, 1))
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
smb_scandir(fa_protocol_t *fap, fa_dir_t *fa, const char *url,
            char *errbuf, size_t errlen, int flags)
{
  char filename[512];
  cifs_tree_t *ct;
  cifs_connection_t *cc;
  int r;
  r = cifs_resolve(url, filename, sizeof(filename), errbuf, errlen,
                   flags, &ct, &cc, 0);
  switch(r) {
  default:
    return -1;

  case CIFS_RESOLVE_TREE:
    if(cifs_scandir(ct, filename, fa, errbuf, errlen))
      return -1;

    cifs_release_tree(ct, 0);
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
	 int flags, struct fa_open_extra *foe)
{
  char filename[512];
  cifs_tree_t *ct;
  SMB_NTCREATE_ANDX_req_t *req;
  const SMB_NTCREATE_ANDX_resp_t *resp;
  smb_file_t *sf;

  int r = cifs_resolve(url, filename, sizeof(filename), errbuf, errlen, flags,
		       &ct, NULL, 1);
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

  smbv1_init_header(cc, &req->hdr, SMB_NT_CREATE_ANDX,
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

  if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen, 0)) {
    snprintf(errbuf, errlen, "I/O error");
    cifs_release_tree(ct, 1);
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

  smbv1_init_header(ct->ct_cc, &req->hdr, SMB_CLOSE,
                    SMB_FLAGS_CANONICAL_PATHNAMES, 0, ct->ct_tid, 1);

  req->fid = sf->sf_fid;
  req->wordcount = 3;
  nbt_write(ct->ct_cc, req, sizeof(SMB_CLOSE_req_t));
  cifs_release_tree(sf->sf_ct, 0);
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

  if(sf->sf_pos >= sf->sf_file_size)
    return 0;

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
    smbv1_init_header(ct->ct_cc, &req->hdr, SMB_READ_ANDX,
                      SMB_FLAGS_CANONICAL_PATHNAMES, 0, ct->ct_tid, 1);

    req->fid = sf->sf_fid;
    int64_t pos = sf->sf_pos + total;
    req->offset_low = htole_32((uint32_t)pos);
    req->offset_high = htole_32((uint32_t)(pos >> 32));
    req->max_count_low = htole_16(cnt & 0xffff);
    req->max_count_high = htole_32(cnt >> 16);
    req->wordcount = 12;
    req->andx_command = 0xff;

    nr = nbt_async_req(ct->ct_cc, req, sizeof(SMB_READ_ANDX_req_t), 0,
                       "read");
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
      if(hts_cond_wait_timeout(&ct->ct_cc->cc_cond, &smb_global_mutex,
                               NBT_TIMEOUT)) {
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
smb_seek(fa_handle_t *fh, int64_t pos, int whence, int lazy)
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
	 int flags, char *errbuf, size_t errlen)
{
  char filename[512];
  int r;
  cifs_tree_t *ct;
  cifs_connection_t *cc;
  r = cifs_resolve(url, filename, sizeof(filename), errbuf, errlen,
		   flags, &ct, &cc, 0);

  switch(r) {
  default:
    return FAP_ERROR;

  case CIFS_RESOLVE_NEED_AUTH:
    return FAP_NEED_AUTH;

  case CIFS_RESOLVE_TREE:
    if(cifs_stat(ct, filename, fs, errbuf, errlen))
      return FAP_ERROR;
    cifs_release_tree(ct, 0);
    return FAP_OK;

  case CIFS_RESOLVE_CONNECTION:
    memset(fs, 0, sizeof(struct fa_stat));
    fs->fs_size = 0;
    fs->fs_mtime = 0;
    fs->fs_type = CONTENT_SHARE;
    cc->cc_refcount--;
    hts_mutex_unlock(&smb_global_mutex);
    return FAP_OK;
  }
}


/**
 *
 */
static int
smb_unlink(const fa_protocol_t *fap, const char *url,
           char *errbuf, size_t errlen)
{
  return cifs_delete(url, errbuf, errlen, 0);
}

/**
 *
 */
static int
smb_rmdir(const fa_protocol_t *fap, const char *url,
          char *errbuf, size_t errlen)
{
  return cifs_delete(url, errbuf, errlen, 1);
}

/**
 * Simple helper for setting one one EA
 */
typedef struct eahdr {
  uint32_t list_len;
  uint8_t ea_flags;
  uint8_t name_len;
  uint16_t data_len;
  char data[0];

} __attribute__((packed)) eahdr_t;

/**
 * Set extended attribute
 */
static fa_err_code_t
smb_set_xattr(struct fa_protocol *fap, const char *url,
              const char *name,
              const void *data, size_t data_len)
{
  char filename[512];
  int r;
  cifs_tree_t *ct;
  const int name_len = strlen(name);

  if(data == NULL)
    data_len = 0;

  r = cifs_resolve(url, filename, sizeof(filename), NULL, 0,
                   FA_NON_INTERACTIVE, &ct, NULL, 1);

  if(r != CIFS_RESOLVE_TREE)
    return -1;

  backslashify(filename);

  int plen = utf8_to_smb(ct->ct_cc, NULL, filename);
  int dlen = sizeof(eahdr_t) + name_len + 1 + data_len;
  int tlen = sizeof(SMB_TRANS2_PATH_QUERY_req_t) + plen + dlen;
  void *rbuf;
  int rlen;
  SMB_TRANS2_PATH_QUERY_req_t *req = alloca(tlen);

  memset(req, 0, tlen);
  smbv1_init_t2_header(ct->ct_cc, &req->t2, TRANS2_SET_PATH_INFORMATION,
                       6 + plen, dlen, ct->ct_tid);

  req->level_of_interest = htole_16(2); // SMB_SET_FILE_EA
  utf8_to_smb(ct->ct_cc, req->data, filename);
  eahdr_t *ea = (void *)(req->data + plen);
  ea->list_len = htole_32(dlen);
  ea->name_len = name_len;
  ea->data_len = htole_16(data_len);
  memcpy(ea->data, name, name_len + 1);
  if(data != NULL)
    memcpy(ea->data + name_len + 1, data, data_len);

  if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen, 1))
    return release_tree_io_error(ct, NULL, 0);

  const SMB_t *reply = rbuf;
  uint32_t errcode = letoh_32(reply->errorcode);
  free(rbuf);
  cifs_release_tree(ct, 0);

  if(errcode)
    return FAP_NOT_SUPPORTED;

  return FAP_OK;
}

/**
 * Simple helper for getting one one EA
 */
typedef struct get_eahdr {
  uint32_t list_len;
  uint8_t name_len;
  char data[0];
} __attribute__((packed)) get_eahdr_t;

/**
 * Get extended attribute
 */
static fa_err_code_t
smb_get_xattr(struct fa_protocol *fap, const char *url,
              const char *name,
              void **datap, size_t *lenp)
{
  char filename[512];
  int r;
  cifs_tree_t *ct;
  const int name_len = strlen(name);

  r = cifs_resolve(url, filename, sizeof(filename), NULL, 0,
                   FA_NON_INTERACTIVE, &ct, NULL, 1);

  if(r != CIFS_RESOLVE_TREE)
    return -1;

  backslashify(filename);
  int plen = utf8_to_smb(ct->ct_cc, NULL, filename);
  int dlen = sizeof(get_eahdr_t) + name_len + 1;
  int tlen = sizeof(SMB_TRANS2_PATH_QUERY_req_t) + plen + dlen;
  void *rbuf;
  int rlen;
  SMB_TRANS2_PATH_QUERY_req_t *req = alloca(tlen);

  memset(req, 0, tlen);
  smbv1_init_t2_header(ct->ct_cc, &req->t2, TRANS2_QUERY_PATH_INFORMATION,
                       6 + plen, dlen, ct->ct_tid);

  req->level_of_interest = htole_16(3); // SMB_INFO_QUERY_EAS_FROM_LIST
  utf8_to_smb(ct->ct_cc, req->data, filename);

  get_eahdr_t *ea = (void *)(req->data + plen);
  ea->list_len = htole_32(dlen);
  ea->name_len = name_len;
  memcpy(ea->data, name, name_len + 1);

  if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen, 1))
    return release_tree_io_error(ct, NULL, 0);

  const TRANS2_reply_t *t2resp = rbuf;

  uint32_t errcode = letoh_32(t2resp->hdr.errorcode);
  int retcode;

  if(errcode) {
    retcode = FAP_ERROR;
  } else {

    int offset = letoh_16(t2resp->data_offset);
    int len    = letoh_16(t2resp->data_count);


    if(len < sizeof(eahdr_t)) {
      retcode = FAP_ERROR;
    } else {
      retcode = FAP_OK;
      const eahdr_t *ea = (void *)(rbuf + offset);
      const int dlen = letoh_16(ea->data_len);
      if(dlen > 0) {
        *datap = malloc(dlen);
        memcpy(*datap, ea->data + ea->name_len + 1, dlen);
        *lenp = dlen;
      } else {
        *datap = NULL;
        *lenp = 0;
      }
    }
  }
  free(rbuf);
  cifs_release_tree(ct, 0);
  return retcode;
}



/**
 *
 */
static void
cifs_periodic(struct callout *c, void *opaque)
{
  cifs_connection_t *cc = opaque;

  EchoRequest_t *req = alloca(sizeof(EchoRequest_t) + 2);
  memset(req, 0, sizeof(EchoRequest_t) + 2);

  hts_mutex_lock(&smb_global_mutex);

  smbv1_init_header(cc, &req->hdr, SMB_ECHO, 0, 0, 0, 1);
  req->wordcount = 1;
  req->echo_count = htole_16(1);
  req->byte_count = htole_16(2);
  req->data[0] = 0x13;
  req->data[1] = 0x37;

  req->hdr.pid = htole_16(3); // PING
  nbt_write(cc, req, sizeof(EchoRequest_t) + 2);

  if(cc->cc_wait_for_ping) {
    cc->cc_broken = 1;
    SMBTRACE("%s:%d no ping response", cc->cc_hostname, cc->cc_port);
  }

  cc->cc_wait_for_ping = 1;

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
static char **
smb_NetServerEnum2(cifs_tree_t *ct, char *errbuf, size_t errlen)
{
  cifs_connection_t *cc = ct->ct_cc;
  const char *domain = cc->cc_primary_domain ?: "WORKGROUP";
  int dlen = strlen(domain) + 1;

  int tlen = sizeof(SMB_enum_servers_req_t) + dlen;
  SMB_enum_servers_req_t *req = alloca(tlen);

  memset(req, 0, tlen);

  smbv1_init_header(cc, &req->trans.hdr, SMB_TRANSACTION,
                    SMB_FLAGS_CANONICAL_PATHNAMES |
                    SMB_FLAGS_CASELESS_PATHNAMES, 0, ct->ct_tid, 1);

  req->trans.wordcount = 14;
  req->trans.total_param_count = htole_16(26 + dlen);
  req->trans.max_param_count = htole_16(8);
  req->trans.max_data_count = htole_16(65535);
  req->trans.param_count = htole_16(26 + dlen);
  req->trans.param_offset = htole_16(92);
  req->bytecount = htole_16(55 + dlen);
  memcpy(req->transaction_name,
         "\\\000P\000I\000P\000E\000\\\000L\000A\000N\000M\000A\000N\000\000\000"
         ,26);

  req->function_code = htole_16(104); // NetServerEnum2
  memcpy(req->parameter_desc, "WrLehDz", 8);
  memcpy(req->return_desc, "B16BBDz", 8);
  req->detail_level = htole_16(1);
  req->receive_buffer_length = htole_16(65535);
  req->server_type = -1;
  strcpy(req->domain, domain);

  void *rbuf;
  int rlen;

  if(nbt_async_req_reply(ct->ct_cc, req, tlen, &rbuf, &rlen, 0)) {
    snprintf(errbuf, errlen, "I/O error");
    return NULL;
  }

  if(check_smb_error(ct, rbuf, rlen, sizeof(TRANS_reply_t), errbuf, errlen))
    return NULL;

  const TRANS_reply_t *treply = rbuf;
  int param_offset = letoh_16(treply->param_offset);
  int param_count = letoh_16(treply->param_count);


  if(param_count < 8 || param_offset + 8 > rlen) {
    snprintf(errbuf, errlen, "Bad params %d %d", param_offset, param_count);
    goto bad;
  }

  int entries = rd16_le(rbuf + param_offset + 6);

  if(entries > 256) {
    snprintf(errbuf, errlen, "Too many servers");
    goto bad;
  }

  char **rvec = calloc(2 * (entries + 1), sizeof(char *));

  int data_offset = letoh_16(treply->data_offset);
  const uint8_t *items = rbuf + data_offset;
  int remain = rlen - data_offset;

  char servername[17];
  servername[16] = 0;
  for(int i = 0; i < entries; i++) {
    if(remain < 26)
      break;
    memcpy(servername, items, 16);
    SMBTRACE("Found server %s\n", servername);

    remain -= 26;
    items += 26;
    rvec[i * 2 + 0] = strdup(servername);
    rvec[i * 2 + 1] = strdup(domain);
  }

  free(rbuf);
  return rvec;

 bad:
  free(rbuf);
  cifs_release_tree(ct, 0);
  return NULL;
}


/**
 *
 */
char **
smb_enum_servers(const char *hostname)
{
  char errbuf[256];
  cifs_connection_t *cc;
  int port = 445;

  cc = cifs_get_connection(hostname, port, errbuf, sizeof(errbuf), 1, 2);
  if(cc == SAMBA_NEED_AUTH)
    return NULL;
  if(cc == NULL) {
    SMBTRACE("Unable to connect to %s:%d for network listings -- %s",
             hostname, port, errbuf);
    return NULL;
  }

  cifs_tree_t *ct;

  ct = smb_tree_connect_andX(cc, "IPC$", errbuf, sizeof(errbuf), 1);
  if(ct == SAMBA_NEED_AUTH || ct == NULL)
    return NULL;

  char **servers = smb_NetServerEnum2(ct, errbuf, sizeof(errbuf));

  if(servers == NULL) {
    SMBTRACE("Failed to enumerate network servers at %s:%d -- %s",
             hostname, port, errbuf);
    return NULL;
  }
  cifs_release_tree(ct, 1);
  return servers;
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
 *
 */
static int
smb_no_parking(fa_handle_t *fh)
{
  return 1;
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
  .fap_unlink= smb_unlink,
  .fap_rmdir = smb_rmdir,
  .fap_set_xattr = smb_set_xattr,
  .fap_get_xattr = smb_get_xattr,
  .fap_no_parking = smb_no_parking,
};
FAP_REGISTER(smb);
