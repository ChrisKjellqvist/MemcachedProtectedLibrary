/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  memcached - memory caching daemon
 *
 *       http://www.memcached.org/
 *
 *  Copyright 2003 Danga Interactive, Inc.  All rights reserved.
 *
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 *
 *  Authors:
 *      Anatoly Vorobey <mellon@pobox.com>
 *      Brad Fitzpatrick <brad@danga.com>
 */
#include "memcached.h"
#include <sys/param.h>
#include <ctype.h>

/* some POSIX systems need the following definition
 * to get mlockall flags out of sys/mman.h.  */
#ifndef _P1003_1B_VISIBLE
#define _P1003_1B_VISIBLE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <sysexits.h>

#include "persistence.h"

/*
 * forward declarations
 */

/* THREADCACHED */
#include <utility>
int is_restart;

/* defaults */
static void settings_init(void);

std::atomic<int> *end_signal;
pthread_mutex_t begin_ops_mutex = PTHREAD_MUTEX_INITIALIZER;
/** exported globals **/
struct stats stats;
struct stats_state stats_state;
struct settings settings;
time_t process_started;     /* when the process was started */
struct slab_rebalance slab_rebal;
volatile int slab_rebalance_signal;
static struct event_base *main_base;

enum transmit_result {
  TRANSMIT_COMPLETE,   /** All done writing. */
  TRANSMIT_INCOMPLETE, /** More data remaining to write. */
  TRANSMIT_SOFT_ERROR, /** Can't write any more right now. */
  TRANSMIT_HARD_ERROR  /** Can't write (c->state is set to conn_closing) */
};

#define REALTIME_MAXDELTA 60*60*24*30


/*
 * given time value that's either unix time or delta from current unix time, return
 * unix time. Use the fact that delta can't exceed one month (and real time value can't
 * be that low).
 */
static rel_time_t realtime(const time_t exptime) {
  /* no. of seconds in 30 days - largest possible delta exptime */

  if (exptime == 0) return 0; /* 0 means never expire */

  if (exptime > REALTIME_MAXDELTA) {
    /* if item expiration is at/before the server started, give it an
       expiration time of 1 second after the server started.
       (because 0 means don't expire).  without this, we'd
       underflow and wrap around to some large value way in the
       future, effectively making items expiring in the past
       really expiring never */
    if (exptime <= process_started)
      return (rel_time_t)1;
    return (rel_time_t)(exptime - process_started);
  } else {
    return (rel_time_t)(exptime + *current_time);
  }
}

static void settings_init(void) {
  /* By default this string should be NULL for getaddrinfo() */
  settings.maxbytes = 64 * 1024 * 1024; /* default is 64MB */
  settings.oldest_live = 0;
  settings.oldest_cas = 0;          /* supplements accuracy of oldest_live */
  settings.evict_to_free = 1;       /* push old items out of cache when memory runs out */
  settings.factor = 1.25;
  settings.chunk_size = 48;         /* space for a modest key and value */
  settings.prefix_delimiter = ':';
  settings.reqs_per_event = 20;
  settings.item_size_max = 1024 * 1024; /* The famous 1MB upper limit. */
  settings.slab_page_size = 1024 * 1024;
  settings.slab_chunk_size_max = settings.slab_page_size / 2;
  settings.lru_crawler = false;
  settings.lru_crawler_sleep = 100;
  settings.lru_crawler_tocrawl = 0;
  settings.hot_max_factor = 0.2;
  settings.warm_max_factor = 2.0;
  settings.hashpower_init = 0;
  settings.tail_repair_time = TAIL_REPAIR_TIME_DEFAULT;
  settings.crawls_persleep = 1000;
  settings.slab_automove_ratio = .8;
  settings.slab_automove_window = 30;
}

