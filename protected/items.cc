/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "memcached.h"
#include "bipbuffer.h"
#include "slab_automove.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <poll.h>

#include <rpmalloc.hpp>

/* Forward Declarations */
static void item_link_q(item *it);
static void item_unlink_q(item *it);

#define LARGEST_ID POWER_LARGEST
struct itemstats_t {
  uint64_t evicted;
  uint64_t evicted_nonzero;
  uint64_t reclaimed;
  uint64_t outofmemory;
  uint64_t tailrepairs;
  uint64_t expired_unfetched; /* items reclaimed but never touched */
  uint64_t evicted_unfetched; /* items evicted but never touched */
  uint64_t evicted_active; /* items evicted that should have been shuffled */
  uint64_t crawler_reclaimed;
  uint64_t crawler_items_checked;
  uint64_t lrutail_reflocked;
  uint64_t moves_to_cold;
  uint64_t moves_to_warm;
  uint64_t moves_within_lru;
  uint64_t direct_reclaims;
  uint64_t hits_to_hot;
  uint64_t hits_to_warm;
  uint64_t hits_to_cold;
  uint64_t hits_to_temp;
  rel_time_t evicted_time;
};

static pptr<item> *heads; // LARGEST_ID
static pptr<item> *tails; // LARGEST_ID
static itemstats_t *itemstats; // LARGEST_ID
static unsigned int *sizes; // LARGEST_ID
static uint64_t *sizes_bytes; // LARGEST_ID

static volatile int do_run_lru_maintainer_thread = 0;
static int lru_maintainer_initialized = 0;

// These (AFAIK) should only be used by the server, and so we don't need
// to make heads for them in the mmapped region 
static pthread_mutex_t lru_maintainer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cas_id_lock = PTHREAD_MUTEX_INITIALIZER;

// PLIB variables
extern int am_server;
extern int is_restart;

void items_init(){
  if (!am_server || is_restart){
    // get roots
    heads = (pptr<item>*)RP_get_root(RPMRoot::Heads);
    tails = (pptr<item>*)RP_get_root(RPMRoot::Tails);
    itemstats = (itemstats_t*)RP_get_root(RPMRoot::ItemStats);
    sizes = (unsigned int*)RP_get_root(RPMRoot::Sizes);
    sizes_bytes = (uint64_t*)RP_get_root(RPMRoot::SizesBytes);
  } else {
    heads = (pptr<item>*)RP_malloc(sizeof(item*)*LARGEST_ID);
    tails = (pptr<item>*)RP_malloc(sizeof(item*)*LARGEST_ID);
    itemstats = (itemstats_t*)RP_malloc(sizeof(item*)*LARGEST_ID);
    sizes = (unsigned int*)RP_malloc(sizeof(item*)*LARGEST_ID);
    sizes_bytes = (uint64_t*)RP_malloc(sizeof(item*)*LARGEST_ID);
    RP_set_root(heads, RPMRoot::Heads);
    RP_set_root(tails, RPMRoot::Tails);
    RP_set_root(itemstats, RPMRoot::ItemStats);
    RP_set_root(sizes, RPMRoot::Sizes);
    RP_set_root(sizes_bytes, RPMRoot::Sizes);
  }
}

void item_stats_reset(void) {
  int i;
  for (i = 0; i < LARGEST_ID; i++) {
    pthread_mutex_lock(&lru_locks[i]);
    memset(&itemstats[i], 0, sizeof(itemstats_t));
    pthread_mutex_unlock(&lru_locks[i]);
  }
}

/* called with class lru lock held */
void do_item_stats_add_crawl(const int i, const uint64_t reclaimed,
    const uint64_t unfetched, const uint64_t checked) {
  itemstats[i].crawler_reclaimed += reclaimed;
  itemstats[i].expired_unfetched += unfetched;
  itemstats[i].crawler_items_checked += checked;
}

struct  lru_bump_buf {
  lru_bump_buf *prev;
  lru_bump_buf *next;
  pthread_mutex_t mutex;
  bipbuf_t *buf;
  uint64_t dropped;
};

struct lru_bump_entry{
  item *it;
  uint32_t hv;
};

static lru_bump_buf *bump_buf_head = NULL;
static lru_bump_buf *bump_buf_tail = NULL;
static pthread_mutex_t bump_buf_lock = PTHREAD_MUTEX_INITIALIZER;
/* TODO: tunable? Need bench results */
#define LRU_BUMP_BUF_SIZE 8192

/* Get the next CAS id for a new item. */
/* TODO: refactor some atomics for this. */
uint64_t get_cas_id(void) {
  static uint64_t cas_id = 0;
  pthread_mutex_lock(&cas_id_lock);
  uint64_t next_id = ++cas_id;
  pthread_mutex_unlock(&cas_id_lock);
  return next_id;
}

int item_is_flushed(item *it) {
  rel_time_t oldest_live = settings.oldest_live;
  uint64_t cas = ITEM_get_cas(it);
  uint64_t oldest_cas = settings.oldest_cas;
  if (oldest_live == 0 || oldest_live > current_time)
    return 0;
  if ((it->time <= oldest_live)
      || (oldest_cas != 0 && cas != 0 && cas < oldest_cas)) {
    return 1;
  }
  return 0;
}

static unsigned int temp_lru_size(int slabs_clsid) {
  int id = CLEAR_LRU(slabs_clsid);
  id |= TEMP_LRU;
  unsigned int ret;
  pthread_mutex_lock(&lru_locks[id]);
  ret = sizes_bytes[id];
  pthread_mutex_unlock(&lru_locks[id]);
  return ret;
}

/* must be locked before call */
unsigned int do_get_lru_size(uint32_t id) {
  return sizes[id];
}

/* Enable this for reference-count debugging. */
#if 0
# define DEBUG_REFCNT(it,op) \
  fprintf(stderr, "item %x refcnt(%c) %d %c%c%c\n", \
      it, op, it->refcount, \
      (it->it_flags & ITEM_LINKED) ? 'L' : ' ', \
      (it->it_flags & ITEM_SLABBED) ? 'S' : ' ')
