/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Slabs memory allocation, based on powers-of-N. Slabs are up to 1MB in size
 * and are divided into chunks. The chunk sizes start off at the size of the
 * "item" structure plus space for a small key and value. They increase by
 * a multiplier factor from there, up to half the maximum slab size. The last
 * slab size is always 1MB, since that's the maximum item size allowed by the
 * memcached protocol.
 */
#include "memcached.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>

//#define DEBUG_SLAB_MOVER
/* powers-of-N allocation structures */

struct slabclass_t{
  unsigned int size;      /* sizes of items */
  unsigned int perslab;   /* how many items per slab */

  void *slots;           /* list of item ptrs */
  unsigned int sl_curr;   /* total free items in list */

  unsigned int slabs;     /* how many slabs were allocated for this class */

  void **slab_list;       /* array of slab pointers */
  unsigned int list_size; /* size of prev array */

  size_t requested; /* The number of requested bytes */
};

// CHRIS NOTES:
//    slabclass[0] is a special slabclass to store reassignable pages...
//    Refer to memcached.h:104
static pptr<slabclass_t> slabclass;// [MAX_NUMBER_OF_SLAB_CLASSES];
static size_t mem_limit = 0;
static size_t mem_malloced = 0;
/* If the memory limit has been hit once. Used as a hint to decide when to
 * early-wake the LRU maintenance thread */
static bool mem_limit_reached = false;
static int power_largest;

static void *mem_current = NULL;
static size_t mem_avail = 0;
/**
 * Access to the slab allocator is protected by this lock
 */
static pthread_mutex_t slabs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t slabs_rebalance_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Forward Declarations
 */
static int grow_slab_list (const unsigned int id);
static int do_slabs_newslab(const unsigned int id);
static void *memory_allocate(size_t size);
static void do_slabs_free(void *ptr, const size_t size, unsigned int id);

/*
 * Figures out which slab class (chunk size) is required to store an item of
 * a given size.
 *
 * Given object size, return id to use when allocating/freeing memory for object
 * 0 means error: can't store such a large object
 */

unsigned int slabs_clsid(const size_t size) {
  int res = POWER_SMALLEST;

  if (size == 0){
    printf("size is 0: %ld\n", size);
    return 0;
  }
  if (size > (size_t)settings.item_size_max){
    printf("size is too big: %ld > %d\n", size, settings.item_size_max);
    return 0;
  }
  while (size > slabclass[res].size)
    if (res++ == power_largest)     /* won't fit in the biggest slab */
      return power_largest;
  return res;
}

/**
 * Determines the chunk sizes and initializes the slab class descriptors
 * accordingly.
 * limit      - amount of memory we allocate in total?
 * factor     - Growth ratio in between slabs. size(slab[k+1])/size(slab[k]).
 */
void slabs_init(const size_t limit, const double factor) {
  int i = POWER_SMALLEST - 1;
  unsigned int size = sizeof(item) + settings.chunk_size;
  // set global var mem_limit
  mem_limit = limit;
  // CHRIS - we used to have a statically allocated set of slabclasses. Make
  // them dynamically allocated so that we can make them persistent
  if (am_server && !is_restart){
    slabclass = pptr<slabclass_t>((slabclass_t*)
        RP_malloc(sizeof(slabclass_t)*MAX_NUMBER_OF_SLAB_CLASSES));
    memset((slabclass_t*)slabclass, 0,
        sizeof(slabclass_t)*MAX_NUMBER_OF_SLAB_CLASSES);
    while (++i < MAX_NUMBER_OF_SLAB_CLASSES-1) {
      /* Make sure items are always n-byte aligned */
      // CHRIS - 8 byte alignment
      if (size % CHUNK_ALIGN_BYTES)
        size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);

      slabclass[i].size = size;
      slabclass[i].perslab = settings.slab_page_size / slabclass[i].size;
      size *= factor;
    }

    power_largest = i;
    slabclass[power_largest].size = settings.slab_chunk_size_max;
    slabclass[power_largest].perslab =
      settings.slab_page_size / settings.slab_chunk_size_max;
  } else {
    // you are not the server or this is a restart
    slabclass = pptr<slabclass_t>(
        (slabclass_t*)RP_get_root(RPMRoot::SlabclassAr));
    power_largest = MAX_NUMBER_OF_SLAB_CLASSES - 1;
  }
}

