#pragma once

#include <stdlib.h>

namespace RERO {
  // return epoch that the transaction exists in
  unsigned begin_tx(void);

  // end the current transaction
  void end_tx(unsigned c);

  // advance the epoch and persist any remaining transactions
  // that happened > 1 epoch ago
  void advance_epoch(unsigned e);

  // help functions to assist in the 
  void help_free(unsigned n);
  void help_persist_old(unsigned n);
  void help_persist_new(unsigned n);

  // initialize shared memory
  void init_persistence();

  // Box to store your cache lines
  class CL_Box {
    public:
    virtual void add(void* addr, size_t sz) = 0;
  };
  
  // You should inherit from RERO::obj if you want to persist or free
  // your objects with this method
  class obj {
    public:
    virtual void commit_memory(CL_Box &box) = 0;
  };

  void add_to_persist(obj *o);
  void add_to_free(obj *o);

}