/* Destination must always be chunked */
/* This should be part of item.c */
static int _store_item_copy_chunks(item *d_it, item *s_it, const int len) {
  item_chunk *dch = (item_chunk *) ITEM_schunk(d_it);
  /* Advance dch until we find free space */
  while (dch->size == dch->used) {
    if (dch->next != nullptr) {
      dch = dch->next;
    } else {
      break;
    }
  }

  if (s_it->it_flags & ITEM_CHUNKED) {
    int remain = len;
    item_chunk *sch = (item_chunk *) ITEM_schunk(s_it);
    int copied = 0;
    /* Fills dch's to capacity, not straight copy sch in case data is
     * being added or removed (ie append/prepend)
     */
    while (sch && dch && remain) {
      assert(dch->used <= dch->size);
      int todo = (dch->size - dch->used < sch->used - copied)
	? dch->size - dch->used : sch->used - copied;
      if (remain < todo)
	todo = remain;
      memcpy(dch->data + dch->used, sch->data + copied, todo);
      dch->used += todo;
      copied += todo;
      remain -= todo;
      assert(dch->used <= dch->size);
      if (dch->size == dch->used) {
	item_chunk *tch = do_item_alloc_chunk(dch, remain);
	if (tch) {
	  dch = tch;
	} else {
	  return -1;
	}
      }
      assert(copied <= sch->used);
      if (copied == sch->used) {
	copied = 0;
	sch = sch->next;
      }
    }
    /* assert that the destination had enough space for the source */
    assert(remain == 0);
  } else {
    int done = 0;
    /* Fill dch's via a non-chunked item. */
    while (len > done && dch) {
      int todo = (dch->size - dch->used < len - done)
	? dch->size - dch->used : len - done;
      //assert(dch->size - dch->used != 0);
      memcpy(dch->data + dch->used, ITEM_data(s_it) + done, todo);
      done += todo;
      dch->used += todo;
      assert(dch->used <= dch->size);
      if (dch->size == dch->used) {
	item_chunk *tch = do_item_alloc_chunk(dch, len - done);
	if (tch) {
	  dch = tch;
	} else {
	  return -1;
	}
      }
    }
    assert(len == done);
  }
  return 0;
}

static int _store_item_copy_data(int comm, item *old_it, item *new_it, item *add_it) {
  if (comm == NREAD_APPEND) {
    if (new_it->it_flags & ITEM_CHUNKED) {
      if (_store_item_copy_chunks(new_it, old_it, old_it->nbytes - 2) == -1 ||
	  _store_item_copy_chunks(new_it, add_it, add_it->nbytes) == -1) {
	return -1;
      }
    } else {
      memcpy(ITEM_data(new_it), ITEM_data(old_it), old_it->nbytes);
      memcpy(ITEM_data(new_it) + old_it->nbytes - 2 /* CRLF */, ITEM_data(add_it), add_it->nbytes);
    }
  } else {
    /* NREAD_PREPEND */
    if (new_it->it_flags & ITEM_CHUNKED) {
      if (_store_item_copy_chunks(new_it, add_it, add_it->nbytes - 2) == -1 ||
	  _store_item_copy_chunks(new_it, old_it, old_it->nbytes) == -1) {
	return -1;
      }
    } else {
      memcpy(ITEM_data(new_it), ITEM_data(add_it), add_it->nbytes);
      memcpy(ITEM_data(new_it) + add_it->nbytes - 2 /* CRLF */, ITEM_data(old_it), old_it->nbytes);
    }
  }
  return 0;
}

/*
 * Stores an item in the cache according to the semantics of one of the set
 * commands. In threaded mode, this is protected by the cache lock.
 *
 * Returns the state of storage.
 */

