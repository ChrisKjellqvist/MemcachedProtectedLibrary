#pragma once
#include <inttypes.h>
#include <pthread.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif
#include "constants.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFINE_API_bare(ret_ty, sym, ...) \
  ret_ty sym (memcached_st *ptr, __VA_ARGS__);

#define DEFINE_API(ret_ty, sym, ...) \
  ret_ty sym (memcached_st *ptr, __VA_ARGS__);\
  ret_ty sym##_internal (__VA_ARGS__);

DEFINE_API(memcached_result_st*, memcached_fetch_result,
   memcached_result_st *result, memcached_return_t *error);

DEFINE_API(char *, memcached_get, const char *key, size_t key_length,
    size_t *value_length, uint32_t *flags, memcached_return_t *error);

DEFINE_API(memcached_return_t, memcached_mget,
   const char * const *keys, const size_t *key_length, size_t number_of_keys); 

DEFINE_API(char *, memcached_fetch,
    const char * key, size_t *key_length, size_t *value_length, uint32_t *flags,
    memcached_return_t *error);

DEFINE_API(memcached_return_t, memcached_set,
    const char * key, size_t nkey, const char * data, size_t datan, uint32_t exptime,
    uint32_t flags);

DEFINE_API(memcached_return_t, memcached_add,
    const char * key, size_t nkey, const char * data, size_t datan, uint32_t exptime,
    uint32_t flags);

DEFINE_API(memcached_return_t, memcached_replace,
    const char * key, size_t nkey, const char * data, size_t datan, uint32_t exptime,
    uint32_t flags);

DEFINE_API(memcached_return_t, memcached_prepend,
    const char * key, size_t nkey, const char * data, size_t datan, uint32_t exptime,
    uint32_t flags);

DEFINE_API(memcached_return_t, memcached_append,
    const char * key, size_t nkey, const char * data, size_t datan, uint32_t exptime,
    uint32_t flags);

DEFINE_API(memcached_return_t, memcached_delete,
    const char * key, size_t nkey, uint32_t exptime);

DEFINE_API(memcached_return_t, memcached_increment,
    const char * key, size_t nkey, uint64_t delta, uint64_t *value);

DEFINE_API(memcached_return_t, memcached_decrement,
    const char * key, size_t nkey, uint64_t delta, uint64_t *value);

DEFINE_API(memcached_return_t, memcached_increment_with_initial,
    const char * key, size_t nkey, uint64_t delta, uint64_t initial, uint32_t exptime,
    uint64_t *value);

DEFINE_API(memcached_return_t, memcached_decrement_with_initial,
    const char * key, size_t nkey, uint64_t delta, uint64_t initial, uint32_t exptime,
    uint64_t *value);

DEFINE_API(memcached_return_t, memcached_flush, uint32_t exptime);

memcached_return_t
memcached_end    ();

void memcached_init();

#ifdef __cplusplus
}
#endif

// include/hodor_plib.h
// HODOR_FUNC_ATTR
// HODOR_INIT_FUNC
// LOCALDISK, qemu-clea.img
