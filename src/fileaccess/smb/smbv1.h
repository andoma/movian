#pragma once

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

} __attribute__((packed)) TRANS_req_t;


typedef struct {
  uint8_t major_version;
  uint8_t minor_version;
  uint8_t type;
  uint8_t flags;
  uint32_t data_representation;
  uint16_t frag_length;
  uint16_t auth_length;
  uint32_t callid;
} __attribute__((packed)) DCERPC_hdr_t;

typedef struct {
  TRANS_req_t trans;
  uint16_t function;
  uint16_t fid;
  uint16_t byte_count;
  uint8_t pad1;
  char name[14];
  uint16_t pad2;

  DCERPC_hdr_t rpc;

} __attribute__((packed)) DCERPC_req_t;


typedef struct {

  DCERPC_req_t h;

  uint16_t max_xmit_frag;
  uint16_t max_recv_frag;
  uint32_t association_group;
  uint8_t num_ctx_items;

  uint8_t pad3[3];

  char payload[0];

} __attribute__((packed)) DCERPC_bind_req_t;


typedef struct {

  DCERPC_req_t h;

  uint32_t alloc_hint;
  uint16_t context_id;
  uint16_t opnum;
  uint8_t payload[0];

} __attribute__((packed)) DCERPC_enum_shares_req_t;


typedef struct {

  DCERPC_hdr_t hdr;
  uint32_t alloc_hint;
  uint16_t context_id;
  uint8_t cancel_count;
  uint8_t pad;
  uint8_t payload[0];

} __attribute__((packed)) DCERPC_enum_shares_reply_t;




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


typedef struct {
  NBT_t nbt;
  SMB_t hdr;
  uint8_t word_count;
  uint16_t search_attributes;
  uint16_t byte_count;
  uint8_t buffer_format;
  uint8_t data[0];
} __attribute__((packed)) SMB_DELETE_FILE_req_t;


typedef struct {
  NBT_t nbt;
  SMB_t hdr;
  uint8_t word_count;
  uint16_t byte_count;
  uint8_t buffer_format;
  uint8_t data[0];
} __attribute__((packed)) SMB_DELETE_DIR_req_t;


#define NBT_SESSION_MSG 0x00



#define SMB_PROTO 0x424d53ff

#define SMB_DELETE_DIR     0x01
#define SMB_CLOSE          0x04
#define SMB_DELETE_FILE    0x06
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
#define TRANS2_SET_PATH_INFORMATION   6

