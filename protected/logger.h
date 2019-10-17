/* logging functions */
#ifndef LOGGER_H
#define LOGGER_H

#include "bipbuffer.h"

/* TODO: starttime tunable */
#define LOGGER_BUF_SIZE 1024 * 64
#define LOGGER_WATCHER_BUF_SIZE 1024 * 256
#define LOGGER_ENTRY_MAX_SIZE 2048
#define GET_LOGGER() ((logger *) pthread_getspecific(logger_key));

/* Inlined from memcached.h - should go into sub header */
using rel_time_t = unsigned int;

enum log_entry_type {
    LOGGER_ASCII_CMD = 0,
    LOGGER_EVICTION,
    LOGGER_ITEM_GET,
    LOGGER_ITEM_STORE,
    LOGGER_CRAWLER_STATUS,
    LOGGER_SLAB_MOVE,
};

enum log_entry_subtype {
    LOGGER_TEXT_ENTRY = 0,
    LOGGER_EVICTION_ENTRY,
    LOGGER_ITEM_GET_ENTRY,
    LOGGER_ITEM_STORE_ENTRY,
};

enum logger_ret_type {
    LOGGER_RET_OK = 0,
    LOGGER_RET_NOSPACE,
    LOGGER_RET_ERR
};

enum logger_parse_entry_ret {
    LOGGER_PARSE_ENTRY_OK = 0,
    LOGGER_PARSE_ENTRY_FULLBUF,
    LOGGER_PARSE_ENTRY_FAILED
};

struct entry_details {
    enum log_entry_subtype subtype;
    int reqlen;
    uint16_t eflags;
    char *format;
};

/* log entry intermediary structures */
struct logentry_eviction {
    long long int exptime;
    uint32_t latime;
    uint16_t it_flags;
    uint8_t nkey;
    uint8_t clsid;
    char key[];
};
struct logentry_item_get {
    uint8_t was_found;
    uint8_t nkey;
    uint8_t clsid;
    char key[];
};

struct logentry_item_store {
    int status;
    int cmd;
    rel_time_t ttl;
    uint8_t nkey;
    uint8_t clsid;
    char key[];
};

/* end intermediary structures */

struct logentry {
    enum log_entry_subtype event;
    uint8_t pad;
    uint16_t eflags;
    uint64_t gid;
    struct timeval tv; /* not monotonic! */
    int size;
    union {
        void *entry; /* probably an item */
        char end;
    } data[];
};

#define LOG_SYSEVENTS  (1<<1) /* threads start/stop/working */
#define LOG_FETCHERS   (1<<2) /* get/gets/etc */
#define LOG_MUTATIONS  (1<<3) /* set/append/incr/etc */
#define LOG_SYSERRORS  (1<<4) /* malloc/etc errors */
#define LOG_CONNEVENTS (1<<5) /* new client, closed, etc */
#define LOG_EVICTIONS  (1<<6) /* details of evicted items */
#define LOG_STRICT     (1<<7) /* block worker instead of drop */
#define LOG_RAWCMDS    (1<<9) /* raw ascii commands */

struct logger {
    logger *prev;
    logger *next;
    pthread_mutex_t mutex; /* guard for this + *buf */
    uint64_t written; /* entries written to the buffer */
    uint64_t dropped; /* entries dropped */
    uint64_t blocked; /* times blocked instead of dropped */
    uint16_t fetcher_ratio; /* log one out of every N fetches */
    uint16_t mutation_ratio; /* log one out of every N mutations */
    uint16_t eflags; /* flags this logger should log */
    bipbuf_t *buf;
    const entry_details *entry_map;
};

enum logger_watcher_type {
    LOGGER_WATCHER_STDERR = 0,
    LOGGER_WATCHER_CLIENT = 1
};

#endif