std::pair<store_item_type, size_t> do_store_item(item *it, int comm, const uint32_t hv) {
  char *key = ITEM_key(it);
  item *old_it = do_item_get(key, it->nkey, hv, DONT_UPDATE);
  enum store_item_type stored = NOT_STORED;

  size_t cas = 0;
  item *new_it = NULL;
  uint32_t flags;
  if (old_it != NULL && comm == NREAD_ADD) {
    /* add only adds a nonexistent item, but promote to head of LRU */
    do_item_update(old_it);
  } else if (!old_it && (comm == NREAD_REPLACE
	|| comm == NREAD_APPEND || comm == NREAD_PREPEND))
  {
    /* replace only replaces an existing value; don't store */
  } else if (comm == NREAD_CAS) {
    /* validate cas operation */
    if(old_it == NULL) {
      // LRU expired
      stored = NOT_FOUND;
    }
    else if (ITEM_get_cas(it) == ITEM_get_cas(old_it)) {
      // cas validates
      // it and old_it may belong to different classes.
      // I'm updating the stats for the one that's getting pushed out
      item_replace(old_it, it, hv);
      stored = STORED;
    } else {
      stored = EXISTS;
    }
  } else {
    int failed_alloc = 0;
    /*
     * Append - combine new and old record into single one. Here it's
     * atomic and thread-safe.
     */
    if (comm == NREAD_APPEND || comm == NREAD_PREPEND) {
      /*
       * Validate CAS
       */
      if (stored == NOT_STORED) {
	/* we have it and old_it here - alloc memory to hold both */
	/* flags was already lost - so recover them from ITEM_suffix(it) */
	FLAGS_CONV(false, old_it, flags);
	new_it = do_item_alloc(key, it->nkey, flags, old_it->exptime, it->nbytes + old_it->nbytes - 2 /* CRLF */);

	/* copy data from it and old_it to new_it */
	if (new_it == NULL || _store_item_copy_data(comm, old_it, new_it, it) == -1) {
	  failed_alloc = 1;
	  stored = NOT_STORED;
	  // failed data copy, free up.
	  if (new_it != NULL)
	    item_remove(new_it);
	} else {
          // README - in the case of append or prepend, the actual item
          // never becomes visible upwards in our stack. For the purposes
          // of persistence, commit the memory here (it's pretty much
          // in the structure anyway)
	  it = new_it;
          unsigned e = RERO::begin_tx();
          it->epoch = e;
          RERO::add_to_persist(it);
          RERO::end_tx(e);
	}
      }
    }

    if (stored == NOT_STORED && failed_alloc == 0) {
      if (old_it != NULL) {
	item_replace(old_it, it, hv);
      } else {
	do_item_link(it, hv);
      }

      cas = ITEM_get_cas(it);

      stored = STORED;
    }
  }

  if (old_it != NULL)
    do_item_remove(old_it);         /* release our reference */
  if (new_it != NULL)
    do_item_remove(new_it);

  if (stored == STORED) {
    cas = ITEM_get_cas(it);
  }
  return std::pair<store_item_type, size_t>(stored, cas);
}

struct token_t {
  char *value;
  size_t length;
};

#define COMMAND_TOKEN 0
#define SUBCOMMAND_TOKEN 1
#define KEY_TOKEN 1

#define MAX_TOKENS 8

/*
 * adds a delta value to a numeric item.
 *
 * c     connection requesting the operation
 * it    item to adjust
 * incr  true to increment value, false to decrement
 * delta amount to adjust value by
 * buf   buffer for response string
 *
 * returns a response string to send back to the client.
 */

/*
 * We keep the current time of day in a global variable that's updated by a
 * timer event. This saves us a bunch of time() system calls (we really only
 * need to get the time once a second, whereas there can be tens of thousands
 * of requests a second) and allows us to use server-start-relative timestamps
 * rather than absolute UNIX timestamps, a space savings on systems where
 * sizeof(time_t) > sizeof(unsigned int).
 */
volatile rel_time_t *current_time;
static struct event clockevent;

/* libevent uses a monotonic clock when available for event scheduling. Aside
 * from jitter, simply ticking our internal timer here is accurate enough.
 * Note that users who are setting explicit dates for expiration times *must*
 * ensure their clocks are correct before starting memcached. */
static void clock_handler(const int fd, const short which, void *arg) {
  struct timeval t = {.tv_sec = 1, .tv_usec = 0};
  static bool initialized = false;
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
  static bool monotonic = false;
  static time_t monotonic_start;
#endif

  if (initialized) {
    /* only delete the event if it's actually there. */
    evtimer_del(&clockevent);
  } else {
    initialized = true;
    /* process_started is initialized to time() - 2. We initialize to 1 so
     * flush_all won't underflow during tests. */
#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
      monotonic = true;
      monotonic_start = ts.tv_sec - ITEM_UPDATE_INTERVAL - 2;
    }
#endif
  }

  // While we're here, check for hash table expansion.
  // This function should be quick to avoid delaying the timer.
  assoc_start_expand(stats_state.curr_items);
    
  evtimer_set(&clockevent, clock_handler, 0);
  event_base_set(main_base, &clockevent);
  evtimer_add(&clockevent, &t);

