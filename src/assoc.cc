/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Hash table
 *
 * The hash function used here is by Bob Jenkins, 1996:
 *    <http://burtleburtle.net/bob/hash/doobs.html>
 *       "By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.
 *       You may use this code any way you wish, private, educational,
 *       or commercial.  It's free."
 *
 * The rest of the file is licensed under the BSD license.  See LICENSE.
 */

#include "memcached.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <signal.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include <pptr.hpp>


static pthread_cond_t maintenance_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t maintenance_lock = PTHREAD_MUTEX_INITIALIZER;

typedef  unsigned long  int  ub4;   /* unsigned 4-byte quantities */
typedef  unsigned       char ub1;   /* unsigned 1-byte quantities */

/* how many powers of 2's worth of buckets we use */
unsigned int hashpower = HASHPOWER_DEFAULT;

#define hashsize(n) ((ub4)1<<(n))
#define hashmask(n) (hashsize(n)-1)

/* Main hash table. This is where we look except during expansion. */
#define primary_hashtable (*primary_hashtable_storage)
static pptr<pptr<item> > *primary_hashtable_storage = nullptr;

/*
 * Previous hash table. During expansion, we look here for keys that haven't
 * been moved over to the primary yet.
 */
#define old_hashtable (*old_hashtable_storage)
static pptr<pptr<item> > *old_hashtable_storage = nullptr;

/* Flag: Are we in the middle of expanding now? */
static bool expanding = false;
static bool started_expanding = false;

/*
 * During expansion we migrate values with bucket granularity; this is how
 * far we've gotten so far. Ranges from 0 .. hashsize(hashpower - 1) - 1.
 */
static unsigned int expand_bucket = 0;

void assoc_init(const int hashtable_init) {
  if (hashtable_init) {
    hashpower = hashtable_init;
  }
  if (!is_restart){
    primary_hashtable_storage = (pptr<pptr<item>>*)RP_malloc(sizeof(pptr<pptr<item> >));
    assert(primary_hashtable_storage != nullptr);

    primary_hashtable = pptr<pptr<item> > ((pptr<item>*)RP_calloc(hashsize(hashpower), sizeof(pptr<item>)));
    assert(primary_hashtable != nullptr);

    RP_set_root(primary_hashtable_storage, RPMRoot::PrimaryHT);
    RP_set_root(nullptr, RPMRoot::OldHT);
    
    for(unsigned int i = 0; i < hashsize(hashpower); ++i){
      primary_hashtable[i] = pptr<item>(nullptr);
    }
  } else {
    primary_hashtable_storage = (pptr<pptr<item> >*)RP_get_root<pptr<pptr<item> > >(RPMRoot::PrimaryHT);
    old_hashtable_storage =     (pptr<pptr<item> >*)RP_get_root<pptr<pptr<item> > >(RPMRoot::OldHT);
  }

  STATS_LOCK();
  stats_state.hash_power_level = hashpower;
  stats_state.hash_bytes = hashsize(hashpower) * sizeof(void *);
  STATS_UNLOCK();
}

item *assoc_find(const char *key, const size_t nkey, const uint32_t hv) {
  pptr<item> it;
  unsigned int oldbucket;

  if (expanding &&
      (oldbucket = (hv & hashmask(hashpower - 1))) >= expand_bucket)
  {
    it = old_hashtable[oldbucket];
  } else {
    // UNDO this
    size_t ind = hv & hashmask(hashpower);
    it = primary_hashtable[ind];
  }

  item *ret = NULL;
  int depth = 0;
  while (it != nullptr) {
    if ((nkey == it->nkey) && (memcmp(key, ITEM_key(it), nkey) == 0)) {
      ret = it;
      break;
    }
    it = it->h_next;
    ++depth;
  }
  return ret;
}

/* returns the address of the item pointer before the key.  if *item == 0,
   the item wasn't found */
static pptr<item>* _hashitem_before (const char *key, const size_t nkey, const uint32_t hv) {
  pptr<item> *pos;
  unsigned int oldbucket;

  if (expanding &&
      (oldbucket = (hv & hashmask(hashpower - 1))) >= expand_bucket)
  {
    pos = &old_hashtable[oldbucket];
  } else {
    pos = &primary_hashtable[hv & hashmask(hashpower)];
  }

  // CHRIS TODO 
  while ((*pos != nullptr) && ((nkey != (*pos)->nkey) || memcmp(key, ITEM_key(*pos), nkey))) {
    pos = &(*pos)->h_next;
  }
  return pos;
}

/* grows the hashtable to the next power of 2. */
static void assoc_expand(void) {
  old_hashtable = primary_hashtable;

  primary_hashtable = pptr<pptr<item> >(
      (pptr<item>*)RP_calloc(hashsize(hashpower + 1), sizeof(pptr<item>)));
  if (primary_hashtable != nullptr) {
    // Hash table expansion starting
    RP_set_root((void*)(&*old_hashtable), RPMRoot::OldHT);
    RP_set_root((void*)(&*primary_hashtable), RPMRoot::PrimaryHT);
    hashpower++;
    expanding = true;
    expand_bucket = 0;
    STATS_LOCK();
    stats_state.hash_power_level = hashpower;
    stats_state.hash_bytes += hashsize(hashpower) * sizeof(void *);
    stats_state.hash_is_expanding = true;
    STATS_UNLOCK();
  } else {
    primary_hashtable = old_hashtable;
    /* Bad news, but we can keep running. */
  }
}

