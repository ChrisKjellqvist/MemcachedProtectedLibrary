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
/** file scope variables **/
static struct event_base *main_base;

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
  settings.use_cas = true;
  settings.access = 0700;
  settings.port = 11211;
  settings.udpport = 0;
  /* By default this string should be NULL for getaddrinfo() */
  settings.inter = NULL;
  settings.maxbytes = 64 * 1024 * 1024; /* default is 64MB */
  settings.maxconns = 1024;         /* to limit connections-related memory to about 5MB */
  settings.verbose = 0;
  settings.oldest_live = 0;
  settings.oldest_cas = 0;          /* supplements accuracy of oldest_live */
  settings.evict_to_free = 1;       /* push old items out of cache when memory runs out */
  settings.socketpath = NULL;       /* by default, not using a unix socket */
  settings.factor = 1.25;
  settings.chunk_size = 48;         /* space for a modest key and value */
  settings.num_threads = 4;         /* N workers */
  settings.num_threads_per_udp = 0;
  settings.prefix_delimiter = ':';
  settings.detail_enabled = 0;
  settings.reqs_per_event = 20;
  settings.backlog = 1024;
  settings.item_size_max = 1024 * 1024; /* The famous 1MB upper limit. */
  settings.slab_page_size = 1024 * 1024; /* chunks are split from 1MB pages. */
  settings.slab_chunk_size_max = settings.slab_page_size / 2;
  settings.maxconns_fast = true;
  settings.lru_crawler = false;
  settings.lru_crawler_sleep = 100;
  settings.lru_crawler_tocrawl = 0;
  settings.lru_maintainer_thread = false;
  settings.lru_segmented = true;
  settings.hot_lru_pct = 20;
  settings.warm_lru_pct = 40;
  settings.hot_max_factor = 0.2;
  settings.warm_max_factor = 2.0;
  settings.inline_ascii_response = false;
  settings.temp_lru = false;
  settings.temporary_ttl = 61;
  settings.idle_timeout = 0; /* disabled */
  settings.hashpower_init = 0;
  settings.slab_reassign = true;
  settings.slab_automove = 1;
  settings.slab_automove_ratio = 0.8;
  settings.slab_automove_window = 30;
  settings.shutdown_command = false;
  settings.tail_repair_time = TAIL_REPAIR_TIME_DEFAULT;
  settings.flush_enabled = true;
  settings.dump_enabled = true;
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
      if(settings.verbose > 1) {
	fprintf(stderr, "CAS:  failure: expected %llu, got %llu\n",
	    (unsigned long long)ITEM_get_cas(old_it),
	    (unsigned long long)ITEM_get_cas(it));
      }
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
	FLAGS_CONV(settings.inline_ascii_response, old_it, flags);
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

typedef struct token_s {
  char *value;
  size_t length;
} token_t;

#define COMMAND_TOKEN 0
#define SUBCOMMAND_TOKEN 1
#define KEY_TOKEN 1

#define MAX_TOKENS 8

/* nsuffix == 0 means use no storage for client flags */
static inline int make_ascii_get_suffix(char *suffix, item *it, bool return_cas, int nbytes) {
  char *p = suffix;
  if (!settings.inline_ascii_response) {
    *p = ' ';
    p++;
    if (it->nsuffix == 0) {
      *p = '0';
      p++;
    } else {
      p = itoa_u32(*((uint32_t *) ITEM_suffix(it)), p);
    }
    *p = ' ';
    p = itoa_u32(nbytes-2, p+1);
  } else {
    p = suffix;
  }
  if (return_cas) {
    *p = ' ';
    p = itoa_u64(ITEM_get_cas(it), p+1);
  }
  *p = '\r';
  *(p+1) = '\n';
  *(p+2) = '\0';
  return (p - suffix) + 2;
}

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
enum delta_result_type do_add_delta(const char *key, const size_t nkey,
    const bool incr, const int64_t delta,
    char *buf, uint64_t *cas,
    const uint32_t hv) {
  char *ptr;
  uint64_t value;
  int res;
  item *it;

  it = do_item_get(key, nkey, hv, DONT_UPDATE);
  if (!it) {
    return DELTA_ITEM_NOT_FOUND;
  }

  /* Can't delta zero byte values. 2-byte are the "\r\n" */
  /* Also can't delta for chunked items. Too large to be a number */
  if (it->nbytes <= 2 || (it->it_flags & (ITEM_CHUNKED)) != 0) {
    do_item_remove(it);
    return NON_NUMERIC;
  }

  if (cas != NULL && *cas != 0 && ITEM_get_cas(it) != *cas) {
    do_item_remove(it);
    return DELTA_ITEM_CAS_MISMATCH;
  }

  ptr = ITEM_data(it);

  if (!safe_strtoull(ptr, &value)) {
    do_item_remove(it);
    return NON_NUMERIC;
  }

  if (incr) {
    value += delta;
    MEMCACHED_COMMAND_INCR(c->sfd, ITEM_key(it), it->nkey, value);
  } else {
    if(delta > value) {
      value = 0;
    } else {
      value -= delta;
    }
    MEMCACHED_COMMAND_DECR(c->sfd, ITEM_key(it), it->nkey, value);
  }

  snprintf(buf, INCR_MAX_STORAGE_LEN, "%llu", (unsigned long long)value);
  res = strlen(buf);
  /* refcount == 2 means we are the only ones holding the item, and it is
   * linked. We hold the item's lock in this function, so refcount cannot
   * increase. */
  if (res + 2 <= it->nbytes && it->refcount == 2) { /* replace in-place */
    /* When changing the value without replacing the item, we
       need to update the CAS on the existing item. */
    /* We also need to fiddle it in the sizes tracker in case the tracking
     * was enabled at runtime, since it relies on the CAS value to know
     * whether to remove an item or not. */
    item_stats_sizes_remove(it);
    ITEM_set_cas(it, (settings.use_cas) ? get_cas_id() : 0);
    item_stats_sizes_add(it);
    memcpy(ITEM_data(it), buf, res);
    memset(ITEM_data(it) + res, ' ', it->nbytes - res - 2);
    do_item_update(it);
  } else if (it->refcount > 1) {
    item *new_it;
    uint32_t flags;
    FLAGS_CONV(settings.inline_ascii_response, it, flags);
    new_it = do_item_alloc(ITEM_key(it), it->nkey, flags, it->exptime, res + 2);
    if (new_it == 0) {
      do_item_remove(it);
      return EOM;
    }
    memcpy(ITEM_data(new_it), buf, res);
    memcpy(ITEM_data(new_it) + res, "\r\n", 2);
    item_replace(it, new_it, hv);
    // Overwrite the older item's CAS with our new CAS since we're
    // returning the CAS of the old item below.
    ITEM_set_cas(it, (settings.use_cas) ? ITEM_get_cas(new_it) : 0);
    do_item_remove(new_it);       /* release our reference */
  } else {
    /* Should never get here. This means we somehow fetched an unlinked
     * item. TODO: Add a counter? */
    if (settings.verbose) {
      fprintf(stderr, "Tried to do incr/decr on invalid item\n");
    }
    if (it->refcount == 1)
      do_item_remove(it);
    return DELTA_ITEM_NOT_FOUND;
  }

  if (cas) {
    *cas = ITEM_get_cas(it);    /* swap the incoming CAS value */
  }
  do_item_remove(it);         /* release our reference */
  return OK;
}