#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
  if (monotonic) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
      return;
    *current_time = (rel_time_t) (ts.tv_sec - monotonic_start);
    return;
  }
#endif
  {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *current_time = (rel_time_t) (tv.tv_sec - process_started);
  }
}

static void sig_handler(const int sig) {
  printf("Signal handled: %s.\n", strsignal(sig));
  exit(EXIT_SUCCESS);
}

#ifndef HAVE_SIGIGNORE
static int sigignore(int sig) {
  struct sigaction sa = { .sa_handler = SIG_IGN, .sa_flags = 0 };

  if (sigemptyset(&sa.sa_mask) == -1 || sigaction(sig, &sa, 0) == -1) {
    return -1;
  }
  return 0;
}
#endif

void server_start() {
}

// run this regardless of whether you're a server or a client
void agnostic_init(){
  if (!is_restart){
    end_signal = (std::atomic<int>*)RP_malloc(sizeof(std::atomic<int>));
    current_time = (rel_time_t*)RP_malloc(sizeof(rel_time_t));
    assert(end_signal != nullptr);
    assert(current_time != nullptr);
    RP_set_root(end_signal, RPMRoot::EndSignal);
    RP_set_root((void*)current_time, RPMRoot::CTime);
  } else {
    end_signal = RP_get_root<std::atomic<int> >(RPMRoot::EndSignal);
    current_time = RP_get_root<rel_time_t>(RPMRoot::CTime);
  }
  enum {
    MAXCONNS_FAST = 0,
    HASHPOWER_INIT,
    NO_HASHEXPAND,
    TAIL_REPAIR_TIME,
    HASH_ALGORITHM,
    LRU_CRAWLER,
    LRU_CRAWLER_SLEEP,
    LRU_CRAWLER_TOCRAWL,
    LRU_MAINTAINER,
    HOT_LRU_PCT,
    WARM_LRU_PCT,
    HOT_MAX_FACTOR,
    WARM_MAX_FACTOR,
    TEMPORARY_TTL,
    IDLE_TIMEOUT,
    WATCHER_LOGBUF_SIZE,
    WORKER_LOGBUF_SIZE,
    TRACK_SIZES,
    NO_INLINE_ASCII_RESP,
    MODERN,
    NO_MODERN,
    NO_CHUNKED_ITEMS,
    NO_MAXCONNS_FAST,
    INLINE_ASCII_RESP,
    NO_LRU_CRAWLER,
    NO_LRU_MAINTAINER
  };
  /* init settings */
  settings_init();
  memcached_thread_init();
  items_init();

  /* Run regardless of initializing it later */
  init_lru_maintainer();

  /* set stderr non-buffering (for running under, say, daemontools) */
  setbuf(stderr, NULL);

  /* initialize other stuff */
  assoc_init(settings.hashpower_init);
  slabs_init(settings.factor);
}

void* server_thread (void *pargs) {
  
  *end_signal = 0;
  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  main_base = event_base_new_with_config(ev_config);
  event_config_free(ev_config);

  if (is_restart) {
    RP_recover();
  }
  /* handle SIGINT and SIGTERM */
  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  if (sigignore(SIGPIPE) == -1) {
    perror("failed to ignore SIGPIPE; sigaction");
    exit(EX_OSERR);
  }
  /* start up worker threads if MT mode */
  // num_threads
  init_lru_crawler(NULL);

  assert(start_assoc_maintenance_thread() != -1);
  assert(start_item_crawler_thread() == 0);
  assert(start_lru_maintainer_thread(NULL) == 0);
  /* initialise clock event */
  clock_handler(0, 0, 0);

  /* enter the event loop */
  // TODO chnage to wait on maintenance threads
  pthread_mutex_unlock(&begin_ops_mutex);
  while(end_signal->load() == 0){
    sleep(1);
    event_base_loop(main_base, EVLOOP_ONCE);

  }

  stop_assoc_maintenance_thread();
  return NULL;
}

