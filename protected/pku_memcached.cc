#include <pku_memcached.h>
#include <memcached.h>

#include <stdlib.h>
#include <hodor-plib.h>
#include <hodor.h>
#include <sys/mman.h>
#include <rpmalloc.hpp>

extern std::atomic<int> *end_signal;

#define INTERNAL(sym)\
#ifndef USE_INTERNAL\
sym ## _internal\
#else\
sym\
#endif


extern "C" {

#define HODOR_FUNC_EXPORT_MCD(sym, num)\
  HODOR_FUNC_EXPORT(\
      INTERNAL(sym)\
      ,num)

#ifndef USE_INTERNAL
{
memcached_result_st*
memcached_fetch_result
  (memcached_st *ptr, memcached_result_st *result, memcached_return_t *error){
  return memcached_fetch_result_internal(result, error);
}

char*
memcached_get
  (memcached_st *ptr, const char *key, size_t key_length, size_t *value_length,
   uint32_t *flags, memcached_return_t *error){
  return memcached_get_internal(key, key_length, value_length, flags, error);
}

memcached_return_t
memcached_mget,
  (memcached_st *ptr, const char * const *keys, const size_t *key_length, 
   size_t number_of_keys){
  return memcached_mget_internal(keys, key_length, number_of_keys);
}

char *
memcached_fetch
  (memcached_st *ptr, char *key, size_t *key_length, size_t *value_length,
   uint32_t *flags, memcached_return_t *error){
  return memcached_fetch_internal(key, key_length, value_length, flags,
      error);
}

memcached_return_t
memcached_set
  (memcached_st *ptr, char* key, size_t nkey, char *data, size_t datan, 
   uint32_t exptime, uint32_t flags){
  return memcached_set_internal(key, nkey, data, datan, exptime, flags);
}

memcached_return_t
memcached_add
  (memcached_st *ptr, char* key, size_t nkey, uint32_t exptime, char* data,
   size_t datan, uint32_t flags){
  return memcached_add_internal(key, nkey, data, datan, exptime);
}

memcached_return_t
memcached_replace
  (char* key, size_t nkey, char *data, size_t datan, uint32_t exptime
   uint32_t flags){
  return memcached_replace_internal(key, nkey, data, datan, exptime, flags);
}

memcached_return_t
memcached_append
  (memcached_st *ptr, char *key, size_t nkey, char *data, size_t datan,
   uint32_t exptime, uint32_t flags){
  return memcached_append_internal(key, nkey, data, datan, exptime, flags);
}

memcached_return_t
memcached_prepend
  (memcached_st *ptr, char *key, size_t nkey, char *data, size_t datan,
   uint32_t exptime, uint32_t flags){
  return memcached_prepend_internal(key, nkey, data, datan, exptime, flags);
}

memcached_return_t
memcached_delete 
  (memcached_str *ptr, char* key, size_t nkey, uint32_t exptime){
  return memcached_delete_internal(key, nkey, exptime);
}

memcached_return_t
memcached_increment
  (memcached_st *ptr, char* key, size_t nkey, uint64_t delta, uint64_t *value){
  return memcached_increment_internal(key, nkey, delta, value);
}

memcached_return_t
memcached_decrement
  (memcached_st *ptr, char* key, size_t nkey, uint64_t delta, uint64_t *value){
  return memcached_decrement_internal(key, nkey, delta, value); 
}

memcached_return_t
memcached_increment_with_initial
  (memcached_st *ptr, char *key, size_t nkey, uint64_t delta, 
   uint64_t initial, uint32_t exptime, uint64_t *value) {
  return memcached_increment_with_initial_internal(key, nkey, delta, initial,
      exptime, value);
}

memcached_return_t
memcached_decrement_with_initial
  (memcached_st *ptr, char *key, size_t nkey, uint64_t delta, 
   uint64_t initial, uint32_t exptime, uint64_t *value) {
  return memcached_decrement_with_initial(key, nkey, delta, initial, exptime,
      value);
}

memcached_return_t
memcached_flush(uint32_t exptime){
  return memcached_flush_internal(exptime);
}
}
#endif

HODOR_FUNC_ATTR
memcached_result_st*
INTERNAL(memcached_fetch_result)
  (memcached_result_st *result, memcached_return_t *error){
  // TODO  
} HODOR_FUNC_EXPORT_MCD(memcached


HODOR_FUNC_ATTR
char *
INTERNAL(memcached_get)
  (const char *key, size_t key_length, size_t *value_length, uint32_t *flags,
   memcached_return_t *error){
  // TODO
}

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_mget)
  (const char * const *keys, const size_t *key_length, size_t number_of_keys){
  // TODO
}

HODOR_FUNC_ATTR

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_set)
  (char* key, size_t nkey, char *data, size_t datan, uint32_t exptime, 
   uint32_t flags){
  return pku_memcached_set(key, nkey, data, datan, exptime);
} HODOR_FUNC_EXPORT_MCD(memcached_set, 6);

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_add_internal)
  (char* key, size_t nkey, uint32_t exptime, char* data,
   size_t datan, uint32_t flags){
  return pku_memcached_insert(key, nkey, data, datan, exptime);
} HODOR_FUNC_EXPORT_MCD(memcached_insert, 6);

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_replace)
  (char* key, size_t nkey, char *data, size_t datan, uint32_t exptime
   uint32_t flags){
  return pku_memcached_replace(key, nkey, data, datan, exptime, flags);
} HODOR_FUNC_EXPORT_MCD(memcached_replace, 6);

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_append)(char *key, size_t nkey, char *data, size_t datan,
    uint32_t exptime, uint32_t flags){
  return pku_memcached_append(key, nkey, data, datan, exptime, flags);
} HODOR_FUNC_EXPORT_MCD(memcached_append, 6);

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_prepend)(char *key, size_t nkey, char *data, size_t datan,
    uint32_t exptime, uint32_t flags){
  return pku_memcached_prepend(key, nkey, data, datan, exptime, flags);
} HODOR_FUNC_EXPORT_MCD(memcached_prepend, 6);

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_delete)(char* key, size_t nkey, uint32_t exptime){
  return pku_memcached_delete(key, nkey, exptime);
} HODOR_FUNC_EXPORT_MCD(memcached_delete, 3);

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_increment)
  (char* key, size_t nkey, uint64_t delta, uint64_t *value){
  switch(add_delta(key, nkey, true, delta, value)){
    case OK:
      return MEMCACHED_SUCCESS;
    default:
      return MEMCACHED_FAILURE;
  }
} HODOR_FUNC_EXPORT_MCD(memcached_increment, 4);

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_decrement)
  (char* key, size_t nkey, uint64_t delta, uint64_t *value){
  switch (add_delta(key, nkey, false, delta, value) == OK){
    case OK:
      return MEMCACHED_SUCCESS;
    default:
      return MEMCACHED_FAILURE;
  }
} HODOR_FUNC_EXPORT_MCD(memcached_decrement, 4);

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_increment_with_initial)
  (char *key, size_t nkey, uint64_t delta, uint64_t initial, uint32_t exptime, 
   uint64_t *value) {
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
} HODOR_FUNC_EXPORT_MCD(memcached_increment_with_initial, 6);

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_decrement_with_initial)
  (char *key, size_t nkey, uint64_t delta, uint64_t initial, uint32_t exptime, 
   uint64_t *value) {
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
} HODOR_FUNC_EXPORT_MCD(memcached_decrement_with_initial, 6);

HODOR_FUNC_ATTR
memcached_return_t
INTERNAL(memcached_flush)(uint32_t exptime){
  return pku_memcached_flush(exptime);
} HODOR_FUNC_EXPORT_MCD(memcached_flush, 1);

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