void assoc_start_expand(uint64_t curr_items) {
  if (started_expanding)
    return;

  if (curr_items > (hashsize(hashpower) * 3) / 2 &&
      hashpower < HASHPOWER_MAX) {
    started_expanding = true;
    pthread_cond_signal(&maintenance_cond);
  }
}

/* Note: this isn't an assoc_update.  The key must not already exist to call this */
int assoc_insert(item *it, const uint32_t hv) {
  unsigned int oldbucket;

  if (expanding &&
      (oldbucket = (hv & hashmask(hashpower - 1))) >= expand_bucket)
  {
    it->h_next = old_hashtable[oldbucket];
    old_hashtable[oldbucket] = it;
  } else {
    size_t ind = hv & hashmask(hashpower);
    it->h_next = primary_hashtable[ind];
    primary_hashtable[ind] = pptr<item>(it);
  }
  return 1;
}

void assoc_delete(const char *key, const size_t nkey, const uint32_t hv) {
  pptr<item> *before = _hashitem_before(key, nkey, hv);

  if (*before != nullptr) {
    pptr<item> nxt;
    nxt = (*before)->h_next;
    (*before)->h_next = 0;   /* probably pointless, but whatever. */
    *before = nxt;
    return;
  }
  /* Note:  we never actually get here.  the callers don't delete things
     they can't find. */
  assert(*before != 0);
}


static volatile int do_run_maintenance_thread = 1;

#define DEFAULT_HASH_BULK_MOVE 1
int hash_bulk_move = DEFAULT_HASH_BULK_MOVE;

static void *assoc_maintenance_thread(void *arg) {

  mutex_lock(&maintenance_lock);
  while (do_run_maintenance_thread) {
    int ii = 0;

    /* There is only one expansion thread, so no need to global lock. */
    for (ii = 0; ii < hash_bulk_move && expanding; ++ii) {
      item *it, *next;
      unsigned int bucket;
      void *item_lock = NULL;

      /* bucket = hv & hashmask(hashpower) =>the bucket of hash table
       * is the lowest N bits of the hv, and the bucket of item_locks is
       *  also the lowest M bits of hv, and N is greater than M.
       *  So we can process expanding with only one item_lock. cool! */
      if ((item_lock = item_trylock(expand_bucket))) {
        for (it = old_hashtable[expand_bucket]; NULL != it; it = next) {
          next = it->h_next;
          bucket = tcd_hash(ITEM_key(it), it->nkey) & hashmask(hashpower);
          it->h_next = primary_hashtable[bucket];
          primary_hashtable[bucket] = it;
        }

        old_hashtable[expand_bucket] = NULL;

        expand_bucket++;
        if (expand_bucket == hashsize(hashpower - 1)) {
          expanding = false;
          RP_free(old_hashtable);
          STATS_LOCK();
          stats_state.hash_bytes -= hashsize(hashpower - 1) * sizeof(void *);
          stats_state.hash_is_expanding = false;
          STATS_UNLOCK();
          // hash table expansion done
        }
      } else {
        usleep(10*1000);
      }

      if (item_lock) {
        item_trylock_unlock(item_lock);
        item_lock = NULL;
      }
    }

    if (!expanding) {
      /* We are done expanding.. just wait for next invocation */
      started_expanding = false;
      pthread_cond_wait(&maintenance_cond, &maintenance_lock);
      /* assoc_expand() swaps out the hash table entirely, so we need
       * all threads to not hold any references related to the hash
       * table while this happens.
       * This is instead of a more complex, possibly slower algorithm to
       * allow dynamic hash table expansion without causing significant
       * wait times.
       */
      // If stopping thread, we'll get here. Need to check that we're not
      // ending before we expand
      if (do_run_maintenance_thread){
        pause_accesses();
        assoc_expand();
        unpause_accesses();
      }
    }
  }
  return NULL;
}

static pthread_t maintenance_tid;

int start_assoc_maintenance_thread() {
  int ret;
  char *env = getenv("MEMCACHED_HASH_BULK_MOVE");
  if (env != NULL) {
    hash_bulk_move = atoi(env);
    if (hash_bulk_move == 0) {
      hash_bulk_move = DEFAULT_HASH_BULK_MOVE;
    }
  }
  pthread_mutex_init(&maintenance_lock, NULL);
  if ((ret = pthread_create(&maintenance_tid, NULL,
          assoc_maintenance_thread, NULL)) != 0) {
    fprintf(stderr, "Can't create thread: %s\n", strerror(ret));
    return -1;
  }
  return 0;
}

void stop_assoc_maintenance_thread() {
  mutex_lock(&maintenance_lock);
  do_run_maintenance_thread = 0;
  pthread_cond_signal(&maintenance_cond);
  mutex_unlock(&maintenance_lock);

  /* Wait for the maintenance thread to stop */
  pthread_join(maintenance_tid, NULL);
}