static int pause_sig = 0;
static int num_lookers = 0;
static pthread_mutex_t counting_lock = PTHREAD_MUTEX_INITIALIZER;

static void inc_lookers (void){
  pthread_mutex_lock(&counting_lock);
  while(pause_sig); //spin until we can move forward
  ++num_lookers;
  pthread_mutex_unlock(&counting_lock);
}

static void dec_lookers (void){
  pthread_mutex_lock(&counting_lock);
  --num_lookers;
  pthread_mutex_unlock(&counting_lock);
}

void pause_accesses(void){
  pause_sig = 1;
  while(num_lookers);
}

void unpause_accesses(void){
  pause_sig = 0;
}

char buf[32];

enum delta_result_type do_add_delta(const char *key,
    const size_t nkey, const bool incr,
    const uint64_t delta, uint64_t *value_ptr, const uint32_t hv) {
    char *ptr;
    uint64_t value;
    int res;
    item *it;

    it = do_item_get(key, nkey, hv, DONT_UPDATE);
    if (!it) {
        return DELTA_ITEM_NOT_FOUND;
    }

    if (it->nbytes <= 2 || (it->it_flags & (ITEM_CHUNKED)) != 0) {
        do_item_remove(it);
        return NON_NUMERIC;
    }

    ptr = ITEM_data(it);

    if (!safe_strtoull(ptr, &value)) {
        do_item_remove(it);
        return NON_NUMERIC;
    }

    if (incr) {
        value += delta;
    } else {
        if(delta > value) {
            value = 0;
        } else {
            value -= delta;
        }
    }
    *value_ptr = value;

    itoa_u64(value, buf);
    res = strlen(buf);
    /* refcount == 2 means we are the only ones holding the item, and it is
     * linked. We hold the item's lock in this function, so refcount cannot
     * increase. */
    if (res + 2 <= it->nbytes && it->refcount == 2) { 
      /* replace in-place */
      item_stats_sizes_remove(it);
      item_stats_sizes_add(it);
      memcpy(ITEM_data(it), buf, res);
      memset(ITEM_data(it) + res, ' ', it->nbytes - res - 2);
      do_item_update(it);
    } else if (it->refcount > 1) {
      item *new_it;
      uint32_t flags;
      FLAGS_CONV(nullptr, it, flags);
      new_it = do_item_alloc(ITEM_key(it), it->nkey, flags, it->exptime, res + 2);
      if (new_it == 0) {
        do_item_remove(it);
        return EOM;
      }
      memcpy(ITEM_data(new_it), buf, res);
      memcpy(ITEM_data(new_it) + res, "\r\n", 2);
      item_replace(it, new_it, hv);
      do_item_remove(new_it);       /* release our reference */
    } else {
      /* Should never get here. This means we somehow fetched an unlinked
       * item. TODO: Add a counter? */
      if (it->refcount == 1)
        do_item_remove(it);
      return DELTA_ITEM_NOT_FOUND;
    }
    do_item_remove(it);         /* release our reference */
    return OK;
}

// Race conditions don't happen in get operations. We never do in place writes,
// only full replacements. Whenever you do a item_get operation, it increments
// the refcounts and prevents it from being freed. NOTE - YOU MUST refcount_decr
// after you read the item so that it may be freed if necessary. 
memcached_return_t
pku_memcached_get(const char* key, size_t nkey, char* &buffer, size_t* buffLen,
    uint32_t *flags){
  *flags = 0; // make sure these don't segfault when it really matters
  *buffLen = 0;
  inc_lookers();
  item* it = item_get(key, nkey, 1);
  if (it == NULL)
    return MEMCACHED_NOTFOUND;
  buffer = (char*)malloc(it->nbytes);
  memcpy(buffer, ITEM_data(it), it->nbytes);
  *flags = it->it_flags;
  *buffLen = it->nbytes;
  item_remove(it); /* release our reference */
  dec_lookers();
  return MEMCACHED_SUCCESS;
}