/*
   static void process_command(conn *c, char *command) {

   token_t tokens[MAX_TOKENS];
   size_t ntokens;
   int comm;

   assert(c != NULL);

   MEMCACHED_PROCESS_COMMAND_START(c->sfd, c->rcurr, c->rbytes);

   if (settings.verbose > 1)
   fprintf(stderr, "<%d %s\n", c->sfd, command);


   c->msgcurr = 0;
   c->msgused = 0;
   c->iovused = 0;
   if (add_msghdr(c) != 0) {
   out_of_memory(c, "SERVER_ERROR out of memory preparing response");
   return;
   }

   ntokens = tokenize_command(command, tokens, MAX_TOKENS);
   if (ntokens >= 3 &&
   ((strcmp(tokens[COMMAND_TOKEN].value, "get") == 0) ||
   (strcmp(tokens[COMMAND_TOKEN].value, "bget") == 0))) {

   process_get_command(c, tokens, ntokens, false, false);

   } else if ((ntokens == 6 || ntokens == 7) &&
   ((strcmp(tokens[COMMAND_TOKEN].value, "add") == 0 && (comm = NREAD_ADD)) ||
   (strcmp(tokens[COMMAND_TOKEN].value, "set") == 0 && (comm = NREAD_SET)) ||
   (strcmp(tokens[COMMAND_TOKEN].value, "replace") == 0 && (comm = NREAD_REPLACE)) ||
   (strcmp(tokens[COMMAND_TOKEN].value, "prepend") == 0 && (comm = NREAD_PREPEND)) ||
   (strcmp(tokens[COMMAND_TOKEN].value, "append") == 0 && (comm = NREAD_APPEND)) )) {

   process_update_command(c, tokens, ntokens, comm, false);

   } else if ((ntokens == 7 || ntokens == 8) && (strcmp(tokens[COMMAND_TOKEN].value, "cas") == 0 && (comm = NREAD_CAS))) {

   process_update_command(c, tokens, ntokens, comm, true);

   } else if ((ntokens == 4 || ntokens == 5) && (strcmp(tokens[COMMAND_TOKEN].value, "incr") == 0)) {

   process_arithmetic_command(c, tokens, ntokens, 1);

   } else if (ntokens >= 3 && (strcmp(tokens[COMMAND_TOKEN].value, "gets") == 0)) {

   process_get_command(c, tokens, ntokens, true, false);

   } else if ((ntokens == 4 || ntokens == 5) && (strcmp(tokens[COMMAND_TOKEN].value, "decr") == 0)) {

   process_arithmetic_command(c, tokens, ntokens, 0);

   } else if (ntokens >= 3 && ntokens <= 5 && (strcmp(tokens[COMMAND_TOKEN].value, "delete") == 0)) {

   process_delete_command(c, tokens, ntokens);

   } else if ((ntokens == 4 || ntokens == 5) && (strcmp(tokens[COMMAND_TOKEN].value, "touch") == 0)) {

   process_touch_command(c, tokens, ntokens);

   } else if (ntokens >= 4 && (strcmp(tokens[COMMAND_TOKEN].value, "gat") == 0)) {

   process_get_command(c, tokens, ntokens, false, true);

   } else if (ntokens >= 4 && (strcmp(tokens[COMMAND_TOKEN].value, "gats") == 0)) {

   process_get_command(c, tokens, ntokens, true, true);

} else if (ntokens >= 2 && (strcmp(tokens[COMMAND_TOKEN].value, "stats") == 0)) {

  process_stat(c, tokens, ntokens);

} else if (ntokens >= 2 && ntokens <= 4 && (strcmp(tokens[COMMAND_TOKEN].value, "flush_all") == 0)) {
  time_t exptime = 0;
  rel_time_t new_oldest = 0;

  set_noreply_maybe(c, tokens, ntokens);

  pthread_mutex_lock(&c->thread->stats.mutex);
  c->thread->stats.flush_cmds++;
  pthread_mutex_unlock(&c->thread->stats.mutex);

  if (!settings.flush_enabled) {
    // flush_all is not allowed but we log it on stats
    out_string(c, "CLIENT_ERROR flush_all not allowed");
    return;
  }

  if (ntokens != (c->noreply ? 3 : 2)) {
    exptime = strtol(tokens[1].value, NULL, 10);
    if(errno == ERANGE) {
      out_string(c, "CLIENT_ERROR bad command line format");
      return;
    }
  }

  if (exptime > 0) {
    new_oldest = realtime(exptime);
  } else { 
    new_oldest = current_time;
  }

  if (settings.use_cas) {
    settings.oldest_live = new_oldest - 1;
    if (settings.oldest_live <= current_time)
      settings.oldest_cas = get_cas_id();
  } else {
    settings.oldest_live = new_oldest;
  }
  out_string(c, "OK");
  return;

} else if (ntokens == 2 && (strcmp(tokens[COMMAND_TOKEN].value, "version") == 0)) {

  out_string(c, "VERSION " VERSION);

} else if (ntokens == 2 && (strcmp(tokens[COMMAND_TOKEN].value, "quit") == 0)) {

  conn_set_state(c, conn_closing);

} else if (ntokens == 2 && (strcmp(tokens[COMMAND_TOKEN].value, "shutdown") == 0)) {

  if (settings.shutdown_command) {
    conn_set_state(c, conn_closing);
    raise(SIGINT);
  } else {
    out_string(c, "ERROR: shutdown not enabled");
  }

} else if (ntokens > 1 && strcmp(tokens[COMMAND_TOKEN].value, "slabs") == 0) {
  if (ntokens == 5 && strcmp(tokens[COMMAND_TOKEN + 1].value, "reassign") == 0) {
    int src, dst, rv;

    if (settings.slab_reassign == false) {
      out_string(c, "CLIENT_ERROR slab reassignment disabled");
      return;
    }

    src = strtol(tokens[2].value, NULL, 10);
    dst = strtol(tokens[3].value, NULL, 10);

    if (errno == ERANGE) {
      out_string(c, "CLIENT_ERROR bad command line format");
      return;
    }

    rv = slabs_reassign(src, dst);
    switch (rv) {
      case REASSIGN_OK:
	out_string(c, "OK");
	break;
      case REASSIGN_RUNNING:
	out_string(c, "BUSY currently processing reassign request");
	break;
      case REASSIGN_BADCLASS:
	out_string(c, "BADCLASS invalid src or dst class id");
	break;
      case REASSIGN_NOSPARE:
	out_string(c, "NOSPARE source class has no spare pages");
	break;
      case REASSIGN_SRC_DST_SAME:
	out_string(c, "SAME src and dst class are identical");
	break;
    }
    return;
  } else if (ntokens >= 4 &&
      (strcmp(tokens[COMMAND_TOKEN + 1].value, "automove") == 0)) {
    process_slabs_automove_command(c, tokens, ntokens);
  } else {
    out_string(c, "ERROR");
  }
} else if (ntokens > 1 && strcmp(tokens[COMMAND_TOKEN].value, "lru_crawler") == 0) {
  if (ntokens == 4 && strcmp(tokens[COMMAND_TOKEN + 1].value, "crawl") == 0) {
    int rv;
    if (settings.lru_crawler == false) {
      out_string(c, "CLIENT_ERROR lru crawler disabled");
      return;
    }

    rv = lru_crawler_crawl(tokens[2].value, CRAWLER_EXPIRED, NULL, 0,
	settings.lru_crawler_tocrawl);
    switch(rv) {
      case CRAWLER_OK:
	out_string(c, "OK");
	break;
      case CRAWLER_RUNNING:
	out_string(c, "BUSY currently processing crawler request");
	break;
      case CRAWLER_BADCLASS:
	out_string(c, "BADCLASS invalid class id");
	break;
      case CRAWLER_NOTSTARTED:
	out_string(c, "NOTSTARTED no items to crawl");
	break;
      case CRAWLER_ERROR:
	out_string(c, "ERROR an unknown error happened");
	break;
    }
    return;
  } else if (ntokens == 4 && strcmp(tokens[COMMAND_TOKEN + 1].value, "metadump") == 0) {
    if (settings.lru_crawler == false) {
      out_string(c, "CLIENT_ERROR lru crawler disabled");
      return;
    }
    if (!settings.dump_enabled) {
      out_string(c, "ERROR metadump not allowed");
      return;
    }

    int rv = lru_crawler_crawl(tokens[2].value, CRAWLER_METADUMP,
	c, c->sfd, LRU_CRAWLER_CAP_REMAINING);
    switch(rv) {
      case CRAWLER_OK:
	out_string(c, "OK");
	// TODO: Don't reuse conn_watch here.
	conn_set_state(c, conn_watch);
	event_del(&c->event);
	break;
      case CRAWLER_RUNNING:
	out_string(c, "BUSY currently processing crawler request");
	break;
      case CRAWLER_BADCLASS:
	out_string(c, "BADCLASS invalid class id");
	break;
      case CRAWLER_NOTSTARTED:
	out_string(c, "NOTSTARTED no items to crawl");
	break;
      case CRAWLER_ERROR:
	out_string(c, "ERROR an unknown error happened");
	break;
    }
    return;
  } else if (ntokens == 4 && strcmp(tokens[COMMAND_TOKEN + 1].value, "tocrawl") == 0) {
    uint32_t tocrawl;
    if (!safe_strtoul(tokens[2].value, &tocrawl)) {
      out_string(c, "CLIENT_ERROR bad command line format");
      return;
    }
    settings.lru_crawler_tocrawl = tocrawl;
    out_string(c, "OK");
    return;
  } else if (ntokens == 4 && strcmp(tokens[COMMAND_TOKEN + 1].value, "sleep") == 0) {
    uint32_t tosleep;
    if (!safe_strtoul(tokens[2].value, &tosleep)) {
      out_string(c, "CLIENT_ERROR bad command line format");
      return;
    }
    if (tosleep > 1000000) {
      out_string(c, "CLIENT_ERROR sleep must be one second or less");
      return;
    }
    settings.lru_crawler_sleep = tosleep;
    out_string(c, "OK");
    return;
  } else if (ntokens == 3) {
    if ((strcmp(tokens[COMMAND_TOKEN + 1].value, "enable") == 0)) {
      if (start_item_crawler_thread() == 0) {
	out_string(c, "OK");
      } else {
	out_string(c, "ERROR failed to start lru crawler thread");
      }
    } else if ((strcmp(tokens[COMMAND_TOKEN + 1].value, "disable") == 0)) {
      if (stop_item_crawler_thread() == 0) {
	out_string(c, "OK");
      } else {
	out_string(c, "ERROR failed to stop lru crawler thread");
      }
    } else {
      out_string(c, "ERROR");
    }
    return;
  } else {
    out_string(c, "ERROR");
  }
} else if (ntokens > 1 && strcmp(tokens[COMMAND_TOKEN].value, "watch") == 0) {
  process_watch_command(c, tokens, ntokens);
} else if ((ntokens == 3 || ntokens == 4) && (strcmp(tokens[COMMAND_TOKEN].value, "cache_memlimit") == 0)) {
  process_memlimit_command(c, tokens, ntokens);
} else if ((ntokens == 3 || ntokens == 4) && (strcmp(tokens[COMMAND_TOKEN].value, "verbosity") == 0)) {
  process_verbosity_command(c, tokens, ntokens);
} else if (ntokens >= 3 && strcmp(tokens[COMMAND_TOKEN].value, "lru") == 0) {
  process_lru_command(c, tokens, ntokens);
} else {
  if (ntokens >= 2 && strncmp(tokens[ntokens - 2].value, "HTTP/", 5) == 0) {
    conn_set_state(c, conn_closing);
  } else {
    out_string(c, "ERROR");
  }
}
return;
}
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

static void usage(void) {
  printf(PACKAGE " " VERSION "\n");
  printf("-p, --port=<num>          TCP port to listen on (default: 11211)\n"
      "-U, --udp-port=<num>      UDP port to listen on (default: 0, off)\n"
      "-s, --unix-socket=<file>  UNIX socket to listen on (disables network support)\n"
      "-A, --enable-shutdown     enable ascii \"shutdown\" command\n"
      "-a, --unix-mask=<mask>    access mask for UNIX socket, in octal (default: 0700)\n"
      "-l, --listen=<addr>       interface to listen on (default: INADDR_ANY)\n"
      "-d, --daemon              run as a daemon\n"
      "-r, --enable-coredumps    maximize core file limit\n"
      "-u, --user=<user>         assume identity of <username> (only when run as root)\n"
      "-m, --memory-limit=<num>  item memory in megabytes (default: 64 MB)\n"
      "-M, --disable-evictions   return error on memory exhausted instead of evicting\n"
      "-c, --conn-limit=<num>    max simultaneous connections (default: 1024)\n"
      "-k, --lock-memory         lock down all paged memory\n"
      "-v, --verbose             verbose (print errors/warnings while in event loop)\n"
      "-vv                       very verbose (also print client commands/responses)\n"
      "-vvv                      extremely verbose (internal state transitions)\n"
      "-h, --help                print this help and exit\n"
      "-i, --license             print memcached and libevent license\n"
      "-V, --version             print version and exit\n"
      "-P, --pidfile=<file>      save PID in <file>, only used with -d option\n"
      "-f, --slab-growth-factor=<num> chunk size growth factor (default: 1.25)\n"
      "-n, --slab-min-size=<bytes> min space used for key+value+flags (default: 48)\n");
  printf("-L, --enable-largepages  try to use large memory pages (if available)\n");
  printf("-D <char>     Use <char> as the delimiter between key prefixes and IDs.\n"
      "              This is used for per-prefix stats reporting. The default is\n"
      "              \":\" (colon). If this option is specified, stats collection\n"
      "              is turned on automatically; if not, then it may be turned on\n"
      "              by sending the \"stats detail on\" command to the server.\n");
  printf("-t, --threads=<num>       number of threads to use (default: 4)\n");
  printf("-R, --max-reqs-per-event  maximum number of requests per event, limits the\n"
      "                          requests processed per connection to prevent \n"
      "                          starvation (default: 20)\n");
  printf("-C, --disable-cas         disable use of CAS\n");
  printf("-b, --listen-backlog=<num> set the backlog queue limit (default: 1024)\n");
  printf("-B, --protocol=<name>     protocol - one of ascii, binary, or auto (default)\n");
  printf("-I, --max-item-size=<num> adjusts max item size\n"
      "                          (default: 1mb, min: 1k, max: 128m)\n");
  printf("-F, --disable-flush-all   disable flush_all command\n");
  printf("-X, --disable-dumping     disable stats cachedump and lru_crawler metadump\n");
  printf("-o, --extended            comma separated list of extended options\n"
      "                          most options have a 'no_' prefix to disable\n"
      "   - maxconns_fast:       immediately close new connections after limit\n"
      "   - hashpower:           an integer multiplier for how large the hash\n"
      "                          table should be. normally grows at runtime.\n"
      "                          set based on \"STAT hash_power_level\"\n"
      "   - tail_repair_time:    time in seconds for how long to wait before\n"
      "                          forcefully killing LRU tail item.\n"
      "                          disabled by default; very dangerous option.\n"
      "   - hash_algorithm:      the hash table algorithm\n"
      "                          default is murmur3 hash. options: jenkins, murmur3\n"
      "   - lru_crawler:         enable LRU Crawler background thread\n"
      "   - lru_crawler_sleep:   microseconds to sleep between items\n"
      "                          default is 100.\n"
      "   - lru_crawler_tocrawl: max items to crawl per slab per run\n"
      "                          default is 0 (unlimited)\n"
      "   - lru_maintainer:      enable new LRU system + background thread\n"
      "   - hot_lru_pct:         pct of slab memory to reserve for hot lru.\n"
      "                          (requires lru_maintainer)\n"
      "   - warm_lru_pct:        pct of slab memory to reserve for warm lru.\n"
      "                          (requires lru_maintainer)\n"
      "   - hot_max_factor:      items idle > cold lru age * drop from hot lru.\n"
      "   - warm_max_factor:     items idle > cold lru age * this drop from warm.\n"
      "   - temporary_ttl:       TTL's below get separate LRU, can't be evicted.\n"
      "                          (requires lru_maintainer)\n"
      "   - idle_timeout:        timeout for idle connections\n"
      "   - slab_chunk_max:      (EXPERIMENTAL) maximum slab size. use extreme care.\n"
      "   - watcher_logbuf_size: size in kilobytes of per-watcher write buffer.\n"
      "   - worker_logbuf_size:  size in kilobytes of per-worker-thread buffer\n"
      "                          read by background thread, then written to watchers.\n"
      "   - track_sizes:         enable dynamic reports for 'stats sizes' command.\n"
      "   - no_inline_ascii_resp: save up to 24 bytes per item.\n"
      "                           small perf hit in ASCII, no perf difference in\n"
      "                           binary protocol. speeds up all sets.\n"
      "   - no_hashexpand:       disables hash table expansion (dangerous)\n"
      "   - modern:              enables options which will be default in future.\n"
      "             currently: nothing\n"
      "   - no_modern:           uses defaults of previous major version (1.4.x)\n"
      );
  return;
}

static void usage_license(void) {
  printf(PACKAGE " " VERSION "\n\n");
  printf(
      "Copyright (c) 2003, Danga Interactive, Inc. <http://www.danga.com/>\n"
      "All rights reserved.\n"
      "\n"
      "Redistribution and use in source and binary forms, with or without\n"
      "modification, are permitted provided that the following conditions are\n"
      "met:\n"
      "\n"
      "    * Redistributions of source code must retain the above copyright\n"
      "notice, this list of conditions and the following disclaimer.\n"
      "\n"
      "    * Redistributions in binary form must reproduce the above\n"
      "copyright notice, this list of conditions and the following disclaimer\n"
      "in the documentation and/or other materials provided with the\n"
      "distribution.\n"
      "\n"
      "    * Neither the name of the Danga Interactive nor the names of its\n"
      "contributors may be used to endorse or promote products derived from\n"
      "this software without specific prior written permission.\n"
      "\n"
      "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS\n"
      "\"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT\n"
      "LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR\n"
      "A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT\n"
      "OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,\n"
      "SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT\n"
      "LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n"
      "DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n"
      "THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
      "(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n"
      "OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
      "\n"
      "\n"
      "This product includes software developed by Niels Provos.\n"
      "\n"
      "[ libevent ]\n"
      "\n"
      "Copyright 2000-2003 Niels Provos <provos@citi.umich.edu>\n"
      "All rights reserved.\n"
      "\n"
      "Redistribution and use in source and binary forms, with or without\n"
      "modification, are permitted provided that the following conditions\n"
      "are met:\n"
      "1. Redistributions of source code must retain the above copyright\n"
      "   notice, this list of conditions and the following disclaimer.\n"
      "2. Redistributions in binary form must reproduce the above copyright\n"
      "   notice, this list of conditions and the following disclaimer in the\n"
      "   documentation and/or other materials provided with the distribution.\n"
      "3. All advertising materials mentioning features or use of this software\n"
      "   must display the following acknowledgement:\n"
      "      This product includes software developed by Niels Provos.\n"
      "4. The name of the author may not be used to endorse or promote products\n"
      "   derived from this software without specific prior written permission.\n"
      "\n"
      "THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR\n"
      "IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES\n"
      "OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.\n"
      "IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,\n"
      "INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT\n"
      "NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,\n"
      "DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY\n"
      "THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
      "(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF\n"
      "THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
      );

  return;
}

static void save_pid(const char *pid_file) {
  FILE *fp;
  if (access(pid_file, F_OK) == 0) {
    if ((fp = fopen(pid_file, "r")) != NULL) {
      char buffer[1024];
      if (fgets(buffer, sizeof(buffer), fp) != NULL) {
	unsigned int pid;
	if (safe_strtoul(buffer, &pid) && kill((pid_t)pid, 0) == 0) {
	  fprintf(stderr, "WARNING: The pid file contained the following (running) pid: %u\n", pid);
	}
      }
      fclose(fp);
    }
  }

  /* Create the pid file first with a temporary name, then
   * atomically move the file to the real name to avoid a race with
   * another process opening the file to read the pid, but finding
   * it empty.
   */
  char tmp_pid_file[1024];
  snprintf(tmp_pid_file, sizeof(tmp_pid_file), "%s.tmp", pid_file);

  if ((fp = fopen(tmp_pid_file, "w")) == NULL) {
    vperror("Could not open the pid file %s for writing", tmp_pid_file);
    return;
  }

  fprintf(fp,"%ld\n", (long)getpid());
  if (fclose(fp) == -1) {
    vperror("Could not close the pid file %s", tmp_pid_file);
  }

  if (rename(tmp_pid_file, pid_file) != 0) {
    vperror("Could not rename the pid file from %s to %s",
	tmp_pid_file, pid_file);
  }
}