static int grow_slab_list (const unsigned int id) {
  slabclass_t *p = &slabclass[id];
  if (p->slabs == p->list_size) {
    size_t new_size =  (p->list_size != 0) ? p->list_size * 2 : 16;
    void *new_list = realloc(p->slab_list, new_size * sizeof(void *));
    if (new_list == 0) return 0;
    p->list_size = new_size;
    p->slab_list = (void**)new_list;
  }
  return 1;
}

static void split_slab_page_into_freelist(char *ptr, const unsigned int id) {
  slabclass_t *p = &slabclass[id];
  unsigned int x;
  for (x = 0; x < p->perslab; x++) {
    do_slabs_free(ptr, 0, id);
    ptr += p->size;
  }
}

/* Fast FIFO queue */
static void *get_page_from_global_pool(void) {
  slabclass_t *p = &slabclass[SLAB_GLOBAL_PAGE_POOL];
  // we have no slabs to reassign...
  if (p->slabs < 1) {
    return NULL;
  }
  // We have a slab of some size. Reassign it from the global pool and decrement
  // the number of slabs that live in slabclass[0].
  char *ret = (char*)p->slab_list[p->slabs - 1];
  p->slabs--;
  return ret;
}

static int do_slabs_newslab(const unsigned int id) {
  slabclass_t *p = &slabclass[id];
  slabclass_t *g = &slabclass[SLAB_GLOBAL_PAGE_POOL];
  int len = (settings.slab_reassign || settings.slab_chunk_size_max != settings.slab_page_size)
    ? settings.slab_page_size
    : p->size * p->perslab;
  char *ptr;

  if ((mem_limit && mem_malloced + len > mem_limit && p->slabs > 0
        && g->slabs == 0)) {
    mem_limit_reached = true;
    return 0;
  }

  if ((grow_slab_list(id) == 0) ||
      (((ptr = (char*)get_page_from_global_pool()) == NULL) &&
       ((ptr = (char*)memory_allocate((size_t)len)) == 0))) {
    return 0;
  }

  memset(ptr, 0, (size_t)len);
  split_slab_page_into_freelist(ptr, id);

  p->slab_list[p->slabs++] = ptr;
  return 1;
}
/* size         - amt of memory requested
 * id           - size class requested memory from
 * total_bytes  - pointer to local variable to return how much memory allocated
 * flags        - flags, for our reduced memcached, only the flag
 *                SLABS_ALLOC_NO_NEWPAGE is used or no flags
 */
static void *do_slabs_alloc(const size_t size, unsigned int id, uint64_t *total_bytes,
    unsigned int flags) {
  slabclass_t *SCp;
  void *ret = NULL;
  pptr<item> it = NULL;
  if (id < POWER_SMALLEST || id > (unsigned int)power_largest) {
    return NULL;
  }
  p = &slabclass[id];
  assert(SCp->sl_curr == 0 || (pptr<item>((item*)(SCp->slots)))->slabs_clsid == 0);
  if (total_bytes != NULL) {
    *total_bytes = SCp->requested;
  }

  assert(size <= SCp->size);
  /* fail unless we have space at the end of a recently allocated page,
     we have something on our freelist, or we could allocate a new page */
  if (SCp->sl_curr == 0 && flags != SLABS_ALLOC_NO_NEWPAGE) {
    fflush(stdout);
    do_slabs_newslab(id);
  }

  if (SCp->sl_curr != 0) {
    /* return off our freelist */
    it = pptr<item>((item*)(SCp->slots));
    SCp->slots = it->next;
    if (it->next != nullptr) it->next->prev = 0;
    /* Kill flag and initialize refcount here for lock safety in slab
     * mover's freeness detection. */
    it->it_flags &= ~ITEM_SLABBED;
    it->refcount = 1;
    SCp->sl_curr--;
    ret = (void *)it;
  } else {
    ret = NULL;
  }

  if (ret) {
    SCp->requested += size;
  }
  return ret;
}