// Is this good? If the user doesn't fetch all the entries, some entries might
// be unfreeable. TODO - change this
memcached_return_t
pku_memcached_mget(const char * const *keys, const size_t *key_length,
   size_t number_of_keys, item **list){
  for(unsigned i = 0; i < number_of_keys; ++i)
    list[i] = item_get(keys[i], key_length[i], 1);
  return MEMCACHED_SUCCESS;
} 

memcached_return_t
pku_memcached_insert(const char* key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime){
  // do what we're here for
  inc_lookers();
  item *it = item_alloc(key, nkey, 0, realtime(exptime), datan + 2);

  memcached_return_t t;
  if (it != NULL) {
    memcpy(ITEM_data(it), data, datan);
    memcpy(ITEM_data(it) + datan, "\r\n", 2);
    uint32_t hv = tcd_hash(key, nkey);
    unsigned e = RERO::begin_tx();
    auto pr = do_store_item(it, NREAD_ADD, hv);
    switch(pr.first){
      case NOT_FOUND:
        t = MEMCACHED_NOTFOUND;
        break;
      case TOO_LARGE: 
        t = MEMCACHED_E2BIG;
        break;
      case NO_MEMORY:
        t = MEMCACHED_MEMORY_ALLOCATION_FAILURE;
        break;
      case NOT_STORED:
        t = MEMCACHED_NOTSTORED;
        break;
      default:
        t = MEMCACHED_SUCCESS;
        break;
    }
    item_remove(it);         /* release our reference */
    if (t == MEMCACHED_SUCCESS){
      it->epoch = e;
      RERO::add_to_persist(it);
    }
    RERO::end_tx(e);
  } else {
    perror("SERVER_ERROR Out of memory allocating new item");
    return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
  }
  dec_lookers();
  return t;
}

memcached_return_t
pku_memcached_set(const char * key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime){
  inc_lookers();
  item *it = item_alloc(key, nkey, 0, realtime(exptime), datan + 2);

  if (it == 0) {
    if (! item_size_ok(nkey, 0, datan)) {
      fprintf(stderr, "SERVER_ERROR too big for the cache\n");
      return MEMCACHED_KEY_TOO_BIG; // Maybe make this more informative
    } else {
      fprintf(stderr, "SERVER_ERROR out of memory storing object");
      return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
    }
    it = item_get(key, nkey, DONT_UPDATE);
    if (it) {
      item_unlink(it); // remove item from table
      item_remove(it); // lazily delete it
    }
    // If we're storing, don't allow data to overwrite to persist in cache,
    // we prefer old data to not exist than pollute the cache
    return MEMCACHED_NOTSTORED;
  }
  if (it != NULL) {
    unsigned e = RERO::begin_tx();
    memcpy(ITEM_data(it), data, datan);
    memcpy(ITEM_data(it) + datan, "\r\n", 2);
    auto res = store_item(it, NREAD_SET);
    item_remove(it);         /* release our reference */
    memcached_return_t t;
    dec_lookers();
    switch(res) {
      case EXISTS:
      case STORED:
        t = MEMCACHED_SUCCESS;
        break;
      case NOT_FOUND:
        t = MEMCACHED_NOTFOUND;
        break;
      case TOO_LARGE: 
        t = MEMCACHED_E2BIG;
        break;
      case NO_MEMORY:
        t = MEMCACHED_MEMORY_ALLOCATION_FAILURE;
        break;
      case NOT_STORED:
        t = MEMCACHED_NOTSTORED;
        break;
      default:
        t = MEMCACHED_FAILURE;
    }
    if (t == MEMCACHED_SUCCESS) {
      it->epoch = e;
      RERO::add_to_persist(it);
    }
    RERO::end_tx(e);
    return t;
  } else {
    perror("SERVER_ERROR Out of memory allocating new item");
    return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
  }
  return MEMCACHED_STORED;
}