#else
# define DEBUG_REFCNT(it,op) while(0)
#endif

/**
 * Generates the variable-sized part of the header for an object.
 *
 * key     - The key
 * nkey    - The length of the key
 * flags   - key flags
 * nbytes  - Number of bytes to hold value and addition CRLF terminator
 * suffix  - Buffer for the "VALUE" line suffix (flags, size).
 * nsuffix - The length of the suffix is stored here.
 *
 * Returns the total size of the header.
 */
static size_t item_make_header(const uint8_t nkey, const unsigned int flags, const int nbytes,
    char *suffix, uint8_t *nsuffix) {
  if (flags == 0) {
    *nsuffix = 0;
  } else {
    *nsuffix = sizeof(flags);
  }
  return sizeof(item) + nkey + *nsuffix + nbytes;
}
item *do_item_alloc_pull(const size_t ntotal, const unsigned int id) {
  item *it = NULL;
  int i;
  /* If no memory is available, attempt a direct LRU juggle/eviction */
  /* This is a race in order to simplify lru_pull_tail; in cases where
   * locked items are on the tail, you want them to fall out and cause
   * occasional OOM's, rather than internally work around them.
   * This also gives one fewer code path for slab alloc/free
   */
  for (i = 0; i < 10; i++) {
    uint64_t total_bytes;
    /* Try to reclaim memory first */
    lru_pull_tail(id, COLD_LRU, 0, 0, 0, NULL);
    it = (item*)slabs_alloc(ntotal, id, &total_bytes, 0);

    if (it == NULL) {
      if (lru_pull_tail(id, COLD_LRU, total_bytes, LRU_PULL_EVICT, 0, NULL) <= 0) {
        lru_pull_tail(id, HOT_LRU, total_bytes, 0, 0, NULL);
      }
    } else {
      break;
    }
  }

  if (i > 0) {
    pthread_mutex_lock(&lru_locks[id]);
    itemstats[id].direct_reclaims += i;
    pthread_mutex_unlock(&lru_locks[id]);
  }

  return it;
}

/* Chain another chunk onto this chunk. */
/* slab mover: if it finds a chunk without ITEM_CHUNK flag, and no ITEM_LINKED
 * flag, it counts as busy and skips.
 * I think it might still not be safe to do linking outside of the slab lock
 */
item_chunk *do_item_alloc_chunk(item_chunk *ch, const size_t bytes_remain) {
  // TODO: Should be a cleaner way of finding real size with slabber calls
  size_t size = bytes_remain + sizeof(item_chunk);
  if (size > (size_t)settings.slab_chunk_size_max)
    size = (size_t)settings.slab_chunk_size_max;
  unsigned int id = slabs_clsid(size);

  item_chunk *nch = (item_chunk *) do_item_alloc_pull(size, id);
  if (nch == NULL)
    return NULL;

  // link in.
  // ITEM_CHUNK[ED] bits need to be protected by the slabs lock.
  slabs_mlock();
  nch->head = ch->head;
  ch->next = nch;
  nch->prev = ch;
  nch->next = 0;
  nch->used = 0;
  nch->slabs_clsid = id;
  nch->size = size - sizeof(item_chunk);
  nch->it_flags |= ITEM_CHUNK;
  slabs_munlock();
  return nch;
}
item *do_item_alloc(char *key, const size_t nkey, const unsigned int flags,
    const rel_time_t exptime, const int nbytes) {
  uint8_t nsuffix;
  item *it = NULL;
  char suffix[40];
  // Avoid potential underflows.
  if (nbytes < 2)
    return 0;

  size_t ntotal = item_make_header(nkey + 1, flags, nbytes, suffix, &nsuffix);
  ntotal += sizeof(uint64_t);

  unsigned int id = slabs_clsid(ntotal);
  unsigned int hdr_id = 0;
  if (id == 0)
    return 0;

  /* This is a large item. Allocate a header object now, lazily allocate
   *  chunks while reading the upload.
   */
  if (ntotal > (size_t)settings.slab_chunk_size_max) {
    /* We still link this item into the LRU for the larger slab class, but
     * we're pulling a header from an entirely different slab class. The
     * free routines handle large items specifically.
     */
    int htotal = nkey + 1 + nsuffix + sizeof(item) + sizeof(item_chunk) 
      + sizeof(uint64_t);
#ifdef NEED_ALIGN
    // header chunk needs to be padded on some systems
    int remain = htotal % 8;
    if (remain != 0) {
      htotal += 8 - remain;
    }
#endif
    hdr_id = slabs_clsid(htotal);
    it = do_item_alloc_pull(htotal, hdr_id);
    /* setting ITEM_CHUNKED is fine here because we aren't LINKED yet. */
    if (it != NULL)
      it->it_flags |= ITEM_CHUNKED;
  } else {
    it = do_item_alloc_pull(ntotal, id);
  }

  if (it == NULL) {
    pthread_mutex_lock(&lru_locks[id]);
    itemstats[id].outofmemory++;
    pthread_mutex_unlock(&lru_locks[id]);
    return NULL;
  }

  assert(it->slabs_clsid == 0);
  //assert(it != heads[id]);

  /* Refcount is seeded to 1 by slabs_alloc() */
  it->next = it->prev = 0;

  /* Items are initially loaded into the HOT_LRU. This is '0' but I want at
   * least a note here. Compiler (hopefully?) optimizes this out.
   */
  id |= HOT_LRU;
  it->slabs_clsid = id;

  DEBUG_REFCNT(it, '*');
  it->it_flags |= ITEM_CAS;
  it->nkey = nkey;
  it->nbytes = nbytes;
  memcpy(ITEM_key(it), key, nkey);
  it->exptime = exptime;
  if (nsuffix > 0) {
    memcpy(ITEM_suffix(it), &flags, sizeof(flags));
  }
  it->nsuffix = nsuffix;

  /* Initialize internal chunk. */
  if (it->it_flags & ITEM_CHUNKED) {
    item_chunk *chunk = (item_chunk *) ITEM_schunk(it);

    chunk->next = 0;
    chunk->prev = 0;
    chunk->used = 0;
    chunk->size = 0;
    chunk->head = it;
    chunk->orig_clsid = hdr_id;
  }
  it->h_next = 0;

  return it;
}