static void do_slabs_free_chunked(pptr<item> it, const size_t size) {
  item_chunk *chunk = (item_chunk *) ITEM_schunk(it);
  slabclass_t *p;

  it->it_flags = ITEM_SLABBED;
  it->slabs_clsid = 0;
  it->prev = 0;
  // header object's original classid is stored in chunk.
  p = &slabclass[chunk->orig_clsid];
  if (chunk->next) {
    chunk = chunk->next;
    chunk->prev = 0;
  } else {
    // header with no attached chunk
    chunk = NULL;
  }

  // return the header object.
  // TODO: This is in three places, here and in do_slabs_free().
  it->prev = 0;
  it->next = pptr<item>((item*)p->slots);
  if (it->next != nullptr) it->next->prev = it;
  p->slots = it;
  p->sl_curr++;
  p->requested -= it->nkey + 1 + it->nsuffix + sizeof(item) + sizeof(item_chunk);
  p->requested -= sizeof(uint64_t);

  item_chunk *next_chunk;
  while (chunk) {
    assert(chunk->it_flags == ITEM_CHUNK);
    chunk->it_flags = ITEM_SLABBED;
    p = &slabclass[chunk->slabs_clsid];
    chunk->slabs_clsid = 0;
    next_chunk = chunk->next;

    chunk->prev = 0;
    chunk->next = (item_chunk*)p->slots;
    if (chunk->next) chunk->next->prev = chunk;
    p->slots = chunk;
    p->sl_curr++;
    p->requested -= chunk->size + sizeof(item_chunk);

    chunk = next_chunk;
  }

  return;
}


static void do_slabs_free(void *ptr, const size_t size, unsigned int id) {
  slabclass_t *p;
  pptr<item> it;

  assert(id >= POWER_SMALLEST && id <= (unsigned int)power_largest);
  if (id < POWER_SMALLEST || id > (unsigned int)power_largest)
    return;

  p = &slabclass[id];

  it = pptr<item>((item*)ptr);
  if ((it->it_flags & ITEM_CHUNKED) == 0) {
    it->it_flags = ITEM_SLABBED;
    it->slabs_clsid = 0;
    it->prev = 0;
    it->next = pptr<item>((item*)(p->slots));
    if (it->next != nullptr) it->next->prev = it;
    p->slots = it;

    p->sl_curr++;
    p->requested -= size;
  } else {
    do_slabs_free_chunked(it, size);
  }
  return;
}

/* With refactoring of the various stats code the automover won't need a
 * custom function here.
 */
void fill_slab_stats_automove(slab_stats_automove *am) {
  int n;
  pthread_mutex_lock(&slabs_lock);
  for (n = 0; n < MAX_NUMBER_OF_SLAB_CLASSES; n++) {
    slabclass_t *p = &slabclass[n];
    slab_stats_automove *cur = &am[n];
    cur->chunks_per_page = p->perslab;
    cur->free_chunks = p->sl_curr;
    cur->total_pages = p->slabs;
    cur->chunk_size = p->size;
  }
  pthread_mutex_unlock(&slabs_lock);
}

/* TODO: slabs_available_chunks should grow up to encompass this.
 * mem_flag is redundant with the other function.
 */
unsigned int global_page_pool_size(bool *mem_flag) {
  unsigned int ret = 0;
  pthread_mutex_lock(&slabs_lock);
  if (mem_flag != NULL)
    *mem_flag = mem_malloced >= mem_limit ? true : false;
  ret = slabclass[SLAB_GLOBAL_PAGE_POOL].slabs;
  pthread_mutex_unlock(&slabs_lock);
  return ret;
}

static void *memory_allocate(size_t size) {
  /* We are not using a preallocated large memory chunk */
  void *ret = RP_malloc(size);
  mem_malloced += size;
  return ret;
}

/* Must only be used if all pages are item_size_max */
static void memory_release() {
  void *p = NULL;
  while (mem_malloced > mem_limit &&
      (p = get_page_from_global_pool()) != NULL) {
    RP_free(p);
    mem_malloced -= settings.slab_page_size;
  }
}

void *slabs_alloc(size_t size, unsigned int id, uint64_t *total_bytes,
    unsigned int flags) {
  void *ret;
  pthread_mutex_lock(&slabs_lock);
  ret = do_slabs_alloc(size, id, total_bytes, flags);
  pthread_mutex_unlock(&slabs_lock);
  return ret;
}

void slabs_free(void *ptr, size_t size, unsigned int id) {
  pthread_mutex_lock(&slabs_lock);
  do_slabs_free(ptr, size, id);
  pthread_mutex_unlock(&slabs_lock);
}

