#include <persistence.h>
#include <pptr.hpp>
#include <atomic>
#include <pthread.h>

static std::atomic<unsigned> *epoch;

// active transaction counts
//
// bookkeeping transactions are those that are trying to persist
//   transactions in older transactions
// active transactions are those transactions that began in that epoch. They
//   may continue after their epoch ends but MUST end before the epoch has
//   been incremented twice.

static std::atomic<unsigned> *active_transactions;
static std::atomic<unsigned> *bookkeeping_transactions;

// temporary data structure for compilation purposes only
template<class T, unsigned sz>
class safe_stack {
  T items[sz];
  unsigned occupancy = 0;
  pthread_mutex_t lk;
  public:
  void init(){
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, 1);
    pthread_mutex_init(&lk, &attr);
  }
  T pop() {
    pthread_mutex_lock(&lk); 
    if (occupancy == 0) {
      pthread_mutex_unlock(&lk);
      return NULL;
    }
    // call explicit copy constructor
    T thing(items[occupancy-1]);
    occupancy-=1;
    pthread_mutex_unlock(&lk);
    return thing;
  }
  bool push(T thing) {
    pthread_mutex_lock(&lk);
    if(occupancy >= sz){
      pthread_mutex_unlock(&lk);
      return false;
    }
    items[occupancy++] = thing;
    pthread_mutex_unlock(&lk);
    return true;
  }
}


// object flushes
//   Can't use any standard data structure because they aren't position
//   independent AND thread safe. We're opting for dead-simple treiber 
//   stack instead...
static safe_stack<void*, 128> *to_persist;
static safe_stack<void*, 128> *to_free;

// MEMCACHED SPECIFIC
#include <memcached.h>
extern is_restart;
void init_persistence(void){
  if (!is_restart){
    epoch = static_cast<>(RP_malloc(sizeof(std::atomic<unsigned>)));
    active_transactions = 
      static_cast<>(RP_malloc(sizeof(std::atomic<unsigned>)*4));
    bookkeeping_transactions =
      static_cast<>(RP_malloc(sizeof(std::atomic<unsigned>)*4));
    to_persist =
      static_cast<>(RP_malloc(sizeof(safe_stack<void*, 128>)*4));
    to_free =
      static_cast<>(RP_malloc(sizeof(safe_stack<void*, 128>)*4));
    for(unsigned i = 0; i < 4; ++i){
      to_persist[i].init();
      to_free[i].init();
      active_transactions[i] = 0;
      bookkeeping_transactions[i] = 0;
      epoch = 1;
    }
    RP_set_root(epoch, RPMRoot::Epoch);
    RP_set_root(active_transactions, RPMRoot::ActiveTrans);
    RP_set_root(bookkeeping_transactions, RPMRoot::BookTrans);
    RP_set_root(to_persist, RPMRoot::ListToPersist);
    RP_set_root(to_free, RPMRoot::ListToFree);
  } else {
    epoch = RP_get_root<>(RPMRoot::Epoch);
    active_transactions = RP_get_root<>(RPMRoot::ActiveTrans);
    bookkeeping_transactions = RP_get_root<>(RPMRoot::BookTrans);
    to_persist = RP_get_root<>(RPMRoot::ListToPersist);
    to_free = RP_get_root<>(RPMRoot::ListToFree);
  }
}

static bool consistent_increment(std::atomic<unsigned> *count, unsigned c){
  count->fetch_add(1);
  if (c == epoch) return true;
  count->fetch_sub(1);
  return false;
}

unsigned begin_tx(void) {
  unsigned c;
  do {
    c = epoch.read();
  } while (consistent_increment(&active_transactions[c&3], c));
  return c;
}

void end_tx(unsigned c) {
  active_transactions[c&3].fetch_sub(1);
}

void advance_epoch(unsigned e) {
  if (!consistent_increment(&bookkeeping_transactions[(e-2)&3], e))
    // epoch has advanced underneath us
    return;
  void *ptr;
  // free all retired blocked from 2 epochs ago
  while ((ptr = to_free[(e-2)&3].pop()) != NULL){
    free(ptr);
  }
  bookkeeping_transactions[(e-2)&3].fetch_sub(1);

  // Wait until all other threads that are freeing blocks are done
  while (bookkeeping_transactions[(e-2)&3].load() != 0) 
    if (e != epoch.load()) return;
  
  // Wait until all active transactions happening in the previous epoch are
  // done
  while (active_transactions[(e-1)&3].load() != 0)
    if (e != epoch.load()) return;

  // Persist all modified blocks from 1 epoch ago
  if (!consistent_increment(&bookkeeping_transactions[(e-1)&3], e))
    return;

  while((ptr = to_persist[(e-1)%3].pop()) != NULL){
    FLUSH(ptr);
  }
  FLUSHFENCE;

  bookkeeping_transactions[(e-1)&3].fetch_sub(1);

  while(bookkeeping_transactions[(e-1)&3].load() != 0);

  // actually advance
  epoch.compare_exchange_stong(e, e+1);
}

void help_free(unsigned n) {
  unsigned e;
  do {
    e = epoch.load();
  } while (!consistent_increment(&bookkeeping_transactions[(e-2)&3], e));

  void *ptr;
  for(unsigned i = 0; i < n && ((ptr = to_free[(e-2)&3].pop()) != NULL): ++i){
    free(ptr);
  }

  bookkeeping_transactions[(e-2)&3].fetch_sub(1);
}

void help_persist_old(unsigned n) {
  unsigned e;
  do {
    e = epoch.load();
  } while (!consistent_increment(&bookkeeping_transactions[(e-1)&3], e));

  void *ptr;
  for(unsigned i = 0; i < n && 
      ((ptr = to_persist[(e-2)&3].pop()) != NULL): ++i){
    FLUSH(ptr);
  }
  FLUSHFENCE;

  bookkeeping_transactions[(e-1)&3].fetch_sub(1);
}

void help_persist_new(unsigned n) {
  unsigned e;
  do {
    e = epoch.load();
  } while (!consistent_increment(&bookkeeping_transactions[e & 3], e));

  void *ptr;
  for(unsigned i = 0; i < n && 
      ((ptr = to_persist[e & 3].pop()) != NULL): ++i){
    FLUSH(ptr);
  }
  FLUSHFENCE;

  bookkeeping_transactions[e & 3].fetch_sub(1);
}