void item_free(item *it) {
  size_t ntotal = ITEM_ntotal(it);
  unsigned int clsid;
  assert((it->it_flags & ITEM_LINKED) == 0);
  assert(it != heads[it->slabs_clsid]);
  assert(it != tails[it->slabs_clsid]);
  assert(it->refcount == 0);

  /* so slab size changer can tell later if item is already free or not */
  clsid = ITEM_clsid(it);
  DEBUG_REFCNT(it, 'F');
  slabs_free(it, ntotal, clsid);
}

/**
 * Returns true if an item will fit in the cache (its size does not exceed
 * the maximum for a cache entry.)
 */
bool item_size_ok(const size_t nkey, const int flags, const int nbytes) {
  char prefix[40];
  uint8_t nsuffix;
  if (nbytes < 2)
    return false;

  size_t ntotal = item_make_header(nkey + 1, flags, nbytes,
      prefix, &nsuffix);
    ntotal += sizeof(uint64_t);
  return slabs_clsid(ntotal) != 0;
}

static void do_item_link_q(item *it) { /* item is the new head */
  pptr<item> *head, *tail;
  assert((it->it_flags & ITEM_SLABBED) == 0);

  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];
  assert(it != *head);
  assert((*head && *tail) || (*head == 0 && *tail == 0));
  it->prev = 0;
  it->next = *head;
  if (it->next != nullptr) it->next->prev = it;
  *head = it;
  if (*tail == 0) *tail = it;
  sizes[it->slabs_clsid]++;
  sizes_bytes[it->slabs_clsid] += ITEM_ntotal(it);

  return;
}

static void item_link_q(item *it) {
  pthread_mutex_lock(&lru_locks[it->slabs_clsid]);
  do_item_link_q(it);
  pthread_mutex_unlock(&lru_locks[it->slabs_clsid]);
}

static void item_link_q_warm(item *it) {
  pthread_mutex_lock(&lru_locks[it->slabs_clsid]);
  do_item_link_q(it);
  itemstats[it->slabs_clsid].moves_to_warm++;
  pthread_mutex_unlock(&lru_locks[it->slabs_clsid]);
}

static void do_item_unlink_q(item *it) {
  pptr<item> *head, *tail;
  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];

  if (*head == it) {
    assert(it->prev == 0);
    *head = it->next;
  }
  if (*tail == it) {
    assert(it->next == 0);
    *tail = it->prev;
  }
  assert(it->next != it);
  assert(it->prev != it);

  if (it->next != nullptr) it->next->prev = it->prev;
  if (it->prev != nullptr) it->prev->next = it->next;
  sizes[it->slabs_clsid]--;
  sizes_bytes[it->slabs_clsid] -= ITEM_ntotal(it);
  return;
}

static void item_unlink_q(item *it) {
  pthread_mutex_lock(&lru_locks[it->slabs_clsid]);
  do_item_unlink_q(it);
  pthread_mutex_unlock(&lru_locks[it->slabs_clsid]);
}

int do_item_link(item *it, const uint32_t hv) {
  MEMCACHED_ITEM_LINK(ITEM_key(it), it->nkey, it->nbytes);
  assert((it->it_flags & (ITEM_LINKED|ITEM_SLABBED)) == 0);
  it->it_flags |= ITEM_LINKED;
  it->time = current_time;

  STATS_LOCK();
  stats_state.curr_bytes += ITEM_ntotal(it);
  stats_state.curr_items += 1;
  stats.total_items += 1;
  STATS_UNLOCK();

  /* Allocate a new CAS ID on link. */
  ITEM_set_cas(it, get_cas_id());
  assoc_insert(it, hv);
  item_link_q(it);
  refcount_incr(it);
  item_stats_sizes_add(it);

  return 1;
}

void do_item_unlink(item *it, const uint32_t hv) {
  MEMCACHED_ITEM_UNLINK(ITEM_key(it), it->nkey, it->nbytes);
  if ((it->it_flags & ITEM_LINKED) != 0) {
    it->it_flags &= ~ITEM_LINKED;
    STATS_LOCK();
    stats_state.curr_bytes -= ITEM_ntotal(it);
    stats_state.curr_items -= 1;
    STATS_UNLOCK();
    item_stats_sizes_remove(it);
    assoc_delete(ITEM_key(it), it->nkey, hv);
    item_unlink_q(it);
    do_item_remove(it);
  }
}

/* FIXME: Is it necessary to keep this copy/pasted code? */
void do_item_unlink_nolock(item *it, const uint32_t hv) {
  MEMCACHED_ITEM_UNLINK(ITEM_key(it), it->nkey, it->nbytes);
  if ((it->it_flags & ITEM_LINKED) != 0) {
    it->it_flags &= ~ITEM_LINKED;
    STATS_LOCK();
    stats_state.curr_bytes -= ITEM_ntotal(it);
    stats_state.curr_items -= 1;
    STATS_UNLOCK();
    item_stats_sizes_remove(it);
    assoc_delete(ITEM_key(it), it->nkey, hv);
    do_item_unlink_q(it);
    do_item_remove(it);
  }
}

void do_item_remove(item *it) {
  MEMCACHED_ITEM_REMOVE(ITEM_key(it), it->nkey, it->nbytes);
  assert((it->it_flags & ITEM_SLABBED) == 0);
  assert(it->refcount > 0);

  if (refcount_decr(it) == 0) {
    item_free(it);
  }
}

/* Copy/paste to avoid adding two extra branches for all common calls, since
 * _nolock is only used in an uncommon case where we want to relink. */
