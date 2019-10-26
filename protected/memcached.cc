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
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <ctype.h>
#include <stdarg.h>

/* some POSIX systems need the following definition
 * to get mlockall flags out of sys/mman.h.  */
#ifndef _P1003_1B_VISIBLE
#define _P1003_1B_VISIBLE
#endif
/* need this to get IOV_MAX on some platforms. */
#ifndef __need_IOV_MAX
#define __need_IOV_MAX
#endif
#include <pwd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <limits.h>
#include <sysexits.h>
#include <stddef.h>

#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#endif

/* FreeBSD 4.x doesn't have IOV_MAX exposed. */
#ifndef IOV_MAX
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__GNU__)
# define IOV_MAX 1024
/* GNU/Hurd don't set MAXPATHLEN
 * http://www.gnu.org/software/hurd/hurd/porting/guidelines.html#PATH_MAX_tt_MAX_PATH_tt_MAXPATHL */
#ifndef MAXPATHLEN
#define MAXPATHLEN 4096
#endif
#endif
#endif

/*
 * forward declarations
 */

/* defaults */
static void settings_init(void);

pthread_mutex_t end_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t begin_ops_mutex = PTHREAD_MUTEX_INITIALIZER;
/** exported globals **/
struct stats stats;
struct stats_state stats_state;
struct settings settings;
time_t process_started;     /* when the process was started */
struct slab_rebalance slab_rebal;
volatile int slab_rebalance_signal;


/* We have to know if we're a server or a client so we can do proper init
 * routines.
 */
int am_server = 0;

struct st_st *mk_st (enum store_item_type my_sit, size_t my_cas){
  struct st_st *temp = (struct st_st*)malloc(sizeof(struct st_st));
  temp->sit = my_sit;
  temp->cas = my_cas;
  return temp;
}

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
    return (rel_time_t)(exptime + current_time);
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
  settings.lru_crawler = false;
  settings.lru_crawler_sleep = 100;
  settings.lru_crawler_tocrawl = 0;
  settings.hot_max_factor = 0.2;
  settings.warm_max_factor = 2.0;
  settings.hashpower_init = 0;
  settings.tail_repair_time = TAIL_REPAIR_TIME_DEFAULT;
  settings.crawls_persleep = 1000;
  settings.logger_watcher_buf_size = LOGGER_WATCHER_BUF_SIZE;
  settings.logger_buf_size = LOGGER_BUF_SIZE;
}