unsigned int slabs_available_chunks(const unsigned int id, bool *mem_flag,
    uint64_t *total_bytes, unsigned int *chunks_perslab) {
  unsigned int ret;
  slabclass_t *p;

  pthread_mutex_lock(&slabs_lock);
  p = &slabclass[id];
  ret = p->sl_curr;
  if (mem_flag != NULL)
    *mem_flag = mem_malloced >= mem_limit ? true : false;
  if (total_bytes != NULL)
    *total_bytes = p->requested;
  if (chunks_perslab != NULL)
    *chunks_perslab = p->perslab;
  pthread_mutex_unlock(&slabs_lock);
  return ret;
}

/* The slabber system could avoid needing to understand much, if anything,
 * about items if callbacks were strategically used. Due to how the slab mover
 * works, certain flag bits can only be adjusted while holding the slabs lock.
 * Using these functions, isolate sections of code needing this and turn them
 * into callbacks when an interface becomes more obvious.
 */
void slabs_mlock(void) {
  pthread_mutex_lock(&slabs_lock);
}

void slabs_munlock(void) {
  pthread_mutex_unlock(&slabs_lock);
}

static pthread_cond_t slab_rebalance_cond = PTHREAD_COND_INITIALIZER;
static volatile int do_run_slab_thread = 1;
static volatile int do_run_slab_rebalance_thread = 1;

#define DEFAULT_SLAB_BULK_CHECK 1
int slab_bulk_check = DEFAULT_SLAB_BULK_CHECK;

static int slab_rebalance_start(void) {
  slabclass_t *s_cls;
  int no_go = 0;

  pthread_mutex_lock(&slabs_lock);

  if (slab_rebal.s_clsid < SLAB_GLOBAL_PAGE_POOL ||
      slab_rebal.s_clsid > power_largest  ||
      slab_rebal.d_clsid < SLAB_GLOBAL_PAGE_POOL ||
      slab_rebal.d_clsid > power_largest  ||
      slab_rebal.s_clsid == slab_rebal.d_clsid)
    no_go = -2;

  s_cls = &slabclass[slab_rebal.s_clsid];

  if (!grow_slab_list(slab_rebal.d_clsid)) {
    no_go = -1;
  }

  if (s_cls->slabs < 2)
    no_go = -3;

  if (no_go != 0) {
    pthread_mutex_unlock(&slabs_lock);
    return no_go; /* Should use a wrapper function... */
  }

  /* Always kill the first available slab page as it is most likely to
   * contain the oldest items
   */
  slab_rebal.slab_start = s_cls->slab_list[0];
  slab_rebal.slab_end   = (char *)slab_rebal.slab_start +
    (s_cls->size * s_cls->perslab);
  slab_rebal.slab_pos   = slab_rebal.slab_start;
  slab_rebal.done       = 0;
  // Don't need to do chunk move work if page is in global pool.
  if (slab_rebal.s_clsid == SLAB_GLOBAL_PAGE_POOL) {
    slab_rebal.done = 1;
  }

  slab_rebalance_signal = 2;

  // Started a slab rebalance
  pthread_mutex_unlock(&slabs_lock);

  STATS_LOCK();
  stats_state.slab_reassign_running = true;
  STATS_UNLOCK();

  return 0;
}

/* CALLED WITH slabs_lock HELD */
static void *slab_rebalance_alloc(const size_t size, unsigned int id) {
  slabclass_t *s_cls;
  s_cls = &slabclass[slab_rebal.s_clsid];
  unsigned int x;
  pptr<item> new_it = NULL;

  for (x = 0; x < s_cls->perslab; x++) {
    new_it = (item*)do_slabs_alloc(size, id, NULL, SLABS_ALLOC_NO_NEWPAGE);
    /* check that memory isn't within the range to clear */
    if (new_it == NULL) {
      break;
    }
    if ((void *)new_it >= slab_rebal.slab_start
        && (void *)new_it < slab_rebal.slab_end) {
      /* Pulled something we intend to free. Mark it as freed since
       * we've already done the work of unlinking it from the freelist.
       */
      s_cls->requested -= size;
      new_it->refcount = 0;
      new_it->it_flags = ITEM_SLABBED|ITEM_FETCHED;
#ifdef DEBUG_SLAB_MOVER
      memcpy(ITEM_key(new_it), "deadbeef", 8);
#endif
      new_it = NULL;
      slab_rebal.inline_reclaim++;
    } else {
      break;
    }
  }
  return new_it;
}