void do_item_update_nolock(item *it) {
  MEMCACHED_ITEM_UPDATE(ITEM_key(it), it->nkey, it->nbytes);
  if (it->time < current_time - ITEM_UPDATE_INTERVAL) {
    assert((it->it_flags & ITEM_SLABBED) == 0);

    if ((it->it_flags & ITEM_LINKED) != 0) {
      do_item_unlink_q(it);
      it->time = current_time;
      do_item_link_q(it);
    }
  }
}

/* Bump the last accessed time, or relink if we're in compat mode */
void do_item_update(item *it) {
  MEMCACHED_ITEM_UPDATE(ITEM_key(it), it->nkey, it->nbytes);

  /* Hits to COLD_LRU immediately move to WARM. */
  assert((it->it_flags & ITEM_SLABBED) == 0);
  if ((it->it_flags & ITEM_LINKED) != 0) {
    if (ITEM_lruid(it) == COLD_LRU && (it->it_flags & ITEM_ACTIVE)) {
      it->time = current_time;
      item_unlink_q(it);
      it->slabs_clsid = ITEM_clsid(it);
      it->slabs_clsid |= WARM_LRU;
      it->it_flags &= ~ITEM_ACTIVE;
      item_link_q_warm(it);
    } else if (it->time < current_time - ITEM_UPDATE_INTERVAL) {
      it->time = current_time;
    }
  }
}

int do_item_replace(item *it, item *new_it, const uint32_t hv) {
  MEMCACHED_ITEM_REPLACE(ITEM_key(it), it->nkey, it->nbytes,
      ITEM_key(new_it), new_it->nkey, new_it->nbytes);
  assert((it->it_flags & ITEM_SLABBED) == 0);

  do_item_unlink(it, hv);
  return do_item_link(new_it, hv);
}

/*@null@*/
/* This is walking the line of violating lock order, but I think it's safe.
 * If the LRU lock is held, an item in the LRU cannot be wiped and freed.
 * The data could possibly be overwritten, but this is only accessing the
 * headers.
 * It may not be the best idea to leave it like this, but for now it's safe.
 */
char *item_cachedump(const unsigned int slabs_clsid, const unsigned int limit, unsigned int *bytes) {
  unsigned int memlimit = 2 * 1024 * 1024;   /* 2MB max response size */
  char *buffer;
  unsigned int bufcurr;
  item *it;
  unsigned int len;
  unsigned int shown = 0;
  char key_temp[KEY_MAX_LENGTH + 1];
  char temp[512];
  unsigned int id = slabs_clsid;
  id |= COLD_LRU;

  pthread_mutex_lock(&lru_locks[id]);
  it = heads[id];

  buffer = (char*)malloc((size_t)memlimit);
  if (buffer == 0) {
    return NULL;
  }
  bufcurr = 0;

  while (it != NULL && (limit == 0 || shown < limit)) {
    assert(it->nkey <= KEY_MAX_LENGTH);
    if (it->nbytes == 0 && it->nkey == 0) {
      it = it->next;
      continue;
    }
    /* Copy the key since it may not be null-terminated in the struct */
    strncpy(key_temp, ITEM_key(it), it->nkey);
    key_temp[it->nkey] = 0x00; /* terminate */
    len = snprintf(temp, sizeof(temp), "ITEM %s [%d b; %llu s]\r\n",
        key_temp, it->nbytes - 2,
        it->exptime == 0 ? 0 :
        (unsigned long long)it->exptime + process_started);
    if (bufcurr + len + 6 > memlimit)  /* 6 is END\r\n\0 */
      break;
    memcpy(buffer + bufcurr, temp, len);
    bufcurr += len;
    shown++;
    it = it->next;
  }

  memcpy(buffer + bufcurr, "END\r\n", 6);
  bufcurr += 5;

  *bytes = bufcurr;
  pthread_mutex_unlock(&lru_locks[id]);
  return buffer;
}

/* With refactoring of the various stats code the automover won't need a
 * custom function here.
 */
void fill_item_stats_automove(item_stats_automove *am) {
  int n;
  for (n = 0; n < MAX_NUMBER_OF_SLAB_CLASSES; n++) {
    item_stats_automove *cur = &am[n];

    // outofmemory records into HOT
    int i = n | HOT_LRU;
    pthread_mutex_lock(&lru_locks[i]);
    cur->outofmemory = itemstats[i].outofmemory;
    pthread_mutex_unlock(&lru_locks[i]);

    // evictions and tail age are from COLD
    i = n | COLD_LRU;
    pthread_mutex_lock(&lru_locks[i]);
    cur->evicted = itemstats[i].evicted;
    if (tails[i]) {
      cur->age = current_time - tails[i]->time;
    } else {
      cur->age = 0;
    }
    pthread_mutex_unlock(&lru_locks[i]);
  }
}

void item_stats_sizes_add(item *it) {
  return;
}

void item_stats_sizes_remove(item *it) {
  return;
}

/** wrapper around assoc_find which does the lazy expiration logic */
item *do_item_get(const char *key, const size_t nkey, const uint32_t hv, const bool do_update) {
  item *it = assoc_find(key, nkey, hv);
  if (it != NULL) {
    refcount_incr(it);
  }
  if (it != NULL) {
    if (item_is_flushed(it)) {
      do_item_unlink(it, hv);
    } else if (it->exptime != 0 && it->exptime <= current_time) {
      do_item_unlink(it, hv);
      do_item_remove(it);
      it = NULL;
    } else {
      if (do_update) {
        /* We update the hit markers only during fetches.
         * An item needs to be hit twice overall to be considered
         * ACTIVE, but only needs a single hit to maintain activity
         * afterward.
         * FETCHED tells if an item has ever been active.
         */
        if ((it->it_flags & ITEM_ACTIVE) == 0) {
          if ((it->it_flags & ITEM_FETCHED) == 0) {
            it->it_flags |= ITEM_FETCHED;
          } else {
            it->it_flags |= ITEM_ACTIVE;
            if (ITEM_lruid(it) != COLD_LRU) {
              do_item_update(it); // bump LA time
            }
          }
        }
      }
      DEBUG_REFCNT(it, '+');
    }
  }

  return it;
}