static void remove_pidfile(const char *pid_file) {
  if (pid_file == NULL)
    return;

  if (unlink(pid_file) != 0) {
    vperror("Could not remove the pid file %s", pid_file);
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


/*
 * On systems that supports multiple page sizes we may reduce the
 * number of TLB-misses by using the biggest available page size
 */
static int enable_large_pages(void) {
#if defined(HAVE_GETPAGESIZES) && defined(HAVE_MEMCNTL)
  int ret = -1;
  size_t sizes[32];
  int avail = getpagesizes(sizes, 32);
  if (avail != -1) {
    size_t max = sizes[0];
    struct memcntl_mha arg = {0};
    int ii;

    for (ii = 1; ii < avail; ++ii) {
      if (max < sizes[ii]) {
	max = sizes[ii];
      }
    }

    arg.mha_flags   = 0;
    arg.mha_pagesize = max;
    arg.mha_cmd = MHA_MAPSIZE_BSSBRK;

    if (memcntl(0, 0, MC_HAT_ADVISE, (caddr_t)&arg, 0, 0) == -1) {
      fprintf(stderr, "Failed to set large pages: %s\n",
	  strerror(errno));
      fprintf(stderr, "Will use default page size\n");
    } else {
      ret = 0;
    }
  } else {
    fprintf(stderr, "Failed to get supported pagesizes: %s\n",
	strerror(errno));
    fprintf(stderr, "Will use default page size\n");
  }

  return ret;
#elif defined(__linux__) && defined(MADV_HUGEPAGE)
  /* check if transparent hugepages is compiled into the kernel */
  struct stat st;
  int ret = stat("/sys/kernel/mm/transparent_hugepage/enabled", &st);
  if (ret || !(st.st_mode & S_IFREG)) {
    fprintf(stderr, "Transparent huge pages support not detected.\n");
    fprintf(stderr, "Will use default page size.\n");
    return -1;
  }
  return 0;
#else
  return -1;
#endif
}

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

static bool _parse_slab_sizes(char *s, uint32_t *slab_sizes) {
    char *b = NULL;
    uint32_t size = 0;
    int i = 0;
    uint32_t last_size = 0;

    if (strlen(s) < 1)
        return false;

    for (char *p = strtok_r(s, "-", &b);
         p != NULL;
         p = strtok_r(NULL, "-", &b)) {
        if (!safe_strtoul(p, &size) || size < settings.chunk_size
             || size > settings.slab_chunk_size_max) {
            fprintf(stderr, "slab size %u is out of valid range\n", size);
            return false;
        }
        if (last_size >= size) {
            fprintf(stderr, "slab size %u cannot be lower than or equal to a previous class size\n", size);
            return false;
        }
        if (size <= last_size + CHUNK_ALIGN_BYTES) {
            fprintf(stderr, "slab size %u must be at least %d bytes larger than previous class\n",
                    size, CHUNK_ALIGN_BYTES);
            return false;
        }
        slab_sizes[i++] = size;
        last_size = size;
        if (i >= MAX_NUMBER_OF_SLAB_CLASSES-1) {
            fprintf(stderr, "too many slab classes specified\n");
            return false;
        }
    }

    slab_sizes[i] = 0;
    return true;
}

void* server_thread (void *pargs) {
  printf("server started\n");
  fflush(stdout);
  struct pthread_args *start_args = (struct pthread_args*)pargs;
  int argc = start_args->argc;
  char **argv = start_args->argv;
  int c;
  bool lock_memory = false;
  bool do_daemonize = false;
  bool preallocate = false;
  int maxcore = 0;
  char *username = NULL;
  char *pid_file = NULL;
  struct passwd *pw;
  struct rlimit rlim;
  char *buf;
  char unit = '\0';
  int size_max = 0;
  bool start_lru_maintainer = true;
  bool start_lru_crawler = true;
  bool start_assoc_maint = true;
  enum hashfunc_type hash_type = MURMUR3_HASH;
  uint32_t tocrawl;
  uint32_t slab_sizes[MAX_NUMBER_OF_SLAB_CLASSES];
  bool use_slab_sizes = false;
  char *slab_sizes_unparsed = NULL;
  bool slab_chunk_size_changed = false;
  char *subopts, *subopts_orig;
  char *subopts_value;
  enum {
    MAXCONNS_FAST = 0,
    HASHPOWER_INIT,
    NO_HASHEXPAND,
    SLAB_REASSIGN,
    SLAB_AUTOMOVE,
    SLAB_AUTOMOVE_RATIO,
    SLAB_AUTOMOVE_WINDOW,
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
    SLAB_SIZES,
    SLAB_CHUNK_MAX,
    TRACK_SIZES,
    NO_INLINE_ASCII_RESP,
    MODERN,
    NO_MODERN,
    NO_CHUNKED_ITEMS,
    NO_SLAB_REASSIGN,
    NO_SLAB_AUTOMOVE,
    NO_MAXCONNS_FAST,
    INLINE_ASCII_RESP,
    NO_LRU_CRAWLER,
    NO_LRU_MAINTAINER
  };
  char *const subopts_tokens[] = {
    [MAXCONNS_FAST] = "maxconns_fast",
    [HASHPOWER_INIT] = "hashpower",
    [NO_HASHEXPAND] = "no_hashexpand",
    [SLAB_REASSIGN] = "slab_reassign",
    [SLAB_AUTOMOVE] = "slab_automove",
    [SLAB_AUTOMOVE_RATIO] = "slab_automove_ratio",
    [SLAB_AUTOMOVE_WINDOW] = "slab_automove_window",
    [TAIL_REPAIR_TIME] = "tail_repair_time",
    [HASH_ALGORITHM] = "hash_algorithm",
    [LRU_CRAWLER] = "lru_crawler",
    [LRU_CRAWLER_SLEEP] = "lru_crawler_sleep",
    [LRU_CRAWLER_TOCRAWL] = "lru_crawler_tocrawl",
    [LRU_MAINTAINER] = "lru_maintainer",
    [HOT_LRU_PCT] = "hot_lru_pct",
    [WARM_LRU_PCT] = "warm_lru_pct",
    [HOT_MAX_FACTOR] = "hot_max_factor",
    [WARM_MAX_FACTOR] = "warm_max_factor",
    [TEMPORARY_TTL] = "temporary_ttl",
    [IDLE_TIMEOUT] = "idle_timeout",
    [WATCHER_LOGBUF_SIZE] = "watcher_logbuf_size",
    [WORKER_LOGBUF_SIZE] = "worker_logbuf_size",
    [SLAB_SIZES] = "slab_sizes",
    [SLAB_CHUNK_MAX] = "slab_chunk_max",
    [TRACK_SIZES] = "track_sizes",
    [NO_INLINE_ASCII_RESP] = "no_inline_ascii_resp",
    [MODERN] = "modern",
    [NO_MODERN] = "no_modern",
    [NO_CHUNKED_ITEMS] = "no_chunked_items",
    [NO_SLAB_REASSIGN] = "no_slab_reassign",
    [NO_SLAB_AUTOMOVE] = "no_slab_automove",
    [NO_MAXCONNS_FAST] = "no_maxconns_fast",
    [INLINE_ASCII_RESP] = "inline_ascii_resp",
    [NO_LRU_CRAWLER] = "no_lru_crawler",
    [NO_LRU_MAINTAINER] = "no_lru_maintainer",
    NULL
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

  char *shortopts =
    "a:"  /* access mask for unix socket */
    "A"  /* enable admin shutdown command */
    "p:"  /* TCP port number to listen on */
    "s:"  /* unix socket path to listen on */
    "U:"  /* UDP port number to listen on */
    "m:"  /* max memory to use for items in megabytes */
    "M"   /* return error on memory exhausted */
    "c:"  /* max simultaneous connections */
    "k"   /* lock down all paged memory */
    "hiV" /* help, licence info, version */
    "r"   /* maximize core file limit */
    "v"   /* verbose */
    "d"   /* daemon mode */
    "l:"  /* interface to listen on */
    "u:"  /* user identity to run as */
    "P:"  /* save PID in file */
    "f:"  /* factor? */
    "n:"  /* minimum space allocated for key+value+flags */
    "t:"  /* threads */
    "D:"  /* prefix delimiter? */
    "L"   /* Large memory pages */
    "R:"  /* max requests per event */
    "C"   /* Disable use of CAS */
    "b:"  /* backlog queue limit */
    "B:"  /* Binding protocol */
    "I:"  /* Max item size */
    "F"   /* Disable flush_all */
    "X"   /* Disable dump commands */
    "o:"  /* Extended generic options */
    ;

  /* process arguments */
#ifdef HAVE_GETOPT_LONG
  const struct option longopts[] = {
    {"unix-mask", required_argument, 0, 'a'},
    {"enable-shutdown", no_argument, 0, 'A'},
    {"port", required_argument, 0, 'p'},
    {"unix-socket", required_argument, 0, 's'},
    {"udp-port", required_argument, 0, 'U'},
    {"memory-limit", required_argument, 0, 'm'},
    {"disable-evictions", no_argument, 0, 'M'},
    {"conn-limit", required_argument, 0, 'c'},
    {"lock-memory", no_argument, 0, 'k'},
    {"help", no_argument, 0, 'h'},
    {"license", no_argument, 0, 'i'},
    {"version", no_argument, 0, 'V'},
    {"enable-coredumps", no_argument, 0, 'r'},
    {"verbose", optional_argument, 0, 'v'},
    {"daemon", no_argument, 0, 'd'},
    {"listen", required_argument, 0, 'l'},
    {"user", required_argument, 0, 'u'},
    {"pidfile", required_argument, 0, 'P'},
    {"slab-growth-factor", required_argument, 0, 'f'},
    {"slab-min-size", required_argument, 0, 'n'},
    {"threads", required_argument, 0, 't'},
    {"enable-largepages", no_argument, 0, 'L'},
    {"max-reqs-per-event", required_argument, 0, 'R'},
    {"disable-cas", no_argument, 0, 'C'},
    {"listen-backlog", required_argument, 0, 'b'},
    {"protocol", required_argument, 0, 'B'},
    {"max-item-size", required_argument, 0, 'I'},
    {"disable-flush-all", no_argument, 0, 'F'},
    {"disable-dumping", no_argument, 0, 'X'},
    {"extended", required_argument, 0, 'o'},
    {0, 0, 0, 0}
  };
  int optindex;
  while (-1 != (c = getopt_long(argc, argv, shortopts,
	  longopts, &optindex))) {
#else
  while (-1 != (c = getopt(argc, argv, shortopts))) {
#endif
    switch (c) {
      case 'A':
	/* enables "shutdown" command */
	settings.shutdown_command = true;
	break;
      case 'm':
	settings.maxbytes = ((size_t)atoi(optarg)) * 1024 * 1024;
	break;
      case 'M':
	settings.evict_to_free = 0;
	break;
      case 'h':
	usage();
	exit(EXIT_SUCCESS);
      case 'i':
	usage_license();
	exit(EXIT_SUCCESS);
      case 'V':
	printf(PACKAGE " " VERSION "\n");
	exit(EXIT_SUCCESS);
      case 'k':
	lock_memory = true;
	break;
      case 'v':
	settings.verbose++;
	break;
      case 'l':
	if (settings.inter != NULL) {
	  if (strstr(settings.inter, optarg) != NULL) {
	    break;
	  }
	  size_t len = strlen(settings.inter) + strlen(optarg) + 2;
	  char *p = malloc(len);
	  if (p == NULL) {
	    fprintf(stderr, "Failed to allocate memory\n");
	    return NULL;
	  }
	  snprintf(p, len, "%s,%s", settings.inter, optarg);
	  free(settings.inter);
	  settings.inter = p;
	} else {
	  settings.inter= strdup(optarg);
	}
	break;
      case 'd':
	do_daemonize = true;
	break;
      case 'r':
	maxcore = 1;
	break;
      case 'R':
	settings.reqs_per_event = atoi(optarg);
	if (settings.reqs_per_event == 0) {
	  fprintf(stderr, "Number of requests per event must be greater than 0\n");
	  return NULL;
	}
	break;
      case 'u':
	username = optarg;
	break;
      case 'P':
	pid_file = optarg;
	break;
      case 'f':
	settings.factor = atof(optarg);
	if (settings.factor <= 1.0) {
	  fprintf(stderr, "Factor must be greater than 1\n");
	  return NULL;
	}
	break;
      case 'n':
	settings.chunk_size = atoi(optarg);
	if (settings.chunk_size == 0) {
	  fprintf(stderr, "Chunk size must be greater than 0\n");
	  return NULL;
	}
	break;
      case 'D':
	if (! optarg || ! optarg[0]) {
	  fprintf(stderr, "No delimiter specified\n");
	  return NULL;
	}
	settings.prefix_delimiter = optarg[0];
	settings.detail_enabled = 1;
	break;
      case 'L' :
	if (enable_large_pages() == 0) {
	  preallocate = true;
	} else {
	  fprintf(stderr, "Cannot enable large pages on this system\n"
	      "(There is no Linux support as of this version)\n");
	  return NULL;
	}
	break;
      case 'C' :
	settings.use_cas = false;
	break;
      case 'b' :
	settings.backlog = atoi(optarg);
	break;
      case 'I':
	buf = strdup(optarg);
	unit = buf[strlen(buf)-1];
	if (unit == 'k' || unit == 'm' ||
	    unit == 'K' || unit == 'M') {
	  buf[strlen(buf)-1] = '\0';
	  size_max = atoi(buf);
	  if (unit == 'k' || unit == 'K')
	    size_max *= 1024;
	  if (unit == 'm' || unit == 'M')
	    size_max *= 1024 * 1024;
	  settings.item_size_max = size_max;
	} else {
	  settings.item_size_max = atoi(buf);
	}
	free(buf);
	break;
      case 'F' :
	settings.flush_enabled = false;
	break;
      case 'X' :
	settings.dump_enabled = false;
	break;
      case 'o': /* It's sub-opts time! */
	subopts_orig = subopts = strdup(optarg); /* getsubopt() changes the original args */

	while (*subopts != '\0') {

	  switch (getsubopt(&subopts, subopts_tokens, &subopts_value)) {
	    case MAXCONNS_FAST:
	      settings.maxconns_fast = true;
	      break;
	    case HASHPOWER_INIT:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing numeric argument for hashpower\n");
		return NULL;
	      }
	      settings.hashpower_init = atoi(subopts_value);
	      if (settings.hashpower_init < 12) {
		fprintf(stderr, "Initial hashtable multiplier of %d is too low\n",
		    settings.hashpower_init);
		return NULL;
	      } else if (settings.hashpower_init > 32) {
		fprintf(stderr, "Initial hashtable multiplier of %d is too high\n"
		    "Choose a value based on \"STAT hash_power_level\" from a running instance\n",
		    settings.hashpower_init);
		return NULL;
	      }
	      break;
	    case NO_HASHEXPAND:
	      start_assoc_maint = false;
	      break;
	    case SLAB_REASSIGN:
	      settings.slab_reassign = true;
	      break;
	    case SLAB_AUTOMOVE:
	      if (subopts_value == NULL) {
		settings.slab_automove = 1;
		break;
	      }
	      settings.slab_automove = atoi(subopts_value);
	      if (settings.slab_automove < 0 || settings.slab_automove > 2) {
		fprintf(stderr, "slab_automove must be between 0 and 2\n");
		return NULL;
	      }
	      break;
	    case SLAB_AUTOMOVE_RATIO:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing slab_automove_ratio argument\n");
		return NULL;
	      }
	      settings.slab_automove_ratio = atof(subopts_value);
	      if (settings.slab_automove_ratio <= 0 || settings.slab_automove_ratio > 1) {
		fprintf(stderr, "slab_automove_ratio must be > 0 and < 1\n");
		return NULL;
	      }
	      break;
	    case SLAB_AUTOMOVE_WINDOW:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing slab_automove_window argument\n");
		return NULL;
	      }
	      settings.slab_automove_window = atoi(subopts_value);
	      if (settings.slab_automove_window < 3) {
		fprintf(stderr, "slab_automove_window must be > 2\n");
		return NULL;
	      }
	      break;
	    case TAIL_REPAIR_TIME:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing numeric argument for tail_repair_time\n");
		return NULL;
	      }
	      settings.tail_repair_time = atoi(subopts_value);
	      if (settings.tail_repair_time < 10) {
		fprintf(stderr, "Cannot set tail_repair_time to less than 10 seconds\n");
		return NULL;
	      }
	      break;
	    case HASH_ALGORITHM:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing hash_algorithm argument\n");
		return NULL;
	      };
	      if (strcmp(subopts_value, "jenkins") == 0) {
		hash_type = JENKINS_HASH;
	      } else if (strcmp(subopts_value, "murmur3") == 0) {
		hash_type = MURMUR3_HASH;
	      } else {
		fprintf(stderr, "Unknown hash_algorithm option (jenkins, murmur3)\n");
		return NULL;
	      }
	      break;
	    case LRU_CRAWLER:
	      start_lru_crawler = true;
	      break;
	    case LRU_CRAWLER_SLEEP:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing lru_crawler_sleep value\n");
		return NULL;
	      }
	      settings.lru_crawler_sleep = atoi(subopts_value);
	      if (settings.lru_crawler_sleep > 1000000 || settings.lru_crawler_sleep < 0) {
		fprintf(stderr, "LRU crawler sleep must be between 0 and 1 second\n");
		return NULL;
	      }
	      break;
	    case LRU_CRAWLER_TOCRAWL:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing lru_crawler_tocrawl value\n");
		return NULL;
	      }
	      if (!safe_strtoul(subopts_value, &tocrawl)) {
		fprintf(stderr, "lru_crawler_tocrawl takes a numeric 32bit value\n");
		return NULL;
	      }
	      settings.lru_crawler_tocrawl = tocrawl;
	      break;
	    case LRU_MAINTAINER:
	      start_lru_maintainer = true;
	      settings.lru_segmented = true;
	      break;
	    case HOT_LRU_PCT:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing hot_lru_pct argument\n");
		return NULL;
	      }
	      settings.hot_lru_pct = atoi(subopts_value);
	      if (settings.hot_lru_pct < 1 || settings.hot_lru_pct >= 80) {
		fprintf(stderr, "hot_lru_pct must be > 1 and < 80\n");
		return NULL;
	      }
	      break;
	    case WARM_LRU_PCT:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing warm_lru_pct argument\n");
		return NULL;
	      }
	      settings.warm_lru_pct = atoi(subopts_value);
	      if (settings.warm_lru_pct < 1 || settings.warm_lru_pct >= 80) {
		fprintf(stderr, "warm_lru_pct must be > 1 and < 80\n");
		return NULL;
	      }
	      break;
	    case HOT_MAX_FACTOR:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing hot_max_factor argument\n");
		return NULL;
	      }
	      settings.hot_max_factor = atof(subopts_value);
	      if (settings.hot_max_factor <= 0) {
		fprintf(stderr, "hot_max_factor must be > 0\n");
		return NULL;
	      }
	      break;
	    case WARM_MAX_FACTOR:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing warm_max_factor argument\n");
		return NULL;
	      }
	      settings.warm_max_factor = atof(subopts_value);
	      if (settings.warm_max_factor <= 0) {
		fprintf(stderr, "warm_max_factor must be > 0\n");
		return NULL;
	      }
	      break;
	    case TEMPORARY_TTL:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing temporary_ttl argument\n");
		return NULL;
	      }
	      settings.temp_lru = true;
	      settings.temporary_ttl = atoi(subopts_value);
	      break;
	    case IDLE_TIMEOUT:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing numeric argument for idle_timeout\n");
		return NULL;
	      }
	      settings.idle_timeout = atoi(subopts_value);
	      break;
	    case WATCHER_LOGBUF_SIZE:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing watcher_logbuf_size argument\n");
		return NULL;
	      }
	      if (!safe_strtoul(subopts_value, &settings.logger_watcher_buf_size)) {
		fprintf(stderr, "could not parse argument to watcher_logbuf_size\n");
		return NULL;
	      }
	      settings.logger_watcher_buf_size *= 1024; /* kilobytes */
	      break;
	    case WORKER_LOGBUF_SIZE:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing worker_logbuf_size argument\n");
		return NULL;
	      }
	      if (!safe_strtoul(subopts_value, &settings.logger_buf_size)) {
		fprintf(stderr, "could not parse argument to worker_logbuf_size\n");
		return NULL;
	      }
	      settings.logger_buf_size *= 1024; /* kilobytes */
	    case SLAB_SIZES:
	      slab_sizes_unparsed = subopts_value;
	      break;
	    case SLAB_CHUNK_MAX:
	      if (subopts_value == NULL) {
		fprintf(stderr, "Missing slab_chunk_max argument\n");
	      }
	      if (!safe_strtol(subopts_value, &settings.slab_chunk_size_max)) {
		fprintf(stderr, "could not parse argument to slab_chunk_max\n");
	      }
	      slab_chunk_size_changed = true;
	      break;
	    case NO_INLINE_ASCII_RESP:
	      settings.inline_ascii_response = false;
	      break;
	    case INLINE_ASCII_RESP:
	      settings.inline_ascii_response = true;
	      break;
	    case NO_CHUNKED_ITEMS:
	      settings.slab_chunk_size_max = settings.slab_page_size;
	      break;
	    case NO_SLAB_REASSIGN:
	      settings.slab_reassign = false;
	      break;
	    case NO_SLAB_AUTOMOVE:
	      settings.slab_automove = 0;
	      break;
	    case NO_MAXCONNS_FAST:
	      settings.maxconns_fast = false;
	      break;
	    case NO_LRU_CRAWLER:
	      settings.lru_crawler = false;
	      start_lru_crawler = false;
	      break;
	    case NO_LRU_MAINTAINER:
	      start_lru_maintainer = false;
	      settings.lru_segmented = false;
	      break;
	    case MODERN:
	      /* currently no new defaults */
	      break;
	    case NO_MODERN:
	      if (!slab_chunk_size_changed) {
		settings.slab_chunk_size_max = settings.slab_page_size;
	      }
	      settings.slab_reassign = false;
	      settings.slab_automove = 0;
	      settings.maxconns_fast = false;
	      settings.inline_ascii_response = true;
	      settings.lru_segmented = false;
	      hash_type = JENKINS_HASH;
	      start_lru_crawler = false;
	      start_lru_maintainer = false;
	      break;
	    default:
	      printf("Illegal suboption \"%s\"\n", subopts_value);
	      return NULL;
	  }

	}
	free(subopts_orig);
	break;
      default:
	fprintf(stderr, "Illegal argument \"%c\"\n", c);
	return NULL;
    }
  }

  if (settings.item_size_max < 1024) {
    fprintf(stderr, "Item max size cannot be less than 1024 bytes.\n");
    exit(EX_USAGE);
  }
  if (settings.item_size_max > (settings.maxbytes / 2)) {
    fprintf(stderr, "Cannot set item size limit higher than 1/2 of memory max.\n");
    exit(EX_USAGE);
  }
  if (settings.item_size_max > (1024 * 1024 * 1024)) {
    fprintf(stderr, "Cannot set item size limit higher than a gigabyte.\n");
    exit(EX_USAGE);
  }
  if (settings.item_size_max > 1024 * 1024) {
    if (!slab_chunk_size_changed) {
      // Ideal new default is 16k, but needs stitching.
      settings.slab_chunk_size_max = settings.slab_page_size / 2;
    }
  }

  if (settings.slab_chunk_size_max > settings.item_size_max) {
    fprintf(stderr, "slab_chunk_max (bytes: %d) cannot be larger than -I (item_size_max %d)\n",
	settings.slab_chunk_size_max, settings.item_size_max);
    exit(EX_USAGE);
  }

  if (settings.item_size_max % settings.slab_chunk_size_max != 0) {
    fprintf(stderr, "-I (item_size_max: %d) must be evenly divisible by slab_chunk_max (bytes: %d)\n",
	settings.item_size_max, settings.slab_chunk_size_max);
    exit(EX_USAGE);
  }

  if (settings.slab_page_size % settings.slab_chunk_size_max != 0) {
    fprintf(stderr, "slab_chunk_max (bytes: %d) must divide evenly into %d (slab_page_size)\n",
	settings.slab_chunk_size_max, settings.slab_page_size);
    exit(EX_USAGE);
  }
  // Reserve this for the new default. If factor size hasn't changed, use
  // new default.
  /*if (settings.slab_chunk_size_max == 16384 && settings.factor == 1.25) {
    settings.factor = 1.08;
    }*/

  if (slab_sizes_unparsed != NULL) {
    if (_parse_slab_sizes(slab_sizes_unparsed, slab_sizes)) {
      use_slab_sizes = true;
    } else {
      exit(EX_USAGE);
    }
  }

  if (settings.hot_lru_pct + settings.warm_lru_pct > 80) {
    fprintf(stderr, "hot_lru_pct + warm_lru_pct cannot be more than 80%% combined\n");
    exit(EX_USAGE);
  }

  if (settings.temp_lru && !start_lru_maintainer) {
    fprintf(stderr, "temporary_ttl requires lru_maintainer to be enabled\n");
    exit(EX_USAGE);
  }

  if (hash_init(hash_type) != 0) {
    fprintf(stderr, "Failed to initialize hash_algorithm!\n");
    exit(EX_USAGE);
  }

  /*
   * Use one workerthread to serve each UDP port if the user specified
   * multiple ports
   */
  if (settings.inter != NULL && strchr(settings.inter, ',')) {
    settings.num_threads_per_udp = 1;
  } else {
    settings.num_threads_per_udp = settings.num_threads;
  }

  if (maxcore != 0) {
    struct rlimit rlim_new;
    /*
     * First try raising to infinity; if that fails, try bringing
     * the soft limit to the hard.
     */
    if (getrlimit(RLIMIT_CORE, &rlim) == 0) {
      rlim_new.rlim_cur = rlim_new.rlim_max = RLIM_INFINITY;
      if (setrlimit(RLIMIT_CORE, &rlim_new)!= 0) {
	/* failed. try raising just to the old max */
	rlim_new.rlim_cur = rlim_new.rlim_max = rlim.rlim_max;
	(void)setrlimit(RLIMIT_CORE, &rlim_new);
      }
    }
    /*
     * getrlimit again to see what we ended up with. Only fail if
     * the soft limit ends up 0, because then no core files will be
     * created at all.
     */

    if ((getrlimit(RLIMIT_CORE, &rlim) != 0) || rlim.rlim_cur == 0) {
      fprintf(stderr, "failed to ensure corefile creation\n");
      exit(EX_OSERR);
    }
  }

  /*
   * If needed, increase rlimits to allow as many connections
   * as needed.
   */

  if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
    fprintf(stderr, "failed to getrlimit number of files\n");
    exit(EX_OSERR);
  } else {
    rlim.rlim_cur = settings.maxconns;
    rlim.rlim_max = settings.maxconns;
    if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
      fprintf(stderr, "failed to set rlimit for open files. Try starting as root or requesting smaller maxconns value.\n");
      exit(EX_OSERR);
    }
  }

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

  /* if we want to ensure our ability to dump core, don't chdir to / */
  if (do_daemonize) {
    if (sigignore(SIGHUP) == -1) {
      perror("Failed to ignore SIGHUP");
    }
    if (daemonize(maxcore, settings.verbose) == -1) {
      fprintf(stderr, "failed to daemon() in order to daemonize\n");
      exit(EXIT_FAILURE);
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

  /* initialize main thread libevent instance */
#if defined(LIBEVENT_VERSION_NUMBER) && LIBEVENT_VERSION_NUMBER >= 0x02000101
  /* If libevent version is larger/equal to 2.0.2-alpha, use newer version */
  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  main_base = event_base_new_with_config(ev_config);
  event_config_free(ev_config);
#else
  /* Otherwise, use older API */
  main_base = event_init();
#endif

  /* initialize other stuff */
  assoc_init(settings.hashpower_init);
  slabs_init(settings.maxbytes, settings.factor, preallocate,
      use_slab_sizes ? slab_sizes : NULL);
  /*
   * ignore SIGPIPE signals; we can use errno == EPIPE if we
   * need that information
   */
  if (sigignore(SIGPIPE) == -1) {
    perror("failed to ignore SIGPIPE; sigaction");
    exit(EX_OSERR);
  }
  /* start up worker threads if MT mode */
  memcached_thread_init(settings.num_threads, NULL);
  init_lru_crawler(NULL);

  if (start_assoc_maint && start_assoc_maintenance_thread() == -1) {
    exit(EXIT_FAILURE);
  }
  if (start_lru_crawler && start_item_crawler_thread() != 0) {
    fprintf(stderr, "Failed to enable LRU crawler thread\n");
    exit(EXIT_FAILURE);
  }
  if (start_lru_maintainer && start_lru_maintainer_thread(NULL) != 0) {
    fprintf(stderr, "Failed to enable LRU maintainer thread\n");
    return NULL;
  }

  if (settings.slab_reassign &&
      start_slab_maintenance_thread() == -1) {
    exit(EXIT_FAILURE);
  }

  /* initialise clock event */
  clock_handler(0, 0, 0);

  if (pid_file != NULL) {
    save_pid(pid_file);
  }

  /* Initialize the uriencode lookup table. */
  uriencode_init();

  /* enter the event loop */
  // TODO chnage to wait on maintenance threads
  pthread_mutex_unlock(&begin_ops_mutex);
  pthread_mutex_lock(&end_mutex);

  stop_assoc_maintenance_thread();

  /* remove the PID file if we're a daemon */
  if (do_daemonize)
    remove_pidfile(pid_file);
  /* Clean up strdup() call for bind() address */
  if (settings.inter)
    free(settings.inter);

  /* cleanup base */
  event_base_free(main_base);

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


int pku_memcached_get(struct get_struct *gs){
  inc_lookers();
  item* it = item_get(gs->k, gs->nk, gs->exptime, 1);
  if (it == NULL)
    return 2;
  if (gs->buffLen < it->nbytes)
    return 1;
  memcpy(gs->buffer, ITEM_data(it), it->nbytes);
  dec_lookers();
  return 0;
}

void pku_memcached_touch(struct touch_struct *ts){
  inc_lookers();
  item_get(ts->k, ts->nk, ts->exptime, 0);
  dec_lookers();
}

void pku_memcached_insert(struct insert_struct *is){
  // do what we're here for
  struct st_st *tup;
  inc_lookers();
  item *it = item_alloc(is->k, is->nk, 0, realtime(is->exptime), is->dn + 2);

  if (it != NULL) {
    memcpy(ITEM_data(it), is->d, is->dn);
    memcpy(ITEM_data(it) + is->dn, "\r\n", 2);
    uint32_t hv = hash(is->k, is->nk);
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