/* CALLED WITH slabs_lock HELD */
/* detaches item/chunk from freelist. */
static void slab_rebalance_cut_free(slabclass_t *s_cls, pptr<item> it) {
  /* Ensure this was on the freelist and nothing else. */
  assert(it->it_flags == ITEM_SLABBED);
  if (s_cls->slots == it) {
    s_cls->slots = it->next;
  }
  if (it->next != nullptr) it->next->prev = it->prev;
  if (it->prev != nullptr) it->prev->next = it->next;
  s_cls->sl_curr--;
}

enum move_status {
  MOVE_PASS=0, MOVE_FROM_SLAB, MOVE_FROM_LRU, MOVE_BUSY, MOVE_LOCKED
};

#define SLAB_MOVE_MAX_LOOPS 1000

/* refcount == 0 is safe since nobody can incr while item_lock is held.
 * refcount != 0 is impossible since flags/etc can be modified in other
 * threads. instead, note we found a busy one and bail. logic in do_item_get
 * will prevent busy items from continuing to be busy
 * NOTE: This is checking it_flags outside of an item lock. I believe this
 * works since it_flags is 8 bits, and we're only ever comparing a single bit
 * regardless. ITEM_SLABBED bit will always be correct since we're holding the
 * lock which modifies that bit. ITEM_LINKED won't exist if we're between an
 * item having ITEM_SLABBED removed, and the key hasn't been added to the item
 * yet. The memory barrier from the slabs lock should order the key write and the
 * flags to the item?
 * If ITEM_LINKED did exist and was just removed, but we still see it, that's
 * still safe since it will have a valid key, which we then lock, and then
 * recheck everything.
 * This may not be safe on all platforms; If not, slabs_alloc() will need to
 * seed the item key while holding slabs_lock.
 */