item *do_item_touch(const char *key, size_t nkey, uint32_t exptime,
    const uint32_t hv) {
  item *it = do_item_get(key, nkey, hv, DO_UPDATE);
  if (it != NULL) {
    it->exptime = exptime;
  }
  return it;
}

/*** LRU MAINTENANCE THREAD ***/

/* Returns number of items remove, expired, or evicted.
 * Callable from worker threads or the LRU maintainer thread */
int lru_pull_tail(const int orig_id, const int cur_lru,
    const uint64_t total_bytes, const uint8_t flags, const rel_time_t max_age,
    lru_pull_tail_return *ret_it) {
  item *it = NULL;
  int id = orig_id;
  int removed = 0;
  if (id == 0)
    return 0;

  int tries = 5;
  item *search;
  item *next_it;
  void *hold_lock = NULL;
  unsigned int move_to_lru = 0;
  uint64_t limit = 0;

  id |= cur_lru;
  pthread_mutex_lock(&lru_locks[id]);
  search = tails[id];
  /* We walk up *only* for locked items, and if bottom is expired. */
  for (; tries > 0 && search != NULL; tries--, search=next_it) {
    /* we might relink search mid-loop, so search->prev isn't reliable */
    next_it = search->prev;
    if (search->nbytes == 0 && search->nkey == 0 && search->it_flags == 1) {
      /* We are a crawler, ignore it. */
      if (flags & LRU_PULL_CRAWL_BLOCKS) {
        pthread_mutex_unlock(&lru_locks[id]);
        return 0;
      }
      tries++;
      continue;
    }
    uint32_t hv = tcd_hash(ITEM_key(search), search->nkey);
    /* Attempt to hash item lock the "search" item. If locked, no
     * other callers can incr the refcount. Also skip ourselves. */
    if ((hold_lock = item_trylock(hv)) == NULL)
      continue;
    /* Now see if the item is refcount locked */
    if (refcount_incr(search) != 2) {
      /* Note pathological case with ref'ed items in tail.
       * Can still unlink the item, but it won't be reusable yet */
      itemstats[id].lrutail_reflocked++;
      /* In case of refcount leaks, enable for quick workaround. */
      /* WARNING: This can cause terrible corruption */
      if (settings.tail_repair_time &&
          search->time + settings.tail_repair_time < current_time) {
        itemstats[id].tailrepairs++;
        search->refcount = 1;
        /* This will call item_remove -> item_free since refcnt is 1 */
        STORAGE_delete(ext_storage, search);
        do_item_unlink_nolock(search, hv);
        item_trylock_unlock(hold_lock);
        continue;
      }
    }

    /* Expired or flushed */
    if ((search->exptime != 0 && search->exptime < current_time)
        || item_is_flushed(search)) {
      itemstats[id].reclaimed++;
      if ((search->it_flags & ITEM_FETCHED) == 0) {
        itemstats[id].expired_unfetched++;
      }
      /* refcnt 2 -> 1 */
      do_item_unlink_nolock(search, hv);
      STORAGE_delete(ext_storage, search);
      /* refcnt 1 -> 0 -> item_free */
      do_item_remove(search);
      item_trylock_unlock(hold_lock);
      removed++;

      /* If all we're finding are expired, can keep going */
      continue;
    }

    /* If we're HOT_LRU or WARM_LRU and over size limit, send to COLD_LRU.
     * If we're COLD_LRU, send to WARM_LRU unless we need to evict
     */
    switch (cur_lru) {
      case HOT_LRU:
        limit = total_bytes * 20 / 100;
      case WARM_LRU:
        if (limit == 0)
          limit = total_bytes * 40 / 100;
        /* Rescue ACTIVE items aggressively */
        if ((search->it_flags & ITEM_ACTIVE) != 0) {
          search->it_flags &= ~ITEM_ACTIVE;
          removed++;
          if (cur_lru == WARM_LRU) {
            itemstats[id].moves_within_lru++;
            do_item_update_nolock(search);
            do_item_remove(search);
            item_trylock_unlock(hold_lock);
          } else {
            /* Active HOT_LRU items flow to WARM */
            itemstats[id].moves_to_warm++;
            move_to_lru = WARM_LRU;
            do_item_unlink_q(search);
            it = search;
          }
        } else if (sizes_bytes[id] > limit ||
            current_time - search->time > max_age) {
          itemstats[id].moves_to_cold++;
          move_to_lru = COLD_LRU;
          do_item_unlink_q(search);
          it = search;
          removed++;
          break;
        } else {
          /* Don't want to move to COLD, not active, bail out */
          it = search;
        }
        break;
      case COLD_LRU:
        it = search; /* No matter what, we're stopping */
        if (flags & LRU_PULL_EVICT) {
          if (settings.evict_to_free == 0) {
            /* Don't think we need a counter for this. It'll OOM.  */
            break;
          }
          itemstats[id].evicted++;
          itemstats[id].evicted_time = current_time - search->time;
          if (search->exptime != 0)
            itemstats[id].evicted_nonzero++;
          if ((search->it_flags & ITEM_FETCHED) == 0) {
            itemstats[id].evicted_unfetched++;
          }
          if ((search->it_flags & ITEM_ACTIVE)) {
            itemstats[id].evicted_active++;
          }
          STORAGE_delete(ext_storage, search);
          do_item_unlink_nolock(search, hv);
          removed++;
          if (settings.slab_automove == 2) {
            slabs_reassign(-1, orig_id);
          }
        } else if (flags & LRU_PULL_RETURN_ITEM) {
          /* Keep a reference to this item and return it. */
          ret_it->it = it;
          ret_it->hv = hv;
        } else if ((search->it_flags & ITEM_ACTIVE) != 0) {
          itemstats[id].moves_to_warm++;
          search->it_flags &= ~ITEM_ACTIVE;
          move_to_lru = WARM_LRU;
          do_item_unlink_q(search);
          removed++;
        }
        break;
      case TEMP_LRU:
        it = search; /* Kill the loop. Parent only interested in reclaims */
        break;
    }
    if (it != NULL)
      break;
  }

  pthread_mutex_unlock(&lru_locks[id]);

  if (it != NULL) {
    if (move_to_lru) {
      it->slabs_clsid = ITEM_clsid(it);
      it->slabs_clsid |= move_to_lru;
      item_link_q(it);
    }
    if ((flags & LRU_PULL_RETURN_ITEM) == 0) {
      do_item_remove(it);
      item_trylock_unlock(hold_lock);
    }
  }

  return removed;
}