memcached_return_t
pku_memcached_flush(uint32_t exptime){
  pause_accesses();
  rel_time_t new_oldest = 0;
  if (exptime > 0) {
    new_oldest = realtime(exptime);
  } else {
    new_oldest = *current_time;
  }
  settings.oldest_live = new_oldest;
  unpause_accesses();
  return MEMCACHED_SUCCESS; 
}

memcached_return_t
pku_memcached_delete(const char * key, size_t nkey, uint32_t exptime){
  // how to free an item?
  // For now we think these are very infrequent...
  item *it;
  memcached_return_t ret;
  uint32_t hv;

  it = item_get_locked(key, nkey, DONT_UPDATE, &hv);
  if (it) {
    do_item_unlink(it, hv);
    it->epoch = 0;
    do_item_remove(it);      /* release our reference */
    FLUSH(it);
    FLUSHFENCE;
    ret = MEMCACHED_SUCCESS;
  } else ret = MEMCACHED_FAILURE;
  item_unlock(hv);
  return ret;
}

// Persistence handled in do_store_item
memcached_return_t
pku_memcached_append(const char * key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags) {
  inc_lookers();
  item *it = item_alloc(key, nkey, 0, 0, datan+2);

  if (it == 0) {
    if (! item_size_ok(nkey, 0, datan + 2)) {
      dec_lookers();
      return MEMCACHED_E2BIG;
    } else {
      dec_lookers();
      return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
    }
  }
  memcpy(ITEM_data(it), data, datan);
  memcpy(ITEM_data(it) + datan, "\r\n", 2);
  if (store_item(it, NREAD_APPEND) != STORED){
    dec_lookers();
    return MEMCACHED_NOTSTORED;
  }
  // TODO add to list of some kind
  item_remove(it);
  dec_lookers();
  return MEMCACHED_STORED;  
}

// Persistence handled in do_store_item
memcached_return_t
pku_memcached_prepend(const char * key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags) {
  inc_lookers();
  item *it = item_alloc(key, nkey, 0, 0, datan+2);

  if (it == 0) {
    if (! item_size_ok(nkey, 0, datan + 2)) {
      dec_lookers();
      return MEMCACHED_E2BIG;
    } else {
      dec_lookers();
      return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
    }
  }
  memcpy(ITEM_data(it), data, datan);
  memcpy(ITEM_data(it) + datan, "\r\n", 2);
  if (store_item(it, NREAD_PREPEND) != STORED){
    dec_lookers();
    return MEMCACHED_NOTSTORED;
  }
  item_remove(it);
  dec_lookers();
  return MEMCACHED_STORED;
}

memcached_return_t
pku_memcached_replace(const char * key, size_t nkey, const char * data, size_t datan,
    uint32_t exptime, uint32_t flags){
  inc_lookers();
  item *it = item_alloc(key, nkey, 0, 0, datan+2);

  if (it == 0) {
    if (! item_size_ok(nkey, 0, datan + 2)) {
      dec_lookers();
      return MEMCACHED_E2BIG;
    } else {
      dec_lookers();
      return MEMCACHED_MEMORY_ALLOCATION_FAILURE;
    }
  }
  memcpy(ITEM_data(it), data, datan);
  memcpy(ITEM_data(it) + datan, "\r\n", 2);
  uint32_t hv = tcd_hash(key, nkey);
  unsigned e = RERO::begin_tx();
  auto pr = do_store_item(it, NREAD_REPLACE, hv);
  memcached_return_t t;
  switch(pr.first) {
    case EXISTS:
    case STORED:
      t = MEMCACHED_SUCCESS;
      break;
    case NOT_FOUND:
      t = MEMCACHED_NOTFOUND;
      break;
    case TOO_LARGE: 
      t = MEMCACHED_E2BIG;
      break;
    case NO_MEMORY:
      t = MEMCACHED_MEMORY_ALLOCATION_FAILURE;
      break;
    case NOT_STORED:
      t = MEMCACHED_NOTSTORED;
      break;
    default:
      t = MEMCACHED_FAILURE;
      break;
  }
  if (t == MEMCACHED_SUCCESS){
    it->epoch = e;
    RERO::add_to_persist(it);
  }
  RERO::end_tx(e);
  item_remove(it);
  dec_lookers();
  return t;

}

