#ifndef PKU_MEMCACHED_H
#define PKU_MEMCACHED_H
#include <inttypes.h>
#include <pthread.h>
#ifndef __cplus_plus
#include <stdbool.h>
#endif
#include "constants.h"


#ifdef __cplus_plus
extern "C" {
#endif

// ---------------- TODO - think about what to do about get interface

memcached_return_t
memcached_set    
  (char* key, size_t nkey, char *data, size_t datan, uint32_t exptime,
   uint32_t flags);

memcached_return_t
memcached_add 
  (char* key, size_t nkey, char *data, size_t datan, uint32_t exptime
   uint32_t flags);

memcached_return_t
memcached_replace 
  (char* key, size_t nkey, char *data, size_t datan, uint32_t exptime
   uint32_t flags);

memcached_return_t
memcached_prepend 
  (char* key, size_t nkey, char *data, size_t datan, uint32_t exptime
   uint32_t flags);

memcached_return_t
memcached_append 
  (char* key, size_t nkey, char *data, size_t datan, uint32_t exptime
   uint32_t flags);

memcached_return_t
memcached_delete 
  (char* key, size_t nkey, uint32_t exptime);

memcached_return_t
memcached_increment   
  (char *key, size_t nkey, uint64_t delta, uint64_t *value);

memcached_return_t
memcached_decrement 
  (char *key, size_t nkey, uint64_t delta, uint64_t *value);

memcached_return_t
memcached_increment_with_initial
  (char *key, size_t nkey, uint64_t delta, uint64_t initial, uint32_t exptime,
   uint64_t *value);  

memcached_return_t
memcached_decrement_with_initial
  (char *key, size_t nkey, uint64_t delta, uint64_t initial, uint32_t exptime,
   uint64_t *value);  

memcached_return_t
memcached_flush
  (uint32_t exptime);

memcached_return_t
memcached_end    ();

void memcached_init(int server);

// API that we deprecate
#include <assert.h>

// Do not support callbacks because it makes no sense in our context
#define memcached_mget_execute(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_mget_execute is not supported!\n");
#define memcached_mget_execute_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_mget_execute_by_key is not supported!\n");
#define memcached_mget_fetch_execute(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_mget_fetch_execute is not supported!\n");
#define memcached_mget_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_mget_by_key is not supported!\n");
#define memcached_get_by_key(...) \
  nullptr;\
  assert(0 && "memcached_get_by_key is not supported!\n");
#define memcached_increment_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_increment_by_key is not supported!\n");
#define memcached_decrement_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_decrement_by_key is not supported!\n");
#define memcached_increment_with_initial_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_increment_with_initial_by_key is not supported!\n");
#define memcached_decrement_with_initial_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_decrement_with_intial_by_key is not supported!\n");
#define memcached_behavior_get(...) \
  0;\
  assert(0 && "memcached_behavior_get is not supported!\n");
#define memcached_behavior_set(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_behavior_set is not supported!\n");
#define memcached_callback_set(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_callback_set is not supported!\n");
#define memcached_callback_get(...) \
  nullptr;\
  assert(0 && "memcached_callback_get is not supported!\n");

// WARNING - should we really be failing silently here???
#define memcached_create(...) nullptr
#define memcached_free(...)   ;
#define memcached_clone(...) nullptr
#define memcached_servers_reset(...) ;

#define memcached_delete_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_delete_by_key is not supported!\n");
#define memcached_dump(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_dump is not supported!\n");
#define memcached_flush_buffers(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_flush_buffers is not supported!\n");
#define memcached_generate_hash_value(...) \
  0;\
  assert(0 && "memcached_generate_hash_value is not supported!\n");
#define memcached_generate_hash(...) \
  0;\
  assert(0 && "memcached_generate_hash is not supported!\n");
#define memcached_set_memory_allocators(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_set_memory_allocators is not supported!\n");
#define memcached_get_memory_allocators(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_get_memory_allocators is not supported!\n");
#define memcached_get_memory_allocators_context(...) \
  nullptr;\
  assert(0 && "memcached_get_memory_allocators_context is not supported!\n");
#define memcached_pool_create(...) \
  nullptr;\
  assert(0 && "memcached_pool_create is not supported!\n");
#define memcached_pool_destroy(...) \
  nullptr;\
  assert(0 && "memcached_pool_destroy is not supported!\n");
#define memcached_pool_pop(...) \
  nullptr;\
  assert(0 && "memcached_pool_pop is not supported!\n");
#define memcached_pool_push(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_pool_push is not supported!\n");
#define memcached_pool_behavior_set(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_pool_behavior_set is not supported!\n");
#define memcached_pool_behavior_get(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_pool_behavior_get is not supported!\n");
#define memcached_quit() ;
#define memcached_server_list_free(...) ;
#define memcached_server_list_append(...) \
  nullptr;\
  assert(0 && "memcached_server_list_append is not supported!\n");
#define memcached_server_list_count(...) \
  0;\
  assert(0 && "memcached_server_list_count is not supported!\n");
#define memcached_servers_parse(...) \
  nullptr;\
  assert(0 && "memcached_servers_parse is not supported!\n");
#define memcached_server_error(...) \
  nullptr;\
  assert(0 && "memcached_ is not supported!\n");
#define memcached_server_error_reset(...) assert(0 && "memcached_server_error_reset is not supported!\n");
#define memcached_server_count(...) \
  0;\
  assert(0 && "memcached_server_count is not supported!\n");
#define memcached_server_list(...) \
  nullptr;\
  assert(0 && "memcached_server_list is not supported!\n");
#define memcached_server_add(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_server_add is not supported!\n");
#define memcached_server_add_udp(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_server_add_udp is not supported!\n");
#define memcached_server_add_unix_socket(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_server_add_unix_socket is not supported!\n");
#define memcached_server_push(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_server_push is not supported!\n");
#define memcached_server_push_by_key(...) \
  nullptr;\
  assert(0 && "memcached_server_push_by_key is not supported!\n");
#define memcached_server_get_last_disconnect(...) \
  nullptr;\
  assert(0 && "memcached_server_get_last_disconnect is not supported!\n");
#define memcached_server_cursor(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_server_cursor is not supported!\n");
#define memcached_cas(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_cas is not supported!\n");
#define memcached_set_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_set_by_key is not supported!\n");
#define memcached_add_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_add_by_key is not supported!\n");
#define memcached_replace_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_replace_by_key is not supported!\n");
#define memcached_prepend_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_prepend_by_key is not supported!\n");
#define memcached_append_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_append_by_key is not supported!\n");
#define memcached_cas_by_key(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_cas_by_key is not supported!\n");
// Maybe reenable stats?
#define memcached_stat(...) \
  nullptr;\
  assert(0 && "memcached_stat is not supported!\n");
#define memcached_stat_servername(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_stat_servername is not supported!\n");
#define memcached_stat_get_value(...) \
  nullptr;\
  assert(0 && "memcached_stat_get_value is not supported!\n");
#define memcached_stat_get_keys(...) \
  nullptr;\
  assert(0 && "memcached_stat_get_keys is not supported!\n");
#define memcached_strerror(...) \
  nullptr;\
  assert(0 && "memcached_strerror is not supported!\n");
#define memcached_get_user_data(...) \
  nullptr;\
  assert(0 && "memcached_get_user_data is not supported!\n");
#define memcached_set_user_data(...) \
  nullptr;\
  assert(0 && "memcached_set_user_data is not supported!\n");
#define memcached_verbosity(...) \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_verbosity is not supported!\n");
#define memcached_lib_version(...) \
  nullptr;\
  assert(0 && "memcached_lib_version is not supported!\n");
#define memcached_version() \
  MEMCACHED_FAILURE;\
  assert(0 && "memcached_version is not supported!\n");

// Only have one server on this machine so master key interface is redundant

#ifdef __cplus_plus
}
#endif

// include/hodor_plib.h
// HODOR_FUNC_ATTR
// HODOR_INIT_FUNC
// LOCALDISK, qemu-clea.img
#endif