/* TODO: Third place this code needs to be deduped */
static void lru_bump_buf_link_q(lru_bump_buf *b) {
  pthread_mutex_lock(&bump_buf_lock);
  assert(b != bump_buf_head);

  b->prev = 0;
  b->next = bump_buf_head;
  if (b->next) b->next->prev = b;
  bump_buf_head = b;
  if (bump_buf_tail == 0) bump_buf_tail = b;
  pthread_mutex_unlock(&bump_buf_lock);
  return;
}

void *item_lru_bump_buf_create(void) {
  lru_bump_buf *b = (lru_bump_buf*)calloc(1, sizeof(lru_bump_buf));
  if (b == NULL) {
    return NULL;
  }

  b->buf = bipbuf_new(sizeof(lru_bump_entry) * LRU_BUMP_BUF_SIZE);
  if (b->buf == NULL) {
    free(b);
    return NULL;
  }

  pthread_mutex_init(&b->mutex, NULL);

  lru_bump_buf_link_q(b);
  return b;
}


/* TODO: Might be worth a micro-optimization of having bump buffers link
 * themselves back into the central queue when queue goes from zero to
 * non-zero, then remove from list if zero more than N times.
 * If very few hits on cold this would avoid extra memory barriers from LRU
 * maintainer thread. If many hits, they'll just stay in the list.
 */
static bool lru_maintainer_bumps(void) {
  lru_bump_buf *b;
  lru_bump_entry *be;
  unsigned int size;
  unsigned int todo;
  bool bumped = false;
  pthread_mutex_lock(&bump_buf_lock);
  for (b = bump_buf_head; b != NULL; b=b->next) {
    pthread_mutex_lock(&b->mutex);
    be = (lru_bump_entry *) bipbuf_peek_all(b->buf, &size);
    pthread_mutex_unlock(&b->mutex);

    if (be == NULL) {
      continue;
    }
    todo = size;
    bumped = true;

    while (todo) {
      item_lock(be->hv);
      do_item_update(be->it);
      do_item_remove(be->it);
      item_unlock(be->hv);
      be++;
      todo -= sizeof(lru_bump_entry);
    }

    pthread_mutex_lock(&b->mutex);
    be = (lru_bump_entry *) bipbuf_poll(b->buf, size);
    pthread_mutex_unlock(&b->mutex);
  }
  pthread_mutex_unlock(&bump_buf_lock);
  return bumped;
}

/* Loop up to N times:
 * If too many items are in HOT_LRU, push to COLD_LRU
 * If too many items are in WARM_LRU, push to COLD_LRU
 * If too many items are in COLD_LRU, poke COLD_LRU tail
 * 1000 loops with 1ms min sleep gives us under 1m items shifted/sec. The
 * locks can't handle much more than that. Leaving a TODO for how to
 * autoadjust in the future.
 */
static int lru_maintainer_juggle(const int slabs_clsid) {
  int i;
  int did_moves = 0;
  uint64_t total_bytes = 0;
  unsigned int chunks_perslab = 0;
  //unsigned int chunks_free = 0;
  /* TODO: if free_chunks below high watermark, increase aggressiveness */
  slabs_available_chunks(slabs_clsid, NULL,
      &total_bytes, &chunks_perslab);
  rel_time_t cold_age = 0;
  rel_time_t hot_age = 0;
  rel_time_t warm_age = 0;
  /* If LRU is in flat mode, force items to drain into COLD via max age */
  pthread_mutex_lock(&lru_locks[slabs_clsid|COLD_LRU]);
  if (tails[slabs_clsid|COLD_LRU]) {
    cold_age = current_time - tails[slabs_clsid|COLD_LRU]->time;
  }
  pthread_mutex_unlock(&lru_locks[slabs_clsid|COLD_LRU]);
  hot_age = cold_age * settings.hot_max_factor;
  warm_age = cold_age * settings.warm_max_factor;

  /* Juggle HOT/WARM up to N times */
  for (i = 0; i < 500; i++) {
    int do_more = 0;
    if (lru_pull_tail(slabs_clsid, HOT_LRU, total_bytes, LRU_PULL_CRAWL_BLOCKS, hot_age, NULL) ||
        lru_pull_tail(slabs_clsid, WARM_LRU, total_bytes, LRU_PULL_CRAWL_BLOCKS, warm_age, NULL)) {
      do_more++;
    }
    do_more += lru_pull_tail(slabs_clsid, COLD_LRU, total_bytes, LRU_PULL_CRAWL_BLOCKS, 0, NULL);
    if (do_more == 0)
      break;
    did_moves++;
  }
  return did_moves;
}

/* Will crawl all slab classes a minimum of once per hour */
#define MAX_MAINTCRAWL_WAIT 60 * 60

/* Hoping user input will improve this function. This is all a wild guess.
 * Operation: Kicks crawler for each slab id. Crawlers take some statistics as
 * to items with nonzero expirations. It then buckets how many items will
 * expire per minute for the next hour.
 * This function checks the results of a run, and if it things more than 1% of
 * expirable objects are ready to go, kick the crawler again to reap.
 * It will also kick the crawler once per minute regardless, waiting a minute
 * longer for each time it has no work to do, up to an hour wait time.
 * The latter is to avoid newly started daemons from waiting too long before
 * retrying a crawl.
 */
