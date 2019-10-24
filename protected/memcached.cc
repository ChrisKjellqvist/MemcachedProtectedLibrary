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
    if(delta > (int64_t)value) {
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
        if (!safe_strtoul(p, &size) || (int)size < settings.chunk_size
             || (int)size > settings.slab_chunk_size_max) {
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
  am_server = 1;
  printf("server started\n");
  fflush(stdout);
  bool lock_memory = false;
  bool do_daemonize = false;
  bool preallocate = false;
  int maxcore = 0;
  char *username = NULL;
  char *pid_file = NULL;
  struct passwd *pw;
  struct rlimit rlim;
  bool start_lru_maintainer = true;
  bool start_lru_crawler = true;
  bool start_assoc_maint = true;
  enum hashfunc_type hash_type = MURMUR3_HASH;
  uint32_t slab_sizes[MAX_NUMBER_OF_SLAB_CLASSES];
  bool use_slab_sizes = false;
  char *slab_sizes_unparsed = NULL;
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

  /* initialize other stuff */
  assoc_init(settings.hashpower_init);
  slabs_init(settings.maxbytes, settings.factor, preallocate,
      use_slab_sizes ? slab_sizes : NULL);
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
  pthread_mutex_init(&end_mutex, nullptr);
  pthread_mutex_lock(&end_mutex);
  pthread_mutex_lock(&end_mutex);

  stop_assoc_maintenance_thread();

  /* remove the PID file if we're a daemon */
  if (do_daemonize)
    remove_pidfile(pid_file);
  /* Clean up strdup() call for bind() address */
  if (settings.inter)
    free(settings.inter);

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