static int slab_rebalance_move(void) {
  slabclass_t *s_cls;
  int x;
  int was_busy = 0;
  int refcount = 0;
  uint32_t hv;
  void *hold_lock;
  enum move_status status = MOVE_PASS;

  pthread_mutex_lock(&slabs_lock);

  s_cls = &slabclass[slab_rebal.s_clsid];

  for (x = 0; x < slab_bulk_check; x++) {
    hv = 0;
    hold_lock = NULL;
    pptr<item> it = (item*)slab_rebal.slab_pos;
    item_chunk *ch = NULL;
    status = MOVE_PASS;
    if (it->it_flags & ITEM_CHUNK) {
      /* This chunk is a chained part of a larger item. */
      ch = (item_chunk *)(&*it);
      /* Instead, we use the head chunk to find the item and effectively
       * lock the entire structure. If a chunk has ITEM_CHUNK flag, its
       * head cannot be slabbed, so the normal routine is safe. */
      it = ch->head;
      assert(it->it_flags & ITEM_CHUNKED);
    }

    /* ITEM_FETCHED when ITEM_SLABBED is overloaded to mean we've cleared
     * the chunk for move. Only these two flags should exist.
     */
    if (it->it_flags != (ITEM_SLABBED|ITEM_FETCHED)) {
      /* ITEM_SLABBED can only be added/removed under the slabs_lock */
      if (it->it_flags & ITEM_SLABBED) {
        assert(ch == NULL);
        slab_rebalance_cut_free(s_cls, it);
        status = MOVE_FROM_SLAB;
      } else if ((it->it_flags & ITEM_LINKED) != 0) {
        /* If it doesn't have ITEM_SLABBED, the item could be in any
         * state on its way to being freed or written to. If no
         * ITEM_SLABBED, but it's had ITEM_LINKED, it must be active
         * and have the key written to it already.
         */
        hv = tcd_hash(ITEM_key(it), it->nkey);
        if ((hold_lock = item_trylock(hv)) == NULL) {
          status = MOVE_LOCKED;
        } else {
          bool is_linked = (it->it_flags & ITEM_LINKED);
          refcount = refcount_incr(it);
          if (refcount == 2) { /* item is linked but not busy */
            /* Double check ITEM_LINKED flag here, since we're
             * past a memory barrier from the mutex. */
            if (is_linked) {
              status = MOVE_FROM_LRU;
            } else {
              /* refcount == 1 + !ITEM_LINKED means the item is being
               * uploaded to, or was just unlinked but hasn't been freed
               * yet. Let it bleed off on its own and try again later */
              status = MOVE_BUSY;
            }
          } else if (refcount > 2 && is_linked) {
            // TODO: Mark items for delete/rescue and process
            // outside of the main loop.
            if (slab_rebal.busy_loops > SLAB_MOVE_MAX_LOOPS) {
              slab_rebal.busy_deletes++;
              // Only safe to hold slabs lock because refcount
              // can't drop to 0 until we release item lock.
              pthread_mutex_unlock(&slabs_lock);
              do_item_unlink(it, hv);
              pthread_mutex_lock(&slabs_lock);
            }
            status = MOVE_BUSY;
          } else {
            // Slab reassign hit a busy item
            status = MOVE_BUSY;
          }
          /* Item lock must be held while modifying refcount */
          if (status == MOVE_BUSY) {
            refcount_decr(it);
            item_trylock_unlock(hold_lock);
          }
        }
      } else {
        /* See above comment. No ITEM_SLABBED or ITEM_LINKED. Mark
         * busy and wait for item to complete its upload. */
        status = MOVE_BUSY;
      }
    }

    int save_item = 0;
    pptr<item> new_it = NULL;
    size_t ntotal = 0;
    unsigned int requested_adjust;
    switch (status) {
      case MOVE_FROM_LRU:
        /* Lock order is LRU locks -> slabs_lock. unlink uses LRU lock.
         * We only need to hold the slabs_lock while initially looking
         * at an item, and at this point we have an exclusive refcount
         * (2) + the item is locked. Drop slabs lock, drop item to
         * refcount 1 (just our own, then fall through and wipe it
         */
        /* Check if expired or flushed */
        ntotal = ITEM_ntotal(it);
        /* REQUIRES slabs_lock: CHECK FOR cls->sl_curr > 0 */
        if (ch == NULL && (it->it_flags & ITEM_CHUNKED)) {
          /* Chunked should be identical to non-chunked, except we need
           * to swap out ntotal for the head-chunk-total. */
          ntotal = s_cls->size;
        }
        if ((it->exptime != 0 && it->exptime < current_time)
            || item_is_flushed(it)) {
          /* Expired, don't save. */
          save_item = 0;
        } else if (ch == NULL &&
            (new_it = (item*)slab_rebalance_alloc(ntotal, slab_rebal.s_clsid)) == NULL) {
          /* Not a chunk of an item, and nomem. */
          save_item = 0;
          slab_rebal.evictions_nomem++;
        } else if (ch != NULL &&
            (new_it = (item*)slab_rebalance_alloc(s_cls->size, slab_rebal.s_clsid)) == NULL) {
          /* Is a chunk of an item, and nomem. */
          save_item = 0;
          slab_rebal.evictions_nomem++;
        } else {
          /* Was whatever it was, and we have memory for it. */
          save_item = 1;
        }
        pthread_mutex_unlock(&slabs_lock);
        requested_adjust = 0;
        if (save_item) {
          if (ch == NULL) {
            assert((new_it->it_flags & ITEM_CHUNKED) == 0);
            /* if free memory, memcpy. clear prev/next/h_bucket */
            memcpy(new_it, it, ntotal);
            new_it->prev = 0;
            new_it->next = 0;
            new_it->h_next = 0;
            /* These are definitely required. else fails assert */
            new_it->it_flags &= ~ITEM_LINKED;
            new_it->refcount = 0;
            do_item_replace(it, new_it, hv);
            /* Need to walk the chunks and repoint head  */
            if (new_it->it_flags & ITEM_CHUNKED) {
              item_chunk *fch = (item_chunk *) ITEM_schunk(new_it);
              fch->next->prev = fch;
              while (fch) {
                fch->head = new_it;
                fch = fch->next;
              }
            }
            it->refcount = 0;
            it->it_flags = ITEM_SLABBED|ITEM_FETCHED;
#ifdef DEBUG_SLAB_MOVER
            memcpy(ITEM_key(it), "deadbeef", 8);
#endif
            slab_rebal.rescues++;
            requested_adjust = ntotal;
          } else {
            item_chunk *nch = (item_chunk *) (&*new_it);
            /* Chunks always have head chunk (the main it) */
            ch->prev->next = nch;
            if (ch->next)
              ch->next->prev = nch;
            memcpy(nch, ch, ch->used + sizeof(item_chunk));
            ch->refcount = 0;
            ch->it_flags = ITEM_SLABBED|ITEM_FETCHED;
            slab_rebal.chunk_rescues++;
#ifdef DEBUG_SLAB_MOVER
            memcpy(ITEM_key(pptr<item>(ch)), "deadbeef", 8);
#endif
            refcount_decr(it);
            requested_adjust = s_cls->size;
          }
        } else {
          /* restore ntotal in case we tried saving a head chunk. */
          ntotal = ITEM_ntotal(it);
          do_item_unlink(it, hv);
          slabs_free(it, ntotal, slab_rebal.s_clsid);
          /* Swing around again later to remove it from the freelist. */
          slab_rebal.busy_items++;
          was_busy++;
        }
        item_trylock_unlock(hold_lock);
        pthread_mutex_lock(&slabs_lock);
        /* Always remove the ntotal, as we added it in during
         * do_slabs_alloc() when copying the item.
         */
        s_cls->requested -= requested_adjust;
        break;
      case MOVE_FROM_SLAB:
        it->refcount = 0;
        it->it_flags = ITEM_SLABBED|ITEM_FETCHED;
#ifdef DEBUG_SLAB_MOVER
        memcpy(ITEM_key(it), "deadbeef", 8);
#endif
        break;
      case MOVE_BUSY:
      case MOVE_LOCKED:
        slab_rebal.busy_items++;
        was_busy++;
        break;
      case MOVE_PASS:
        break;
    }

    slab_rebal.slab_pos = (char *)slab_rebal.slab_pos + s_cls->size;
    if (slab_rebal.slab_pos >= slab_rebal.slab_end)
      break;
  }

  if (slab_rebal.slab_pos >= slab_rebal.slab_end) {
    /* Some items were busy, start again from the top */
    if (slab_rebal.busy_items) {
      slab_rebal.slab_pos = slab_rebal.slab_start;
      STATS_LOCK();
      stats.slab_reassign_busy_items += slab_rebal.busy_items;
      STATS_UNLOCK();
      slab_rebal.busy_items = 0;
      slab_rebal.busy_loops++;
    } else {
      slab_rebal.done++;
    }
  }

  pthread_mutex_unlock(&slabs_lock);

  return was_busy;
}