static void lru_maintainer_crawler_check(crawler_expired_data *cdata) {
  int i;
  static rel_time_t next_crawls[POWER_LARGEST];
  static rel_time_t next_crawl_wait[POWER_LARGEST];
  uint8_t todo[POWER_LARGEST];
  memset(todo, 0, sizeof(uint8_t) * POWER_LARGEST);
  bool do_run = false;
  unsigned int tocrawl_limit = 0;

  // TODO: If not segmented LRU, skip non-cold
  for (i = POWER_SMALLEST; i < POWER_LARGEST; i++) {
    crawlerstats_t *s = &cdata->crawlerstats[i];
    /* We've not successfully kicked off a crawl yet. */
    if (s->run_complete) {
      pthread_mutex_lock(&cdata->lock);
      uint32_t x;
      /* Should we crawl again? */
      uint64_t possible_reclaims = s->seen - s->noexp;
      uint64_t available_reclaims = 0;
      /* Need to think we can free at least 1% of the items before
       * crawling. */
      /* FIXME: Configurable? */
      uint64_t low_watermark = (possible_reclaims / 100) + 1;
      /* Don't bother if the payoff is too low. */
      for (x = 0; x < 60; x++) {
        available_reclaims += s->histo[x];
        if (available_reclaims > low_watermark) {
          if (next_crawl_wait[i] < (x * 60)) {
            next_crawl_wait[i] += 60;
          } else if (next_crawl_wait[i] >= 60) {
            next_crawl_wait[i] -= 60;
          }
          break;
        }
      }

      if (available_reclaims == 0) {
        next_crawl_wait[i] += 60;
      }

      if (next_crawl_wait[i] > MAX_MAINTCRAWL_WAIT) {
        next_crawl_wait[i] = MAX_MAINTCRAWL_WAIT;
      }

      next_crawls[i] = current_time + next_crawl_wait[i] + 5;
      // Got our calculation, avoid running until next actual run.
      s->run_complete = false;
      pthread_mutex_unlock(&cdata->lock);
    }
    if (current_time > next_crawls[i]) {
      pthread_mutex_lock(&lru_locks[i]);
      if (sizes[i] > tocrawl_limit) {
        tocrawl_limit = sizes[i];
      }
      pthread_mutex_unlock(&lru_locks[i]);
      todo[i] = 1;
      do_run = true;
      next_crawls[i] = current_time + 5; // minimum retry wait.
    }
  }
  if (do_run) {
    if (settings.lru_crawler_tocrawl && settings.lru_crawler_tocrawl < tocrawl_limit) {
      tocrawl_limit = settings.lru_crawler_tocrawl;
    }
    lru_crawler_start(todo, tocrawl_limit, CRAWLER_AUTOEXPIRE, cdata, NULL, 0);
  }
}

slab_automove_reg_t slab_automove_default = {
  .init = slab_automove_init,
  .free = slab_automove_free,
  .run = slab_automove_run
};
static pthread_t lru_maintainer_tid;

#define MAX_LRU_MAINTAINER_SLEEP 1000000
#define MIN_LRU_MAINTAINER_SLEEP 1000

static void *lru_maintainer_thread(void *arg) {
  slab_automove_reg_t *sam = &slab_automove_default;
  int i;
  useconds_t to_sleep = MIN_LRU_MAINTAINER_SLEEP;
  useconds_t last_sleep = MIN_LRU_MAINTAINER_SLEEP;
  rel_time_t last_crawler_check = 0;
  rel_time_t last_automove_check = 0;
  useconds_t next_juggles[MAX_NUMBER_OF_SLAB_CLASSES] = {0};
  useconds_t backoff_juggles[MAX_NUMBER_OF_SLAB_CLASSES] = {0};
  crawler_expired_data *cdata =
    (crawler_expired_data*)calloc(1, sizeof(crawler_expired_data));
  if (cdata == NULL) {
    fprintf(stderr, "Failed to allocate crawler data for LRU maintainer thread\n");
    abort();
  }
  pthread_mutex_init(&cdata->lock, NULL);
  cdata->crawl_complete = true; // kick off the crawler.

  double last_ratio = settings.slab_automove_ratio;
  void *am = sam->init(&settings);

  pthread_mutex_lock(&lru_maintainer_lock);
  // LRU maintainer background thread
  while (do_run_lru_maintainer_thread) {
    pthread_mutex_unlock(&lru_maintainer_lock);
    if (to_sleep)
      usleep(to_sleep);
    pthread_mutex_lock(&lru_maintainer_lock);
    /* A sleep of zero counts as a minimum of a 1ms wait */
    last_sleep = to_sleep > 1000 ? to_sleep : 1000;
    to_sleep = MAX_LRU_MAINTAINER_SLEEP;

    STATS_LOCK();
    stats.lru_maintainer_juggles++;
    STATS_UNLOCK();

    /* Each slab class gets its own sleep to avoid hammering locks */
    for (i = POWER_SMALLEST; i < MAX_NUMBER_OF_SLAB_CLASSES; i++) {
      next_juggles[i] = next_juggles[i] > last_sleep ? next_juggles[i] - last_sleep : 0;

      if (next_juggles[i] > 0) {
        // Sleep the thread just for the minimum amount (or not at all)
        if (next_juggles[i] < to_sleep)
          to_sleep = next_juggles[i];
        continue;
      }

      int did_moves = lru_maintainer_juggle(i);
      if (did_moves == 0) {
        if (backoff_juggles[i] != 0) {
          backoff_juggles[i] += backoff_juggles[i] / 8;
        } else {
          backoff_juggles[i] = MIN_LRU_MAINTAINER_SLEEP;
        }
        if (backoff_juggles[i] > MAX_LRU_MAINTAINER_SLEEP)
          backoff_juggles[i] = MAX_LRU_MAINTAINER_SLEEP;
      } else if (backoff_juggles[i] > 0) {
        backoff_juggles[i] /= 2;
        if (backoff_juggles[i] < MIN_LRU_MAINTAINER_SLEEP) {
          backoff_juggles[i] = 0;
        }
      }
      next_juggles[i] = backoff_juggles[i];
      if (next_juggles[i] < to_sleep)
        to_sleep = next_juggles[i];
    }

    /* Minimize the sleep if we had async LRU bumps to process */
    if (lru_maintainer_bumps() && to_sleep > 1000) {
      to_sleep = 1000;
    }

    /* Once per second at most */
    if (settings.lru_crawler && last_crawler_check != current_time) {
      lru_maintainer_crawler_check(cdata);
      last_crawler_check = current_time;
    }

    if (settings.slab_automove == 1 && last_automove_check != current_time) {
      if (last_ratio != settings.slab_automove_ratio) {
        sam->free(am);
        am = sam->init(&settings);
        last_ratio = settings.slab_automove_ratio;
      }
      int src, dst;
      sam->run(am, &src, &dst);
      if (src != -1 && dst != -1) {
        slabs_reassign(src, dst);
      }
      // dst == 0 means reclaim to global pool, be more aggressive
      if (dst != 0) {
        last_automove_check = current_time;
      } else if (dst == 0) {
        // also ensure we minimize the thread sleep
        to_sleep = 1000;
      }
    }
  }
  pthread_mutex_unlock(&lru_maintainer_lock);
  sam->free(am);
  free(cdata);
  // LRU maintainer thread stopping
  return NULL;
}