/* Destination must always be chunked */
/* This should be part of item.c */
static int _store_item_copy_chunks(item *d_it, item *s_it, const int len) {
  item_chunk *dch = (item_chunk *) ITEM_schunk(d_it);
  /* Advance dch until we find free space */
  while (dch->size == dch->used) {
    if (dch->next) {
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

struct st_st *do_store_item(item *it, int comm, const uint32_t hv) {
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
      if (ITEM_get_cas(it) != 0) {
	// CAS much be equal
	if (ITEM_get_cas(it) != ITEM_get_cas(old_it)) {
	  stored = EXISTS;
	}
      }
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
	  it = new_it;
	}
      }
    }

    if (stored == NOT_STORED && failed_alloc == 0) {
      if (old_it != NULL) {
	STORAGE_delete(c->thread->storage, old_it);
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
  return mk_st(stored, cas);
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
volatile rel_time_t current_time;
static struct event clockevent;

/* libevent uses a monotonic clock when available for event scheduling. Aside
 * from jitter, simply ticking our internal timer here is accurate enough.
 * Note that users who are setting explicit dates for expiration times *must*
 * ensure their clocks are correct before starting memcached. */
static void clock_handler(const int fd, const short which, void *arg) {
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

#if defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
  if (monotonic) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
      return;
    current_time = (rel_time_t) (ts.tv_sec - monotonic_start);
    return;
  }
#endif
  {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    current_time = (rel_time_t) (tv.tv_sec - process_started);
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

/**
 * Do basic sanity check of the runtime environment
 * @return true if no errors found, false if we can't use this env
 */
static bool sanitycheck(void) {
    /* One of our biggest problems is old and bogus libevents */
    const char *ever = event_get_version();
    if (ever != NULL) {
        if (strncmp(ever, "1.", 2) == 0) {
            /* Require at least 1.3 (that's still a couple of years old) */
            if (('0' <= ever[2] && ever[2] < '3') && !isdigit(ever[3])) {
                fprintf(stderr, "You are using libevent %s.\nPlease upgrade to"
                        " a more recent version (1.3 or newer)\n",
                        event_get_version());
                return false;
            }
        }
    }

    return true;
}

void* server_thread (void *pargs) {
  am_server = 1;
  printf("server started\n");
  fflush(stdout);
  bool lock_memory = false;
  char *username = NULL;
  struct passwd *pw;
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

  if (!sanitycheck()) {
    return (void*)0xDEAFBEEF;
  }

  /* handle SIGINT and SIGTERM */
  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  /* init settings */
  settings_init();

  /* Run regardless of initializing it later */
  init_lru_maintainer();

  /* set stderr non-buffering (for running under, say, daemontools) */
  setbuf(stderr, NULL);

  /* lose root privileges if we have them */
  if (getuid() == 0 || geteuid() == 0) {
    if (username == 0 || *username == '\0') {
      fprintf(stderr, "can't run as root without the -u switch\n");
      exit(EX_USAGE);
    }
    if ((pw = getpwnam(username)) == 0) {
      fprintf(stderr, "can't find the user %s to switch to\n", username);
      exit(EX_NOUSER);
    }
    if (setgroups(0, NULL) < 0) {
      fprintf(stderr, "failed to drop supplementary groups\n");
      exit(EX_OSERR);
    }
    if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
      fprintf(stderr, "failed to assume identity of user %s\n", username);
      exit(EX_OSERR);
    }
  }

  /* lock paged memory if needed */
  if (lock_memory) {
#ifdef HAVE_MLOCKALL
    int res = mlockall(MCL_CURRENT | MCL_FUTURE);
    if (res != 0) {
      fprintf(stderr, "warning: -k invalid, mlockall() failed: %s\n",
	  strerror(errno));
    }
#else
    fprintf(stderr, "warning: -k invalid, mlockall() not supported on this platform.  proceeding without.\n");
#endif
  }

  /* initialize other stuff */
  assoc_init(settings.hashpower_init);
  if (sigignore(SIGPIPE) == -1) {
    perror("failed to ignore SIGPIPE; sigaction");
    exit(EX_OSERR);
  }
  /* start up worker threads if MT mode */
  // num_threads
  memcached_thread_init();
  init_lru_crawler(NULL);

  assert(start_assoc_maintenance_thread() != -1);
  assert(start_item_crawler_thread() == 0);
  assert(start_lru_maintainer_thread(NULL) == 0);
  /* initialise clock event */
  clock_handler(0, 0, 0);

  /* enter the event loop */
  // TODO chnage to wait on maintenance threads
  pthread_mutex_unlock(&begin_ops_mutex);
  pthread_mutex_init(&end_mutex, nullptr);
  pthread_mutex_lock(&end_mutex);
  pthread_mutex_lock(&end_mutex);

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


int pku_memcached_get(char* key, size_t nkey, uint32_t exptime, char* buffer,
    size_t buffLen){
  inc_lookers();
  item* it = item_get(key, nkey, exptime, 1);
  if (it == NULL)
    return 2;
  if (buffLen < (size_t)it->nbytes)
    return 1;
  memcpy(buffer, ITEM_data(it), it->nbytes);
  dec_lookers();
  return 0;
}

void pku_memcached_touch(char* key, size_t nkey, uint32_t exptime){
  inc_lookers();
  item_get(key, nkey, exptime, 0);
  dec_lookers();
}

void pku_memcached_insert(char* key, size_t nkey, char* data, size_t datan,
    uint32_t exptime){
  // do what we're here for
  struct st_st *tup;
  inc_lookers();
  item *it = item_alloc(key, nkey, 0, realtime(exptime), datan + 2);

  if (it != NULL) {
    memcpy(ITEM_data(it), data, datan);
    memcpy(ITEM_data(it) + datan, "\r\n", 2);
    uint32_t hv = tcd_hash(key, nkey);
    if (!(tup =do_store_item(it, NREAD_ADD, hv))->sit) {
      perror("Couldn't store item!!!\n");
    }
    item_remove(it);         /* release our reference */
  } else {
    perror("SERVER_ERROR Out of memory allocating new item");
    exit(1);
  }
  dec_lookers();
}