static void slab_rebalance_finish(void) {
  slabclass_t *s_cls;
  slabclass_t *d_cls;
  unsigned int x;
  uint32_t rescues;
  uint32_t evictions_nomem;
  uint32_t inline_reclaim;
  uint32_t chunk_rescues;
  uint32_t busy_deletes;

  pthread_mutex_lock(&slabs_lock);

  s_cls = &slabclass[slab_rebal.s_clsid];
  d_cls = &slabclass[slab_rebal.d_clsid];

#ifdef DEBUG_SLAB_MOVER
  /* If the algorithm is broken, live items can sneak in. */
  slab_rebal.slab_pos = slab_rebal.slab_start;
  while (1) {
    pptr<item> it = slab_rebal.slab_pos;
    assert(it->it_flags == (ITEM_SLABBED|ITEM_FETCHED));
    assert(memcmp(ITEM_key(it), "deadbeef", 8) == 0);
    it->it_flags = ITEM_SLABBED|ITEM_FETCHED;
    slab_rebal.slab_pos = (char *)slab_rebal.slab_pos + s_cls->size;
    if (slab_rebal.slab_pos >= slab_rebal.slab_end)
      break;
  }
#endif

  /* At this point the stolen slab is completely clear.
   * We always kill the "first"/"oldest" slab page in the slab_list, so
   * shuffle the page list backwards and decrement.
   */
  s_cls->slabs--;
  for (x = 0; x < s_cls->slabs; x++) {
    s_cls->slab_list[x] = s_cls->slab_list[x+1];
  }

  d_cls->slab_list[d_cls->slabs++] = slab_rebal.slab_start;
  /* Don't need to split the page into chunks if we're just storing it */
  if (slab_rebal.d_clsid > SLAB_GLOBAL_PAGE_POOL) {
    memset(slab_rebal.slab_start, 0, (size_t)settings.slab_page_size);
    split_slab_page_into_freelist((char*)slab_rebal.slab_start,
        slab_rebal.d_clsid);
  } else if (slab_rebal.d_clsid == SLAB_GLOBAL_PAGE_POOL) {
    /* mem_malloc'ed might be higher than mem_limit. */
    mem_limit_reached = false;
    memory_release();
  }

  slab_rebal.busy_loops = 0;
  slab_rebal.done       = 0;
  slab_rebal.s_clsid    = 0;
  slab_rebal.d_clsid    = 0;
  slab_rebal.slab_start = NULL;
  slab_rebal.slab_end   = NULL;
  slab_rebal.slab_pos   = NULL;
  evictions_nomem    = slab_rebal.evictions_nomem;
  inline_reclaim = slab_rebal.inline_reclaim;
  rescues   = slab_rebal.rescues;
  chunk_rescues = slab_rebal.chunk_rescues;
  busy_deletes = slab_rebal.busy_deletes;
  slab_rebal.evictions_nomem    = 0;
  slab_rebal.inline_reclaim = 0;
  slab_rebal.rescues  = 0;
  slab_rebal.chunk_rescues = 0;
  slab_rebal.busy_deletes = 0;

  slab_rebalance_signal = 0;

  pthread_mutex_unlock(&slabs_lock);

  STATS_LOCK();
  stats.slabs_moved++;
  stats.slab_reassign_rescues += rescues;
  stats.slab_reassign_evictions_nomem += evictions_nomem;
  stats.slab_reassign_inline_reclaim += inline_reclaim;
  stats.slab_reassign_chunk_rescues += chunk_rescues;
  stats.slab_reassign_busy_deletes += busy_deletes;
  stats_state.slab_reassign_running = false;
  STATS_UNLOCK();
  // Finished a slab move
}