int stop_lru_maintainer_thread(void) {
  int ret;
  pthread_mutex_lock(&lru_maintainer_lock);
  /* LRU thread is a sleep loop, will die on its own */
  do_run_lru_maintainer_thread = 0;
  pthread_mutex_unlock(&lru_maintainer_lock);
  if ((ret = pthread_join(lru_maintainer_tid, NULL)) != 0) {
    fprintf(stderr, "Failed to stop LRU maintainer thread: %s\n", strerror(ret));
    return -1;
  }
  settings.lru_maintainer_thread = false;
  return 0;
}

int start_lru_maintainer_thread(void *arg) {
  int ret;

  pthread_mutex_lock(&lru_maintainer_lock);
  do_run_lru_maintainer_thread = 1;
  settings.lru_maintainer_thread = true;
  if ((ret = pthread_create(&lru_maintainer_tid, NULL,
          lru_maintainer_thread, arg)) != 0) {
    fprintf(stderr, "Can't create LRU maintainer thread: %s\n",
        strerror(ret));
    pthread_mutex_unlock(&lru_maintainer_lock);
    return -1;
  }
  pthread_mutex_unlock(&lru_maintainer_lock);

  return 0;
}

/* If we hold this lock, crawler can't wake up or move */
void lru_maintainer_pause(void) {
  pthread_mutex_lock(&lru_maintainer_lock);
}

void lru_maintainer_resume(void) {
  pthread_mutex_unlock(&lru_maintainer_lock);
}

int init_lru_maintainer(void) {
  if (lru_maintainer_initialized == 0) {
    pthread_mutex_init(&lru_maintainer_lock, NULL);
    lru_maintainer_initialized = 1;
  }
  return 0;
}

/* Tail linkers and crawler for the LRU crawler. */
void do_item_linktail_q(item *it) { /* item is the new tail */
  pptr<item> *head, *tail;
  assert(it->it_flags == 1);
  assert(it->nbytes == 0);

  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];
  //assert(*tail != 0);
  assert(it != *tail);
  assert((*head && *tail) || (*head == 0 && *tail == 0));
  it->prev = *tail;
  it->next = 0;
  if (it->prev != nullptr) {
    assert(it->prev->next == 0);
    it->prev->next = it;
  }
  *tail = it;
  if (*head == 0) *head = it;
  return;
}

void do_item_unlinktail_q(item *it) {
  pptr<item> *head, *tail;
  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];

  if (*head == it) {
    assert(it->prev == 0);
    *head = it->next;
  }
  if (*tail == it) {
    assert(it->next == 0);
    *tail = it->prev;
  }
  assert(it->next != it);
  assert(it->prev != it);

  if (it->next != nullptr) it->next->prev = it->prev;
  if (it->prev != nullptr) it->prev->next = it->next;
  return;
}

/* This is too convoluted, but it's a difficult shuffle. Try to rewrite it
 * more clearly. */
item *do_item_crawl_q(item *it) {
  pptr<item> *head, *tail;
  assert(it->it_flags == 1);
  assert(it->nbytes == 0);
  head = &heads[it->slabs_clsid];
  tail = &tails[it->slabs_clsid];

  /* We've hit the head, pop off */
  if (it->prev == nullptr) {
    assert(*head == it);
    if (it->next != nullptr) {
      *head = it->next;
      assert(it->next->prev == it);
      it->next->prev = 0;
    }
    return NULL; /* Done */
  }

  /* Swing ourselves in front of the next item */
  /* NB: If there is a prev, we can't be the head */
  assert(it->prev != it);
  if (it->prev != nullptr) {
    if (*head == it->prev) {
      /* Prev was the head, now we're the head */
      *head = it;
    }
    if (*tail == it) {
      /* We are the tail, now they are the tail */
      *tail = it->prev;
    }
    assert(it->next != it);
    if (it->next != nullptr) {
      assert(it->prev->next == it);
      it->prev->next = it->next;
      it->next->prev = it->prev;
    } else {
      /* Tail. Move this above? */
      it->prev->next = 0;
    }
    /* prev->prev's next is it->prev */
    it->next = it->prev;
    it->prev = it->next->prev;
    it->next->prev = it;
    /* New it->prev now, if we're not at the head. */
    if (it->prev != nullptr) {
      it->prev->next = it;
    }
  }
  assert(it->next != it);
  assert(it->prev != it);

  return it->next; /* success */
}
