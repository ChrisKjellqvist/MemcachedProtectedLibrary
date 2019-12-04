#include <pku_memcached.h>
#include <memcached.h>

#include <stdlib.h>
#include <hodor-plib.h>
#include <hodor.h>
#include <sys/mman.h>
#include <rpmalloc.hpp>

extern std::atomic<int> *end_signal;

extern "C" {

  // Implement replace, prepend, append, delete API

HODOR_FUNC_ATTR 
memcached_return_t 
memcached_set(char* key, size_t nkey, char *data, size_t datan, 
    uint32_t exptime, uint32_t flags){
  return pku_memcached_set(key, nkey, data, datan, exptime);
} HODOR_FUNC_EXPORT(memcached_set, 6);

HODOR_FUNC_ATTR 
memcached_return_t
memcached_add(char* key, size_t nkey, uint32_t exptime, char* data, 
    size_t datan, uint32_t flags){ 
  return pku_memcached_insert(key, nkey, data, datan, exptime);
} HODOR_FUNC_EXPORT(memcached_insert, 6);

HODOR_FUNC_ATTR
memcached_return_t
memcached_replace 
  (char* key, size_t nkey, char *data, size_t datan, uint32_t exptime
   uint32_t flags){
  return pku_memcached_replace(key, nkey, data, datan, exptime, flags);
} HODOR_FUNC_EXPORT(memcached_replace, 6);

HODOR_FUNC_ATTR
memcached_return_t
memcached_append(char *key, size_t nkey, char *data, size_t datan,
    uint32_t exptime, uint32_t flags){
  return pku_memcached_append(key, nkey, data, datan, exptime, flags);
} HODOR_FUNC_EXPORT(memcached_append, 6);

HODOR_FUNC_ATTR
memcached_return_t
memcached_prepend(char *key, size_t nkey, char *data, size_t datan,
    uint32_t exptime, uint32_t flags){
  return pku_memcached_prepend(key, nkey, data, datan, exptime, flags);
} HODOR_FUNC_EXPORT(memcached_prepend, 6);

HODOR_FUNC_ATTR
memcached_return_t
memcached_delete 
  (char* key, size_t nkey, uint32_t exptime){
  return pku_memcached_delete(key, nkey, exptime);
} HODOR_FUNC_EXPORT(memcached_delete, 3);

HODOR_FUNC_ATTR 
memcached_return_t 
memcached_increment(char* key, size_t nkey, uint64_t delta, uint64_t *value){
  switch(add_delta(key, nkey, true, delta, value)){
    case OK:
      return MEMCACHED_SUCCESS;
    default:
      return MEMCACHED_FAILURE;
  }
} HODOR_FUNC_EXPORT(memcached_increment, 4);

HODOR_FUNC_ATTR 
memcached_return_t
memcached_decrement(char* key, size_t nkey, uint64_t delta, uint64_t *value){
  switch (add_delta(key, nkey, false, delta, value) == OK){
    case OK:
      return MEMCACHED_SUCCESS;
    default:
      return MEMCACHED_FAILURE;
  }
} HODOR_FUNC_EXPORT(memcached_decrement, 4);

HODOR_FUNC_ATTR
memcached_return_t
memcached_increment_with_initial (char *key, size_t nkey, uint64_t delta,
    uint64_t initial, uint32_t exptime, uint64_t *value) {
  switch(add_delta(key, nkey, true, delta, value) == OK){
    case OK:
      return MEMCACHED_SUCCESS;
    case ITEM_NOT_FOUND:
      char buff[32];
      char *NT = itoa_u64(initial, buff);
      *value = initial;
      return memcached_insert(key, nkey, exptime, buff, NT-(&*buff));
    default:
      return MEMCACHED_FAILURE;
  }
} HODOR_FUNC_EXPORT(memcached_increment_with_initial, 6);

HODOR_FUNC_ATTR
memcached_return_t
memcached_decrement_with_initial (char *key, size_t nkey, uint64_t delta,
    uint64_t initial, uint32_t exptime, uint64_t *value) {
  switch(add_delta(key, nkey, false, delta, value) == OK){
    case OK:
      return MEMCACHED_SUCCESS;
    case ITEM_NOT_FOUND:
      char buff[32];
      char *NT = itoa_u64(initial, buff);
      *value = initial;
      return memcached_insert(key, nkey, exptime, buff, NT-(&*buff));
    default:
      return MEMCACHED_FAILURE;
  }
} HODOR_FUNC_EXPORT(memcached_decrement_with_initial, 6);

HODOR_FUNC_ATTR 
memcached_return_t 
memcached_flush(uint32_t exptime){
  pku_memcached_flush(exptime);
} HODOR_FUNC_EXPORT(memcached_flush, 1);

HODOR_FUNC_ATTR 
memcached_return_t
memcached_end(){
  end_signal->store(1);
} HODOR_FUNC_EXPORT(memcached_end, 0);

// Start memcached maintainence processes
// server is either 0 or 1 to represent whether or not we are initializing
// for a server process or a client process
void memcached_init(int server){
  is_restart = RP_init("memcached.rpma");
  printf("is restart? %d\n", is_restart);
  is_server = server;
  int i = 0;
  void *start, *end;
  while (!RP_region_range(i++, &start, &end)){
    ptrdiff_t rp_region_len = (char*)start - (char*)end;
    pkey_mprotect(start, rp_region_len, PROT_READ | PROT_WRITE | PROT_EXEC, 1);
  }

  agnostic_init();

} HODOR_INIT_FUNC(memcached_init);
}

