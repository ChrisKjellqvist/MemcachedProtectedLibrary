#pragma once
#include <inttypes.h>
#include <pthread.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif
#include "constants.h"
#include "memcached.h"


#ifdef __cplusplus
extern "C" {
#endif

#define DEFINE_API(ret_ty, sym, ...) \
  ret_ty sym (memcached_st *ptr, __VA_ARGS__);\
  ret_ty sym##_internal (__VA_ARGS__);

#ifdef FAIL_ASSERT
#define FAIL(str) assert(0 && #str);
#elif  FAIL_SILENT
#define FAIL(str) ;
#elif  FAIL_WARNING
#define FAIL(str) fprintf(stderr, #str "\n"); fflush(stderr);
#elif  FAIL_FATAL
#define FAIL(str) ;===;
#else
#error No failure preference given
#endif

DEFINE_API(memcached_result_st*, memcached_fetch_result,
   memcached_result_st *result, memcached_return_t *error);

DEFINE_API(char *, memcached_get, const char *key, size_t key_length,
    size_t *value_length, uint32_t *flags, memcached_return_t *error);

DEFINE_API(memcached_return_t, memcached_mget,
   const char * const *keys, const size_t *key_length, size_t number_of_keys); 

DEFINE_API(char *, memcached_fetch,
    char *key, size_t *key_length, size_t *value_length, uint32_t *flags,
    memcached_return_t *error);

DEFINE_API(memcached_return_t, memcached_set,
    char *key, size_t nkey, char *data, size_t datan, uint32_t exptime,
    uint32_t flags);

DEFINE_API(memcached_return_t, memcached_add,
    char *key, size_t nkey, char *data, size_t datan, uint32_t exptime,
    uint32_t flags);

DEFINE_API(memcached_return_t, memcached_replace,
    char *key, size_t nkey, char *data, size_t datan, uint32_t exptime,
    uint32_t flags);

DEFINE_API(memcached_return_t, memcached_prepend,
    char *key, size_t nkey, char *data, size_t datan, uint32_t exptime,
    uint32_t flags);

DEFINE_API(memcached_return_t, memcached_append,
    char *key, size_t nkey, char *data, size_t datan, uint32_t exptime,
    uint32_t flags);

DEFINE_API(memcached_return_t, memcached_delete,
    char *key, size_t nkey, uint32_t exptime);

DEFINE_API(memcached_return_t, memcached_increment,
    char *key, size_t nkey, uint64_t delta, uint64_t *value);

DEFINE_API(memcached_return_t, memcached_decrement,
    char *key, size_t nkey, uint64_t delta, uint64_t *value);

DEFINE_API(memcached_return_t, memcached_increment_with_initial,
    char *key, size_t nkey, uint64_t delta, uint64_t initial, uint32_t exptime,
    uint64_t *value);

DEFINE_API(memcached_return_t, memcached_decrement_with_initial,
    char *key, size_t nkey, uint64_t delta, uint64_t initial, uint32_t exptime,
    uint64_t *value);

DEFINE_API(memcached_return_t, memcached_flush, uint32_t exptime);

memcached_return_t
memcached_end    ();

void memcached_init(int server);

// API that we deprecate
#include <assert.h>

// Do not support callbacks because it makes no sense in our context
#define memcached_mget_execute(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_mget_execute is not supported!);
#define memcached_mget_execute_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_mget_execute_by_key is not supported!);
#define memcached_mget_fetch_execute(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_mget_fetch_execute is not supported!);
#define memcached_mget_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_mget_by_key is not supported!);
#define memcached_get_by_key(...) \
  nullptr;\
  FAIL(memcached_get_by_key is not supported!);
#define memcached_increment_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_increment_by_key is not supported!);
#define memcached_decrement_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_decrement_by_key is not supported!);
#define memcached_increment_with_initial_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_increment_with_initial_by_key is not supported!);
#define memcached_decrement_with_initial_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_decrement_with_intial_by_key is not supported!);
#define memcached_behavior_get(...) \
  0;\
  FAIL(memcached_behavior_get is not supported!);
#define memcached_behavior_set(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_behavior_set is not supported!);
#define memcached_callback_set(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_callback_set is not supported!);
#define memcached_callback_get(...) \
  nullptr;\
  FAIL(memcached_callback_get is not supported!);

// WARNING - should we really be failing silently here???
#define memcached_create(...) nullptr
#define memcached_free(...)   ;
#define memcached_clone(...) nullptr
#define memcached_servers_reset(...) ;

#define memcached_delete_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_delete_by_key is not supported!);
#define memcached_dump(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_dump is not supported!);
#define memcached_flush_buffers(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_flush_buffers is not supported!);
#define memcached_generate_hash_value(...) \
  0;\
  FAIL(memcached_generate_hash_value is not supported!);
#define memcached_generate_hash(...) \
  0;\
  FAIL(memcached_generate_hash is not supported!);
#define memcached_set_memory_allocators(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_set_memory_allocators is not supported!);
#define memcached_get_memory_allocators(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_get_memory_allocators is not supported!);
#define memcached_get_memory_allocators_context(...) \
  nullptr;\
  FAIL(memcached_get_memory_allocators_context is not supported!);
#define memcached_pool_create(...) \
  nullptr;\
  FAIL(memcached_pool_create is not supported!);
#define memcached_pool_destroy(...) \
  nullptr;\
  FAIL(memcached_pool_destroy is not supported!);
#define memcached_pool_pop(...) \
  nullptr;\
  FAIL(memcached_pool_pop is not supported!);
#define memcached_pool_push(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_pool_push is not supported!);
#define memcached_pool_behavior_set(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_pool_behavior_set is not supported!);
#define memcached_pool_behavior_get(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_pool_behavior_get is not supported!);
#define memcached_quit() ;
#define memcached_server_list_free(...) ;
#define memcached_server_list_append(...) \
  nullptr;\
  FAIL(memcached_server_list_append is not supported!);
#define memcached_server_list_count(...) \
  0;\
  FAIL(memcached_server_list_count is not supported!);
#define memcached_servers_parse(...) \
  nullptr;\
  FAIL(memcached_servers_parse is not supported!);
#define memcached_server_error(...) \
  nullptr;\
  FAIL(memcached_ is not supported!);
#define memcached_server_error_reset(...) FAIL(memcached_server_error_reset is not supported!);
#define memcached_server_count(...) \
  0;\
  FAIL(memcached_server_count is not supported!);
#define memcached_server_list(...) \
  nullptr;\
  FAIL(memcached_server_list is not supported!);
#define memcached_server_add(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_server_add is not supported!);
#define memcached_server_add_udp(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_server_add_udp is not supported!);
#define memcached_server_add_unix_socket(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_server_add_unix_socket is not supported!);
#define memcached_server_push(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_server_push is not supported!);
#define memcached_server_push_by_key(...) \
  nullptr;\
  FAIL(memcached_server_push_by_key is not supported!);
#define memcached_server_get_last_disconnect(...) \
  nullptr;\
  FAIL(memcached_server_get_last_disconnect is not supported!);
#define memcached_server_cursor(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_server_cursor is not supported!);
#define memcached_cas(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_cas is not supported!);
#define memcached_set_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_set_by_key is not supported!);
#define memcached_add_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_add_by_key is not supported!);
#define memcached_replace_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_replace_by_key is not supported!);
#define memcached_prepend_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_prepend_by_key is not supported!);
#define memcached_append_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_append_by_key is not supported!);
#define memcached_cas_by_key(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_cas_by_key is not supported!);
// Maybe reenable stats?
#define memcached_stat(...) \
  nullptr;\
  FAIL(memcached_stat is not supported!);
#define memcached_stat_servername(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_stat_servername is not supported!);
#define memcached_stat_get_value(...) \
  nullptr;\
  FAIL(memcached_stat_get_value is not supported!);
#define memcached_stat_get_keys(...) \
  nullptr;\
  FAIL(memcached_stat_get_keys is not supported!);
#define memcached_strerror(...) \
  nullptr;\
  FAIL(memcached_strerror is not supported!);
#define memcached_get_user_data(...) \
  nullptr;\
  FAIL(memcached_get_user_data is not supported!);
#define memcached_set_user_data(...) \
  nullptr;\
  FAIL(memcached_set_user_data is not supported!);
#define memcached_verbosity(...) \
  MEMCACHED_FAILURE;\
  FAIL(memcached_verbosity is not supported!);
#define memcached_lib_version(...) \
  nullptr;\
  FAIL(memcached_lib_version is not supported!);
#define memcached_version() \
  MEMCACHED_FAILURE;\
  FAIL(memcached_version is not supported!);

// Only have one server on this machine so master key interface is redundant

#ifdef __cplusplus
}
#endif

// include/hodor_plib.h
// HODOR_FUNC_ATTR
// HODOR_INIT_FUNC
// LOCALDISK, qemu-clea.img