/* Slab mover thread.
 * Sits waiting for a condition to jump off and shovel some memory about
 */
static void *slab_rebalance_thread(void *arg) {
  int was_busy = 0;
  /* So we first pass into cond_wait with the mutex held */
  mutex_lock(&slabs_rebalance_lock);

  while (do_run_slab_rebalance_thread) {
    if (slab_rebalance_signal == 1) {
      if (slab_rebalance_start() < 0) {
        /* Handle errors with more specificity as required. */
        slab_rebalance_signal = 0;
      }

      was_busy = 0;
    } else if (slab_rebalance_signal && slab_rebal.slab_start != NULL) {
      was_busy = slab_rebalance_move();
    }

    if (slab_rebal.done) {
      slab_rebalance_finish();
    } else if (was_busy) {
      /* Stuck waiting for some items to unlock, so slow down a bit
       * to give them a chance to free up */
      usleep(1000);
    }

    if (slab_rebalance_signal == 0) {
      /* always hold this lock while we're running */
      pthread_cond_wait(&slab_rebalance_cond, &slabs_rebalance_lock);
    }
  }
  return NULL;
}

/* If we hold this lock, rebalancer can't wake up or move */
void slabs_rebalancer_pause(void) {
  pthread_mutex_lock(&slabs_rebalance_lock);
}

void slabs_rebalancer_resume(void) {
  pthread_mutex_unlock(&slabs_rebalance_lock);
}

static pthread_t rebalance_tid;

int start_slab_maintenance_thread(void) {
  int ret;
  slab_rebalance_signal = 0;
  slab_rebal.slab_start = NULL;
  char *env = getenv("MEMCACHED_SLAB_BULK_CHECK");
  if (env != NULL) {
    slab_bulk_check = atoi(env);
    if (slab_bulk_check == 0) {
      slab_bulk_check = DEFAULT_SLAB_BULK_CHECK;
    }
  }

  if (pthread_cond_init(&slab_rebalance_cond, NULL) != 0) {
    fprintf(stderr, "Can't initialize rebalance condition\n");
    return -1;
  }
  pthread_mutex_init(&slabs_rebalance_lock, NULL);

  if ((ret = pthread_create(&rebalance_tid, NULL,
          slab_rebalance_thread, NULL)) != 0) {
    fprintf(stderr, "Can't create rebal thread: %s\n", strerror(ret));
    return -1;
  }
  return 0;
}

/* The maintenance thread is on a sleep/loop cycle, so it should join after a
 * short wait */
void stop_slab_maintenance_thread(void) {
  mutex_lock(&slabs_rebalance_lock);
  do_run_slab_thread = 0;
  do_run_slab_rebalance_thread = 0;
  pthread_cond_signal(&slab_rebalance_cond);
  pthread_mutex_unlock(&slabs_rebalance_lock);

  /* Wait for the maintenance thread to stop */
  pthread_join(rebalance_tid, NULL);
}
